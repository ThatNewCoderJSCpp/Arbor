#include "build_common.hpp"
#include "pde_engine.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace arbor {
namespace build {

namespace {

enum class Mode { Evolution, Steady, March };

const std::vector<std::string>& all_funs() {
    static const std::vector<std::string> f = { "u", "v", "w" };
    return f;
}

std::string squash(const std::string& s) {
    std::string out;
    for (char ch : s) {
        if (!std::isspace(static_cast<unsigned char>(ch))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

bool blank(const std::string& s) {
    return s.find_first_not_of(" \t") == std::string::npos;
}

int list_count(const std::string& s) {
    if (blank(s)) return 0;
    return static_cast<int>(std::count(s.begin(), s.end(), ',')) + 1;
}

struct Parsed {
    ExprPtr expr;
    ExprPtr user;
    Statement statement;
    std::vector<int> terms;
    int primary = -1;
};

struct Reader {
    Mode mode = Mode::Evolution;
    int dims = 1;
    int nfun = 1;
    Env* env = nullptr;
    PdeProblem* P = nullptr;
    std::string user_letters;
    std::vector<std::string> user_coords;
    std::map<std::string, std::string> coord_map;

    int internal_index(char letter) const {
        if (mode == Mode::March) {
            if (letter == 'x') return 3;
            if (letter == 't') return 0;
            if (letter == 'y') return 1;
            return -1;
        }
        if (letter == 't') return 3;
        if (letter == 'x') return 0;
        if (letter == 'y') return 1;
        if (letter == 'z') return 2;
        return -1;
    }

    int add_term(int fun, const int ord[3], int tord) {
        const std::string name = pde_term_name(fun, ord, tord);
        for (std::size_t k = 0; k < P->terms.size(); ++k) {
            if (P->terms[k].name == name) return static_cast<int>(k);
        }
        PdeTerm t;
        t.fun = fun;
        for (int a = 0; a < 3; ++a) t.ord[a] = ord[a];
        t.tord = tord;
        t.name = name;
        P->terms.push_back(t);
        return static_cast<int>(P->terms.size()) - 1;
    }

    bool parse(const std::string& text, bool allow_terms, bool want_equation, Parsed& out, std::string& err) {
        std::vector<std::string> vars = user_coords;
        for (int f = 0; f < 3; ++f) vars.push_back(all_funs()[static_cast<std::size_t>(f)]);
        ParseResult r = parse_field(text, *env, vars);
        if (!r.ok) {
            err = r.error;
            return false;
        }
        out.statement = r.statement;
        ExprPtr G;
        if (r.statement.relation == Relation::Equal && r.statement.rhs) G = sub(r.statement.lhs, r.statement.rhs);
        else if (r.statement.relation == Relation::None) G = r.statement.lhs;
        else {
            err = "use = here, not an inequality";
            return false;
        }
        if (!want_equation && r.statement.relation == Relation::Equal) {
            if (r.statement.rhs) G = r.statement.rhs;
        }
        std::set<std::string> names;
        collect_names(G, names);
        std::map<std::string, ExprPtr> rename;
        std::vector<std::string> keep = user_coords;
        for (const std::string& name : names) {
            int fun = -1;
            int counts[4];
            if (!pde_parse_term(name, all_funs(), "xyzt", fun, counts)) continue;
            if (!allow_terms) {
                err = name + " can't be used here; this field takes a formula in " + coords_text();
                return false;
            }
            if (fun >= nfun) {
                err = all_funs()[static_cast<std::size_t>(fun)] + " isn't one of the functions here; pick more functions in the dropdown";
                return false;
            }
            int ord[3] = { 0, 0, 0 };
            int tord = 0;
            for (int L = 0; L < 4; ++L) {
                if (counts[L] == 0) continue;
                const char letter = "xyzt"[L];
                if (user_letters.find(letter) == std::string::npos) {
                    err = name + ": " + std::string(1, letter) + " isn't a variable here";
                    return false;
                }
                const int m = internal_index(letter);
                if (m == 3) tord += counts[L];
                else ord[m] += counts[L];
            }
            if (mode == Mode::Steady && tord > 0) {
                err = name + ": a steady state has no time; use Time evolution for equations with time derivatives";
                return false;
            }
            const int idx = add_term(fun, ord, tord);
            out.terms.push_back(idx);
            rename[name] = variable(P->terms[static_cast<std::size_t>(idx)].name);
            keep.push_back(name);
        }
        out.user = G;
        std::string perr;
        ExprPtr prepared = prepare(G, *env, keep, perr);
        if (!prepared) {
            err = perr;
            return false;
        }
        for (const auto& [from, to] : coord_map) rename[from] = variable(to);
        out.expr = simplify(substitute(prepared, rename));
        if (allow_terms) {
            std::set<std::string> left;
            collect_names(r.statement.relation == Relation::Equal && want_equation ? r.statement.lhs : G, left);
            int best = -1;
            for (const std::string& name : left) {
                int fun = -1;
                int counts[4];
                if (pde_parse_term(name, all_funs(), "xyzt", fun, counts) && fun < nfun) best = best < 0 ? fun : std::min(best, fun);
            }
            if (best < 0) {
                for (int idx : out.terms) {
                    const int fun = P->terms[static_cast<std::size_t>(idx)].fun;
                    best = best < 0 ? fun : std::min(best, fun);
                }
            }
            out.primary = best;
        }
        return true;
    }

    std::string coords_text() const {
        std::string s;
        for (std::size_t i = 0; i < user_coords.size(); ++i) {
            if (i) s += i + 1 == user_coords.size() ? " and " : ", ";
            s += user_coords[i];
        }
        return s;
    }
};

struct Built {
    PdeProblem P;
    std::vector<std::string> axis_names;
    std::string time_name;
    double at = 0.0;
    std::vector<Statement> shown;
    std::string key;
    std::vector<std::string> notes;
};

int pick_count(const std::string& value, int most) {
    return std::clamp(std::max(1, list_count(value)), 1, most);
}

bool read_problem(const Item& item, Ctx& c, Mode mode, Built& b, std::string& err) {
    PdeProblem& P = b.P;
    Reader rd;
    rd.mode = mode;
    rd.env = &c.env;
    rd.P = &P;
    int dims = pick_count(item.field("space"), mode == Mode::March ? 2 : 3);
    if (mode == Mode::Evolution && item.fields.find("space") == item.fields.end()) {
        dims = 1;
        for (const char* id : { "eq", "init", "init2", "edge" }) {
            ParseResult r = parse_field(item.field(id), c.env, { "x", "y", "z", "t", "u", "v", "w" });
            if (!r.ok) continue;
            std::set<std::string> names;
            collect_names(r.statement.lhs, names);
            if (r.statement.rhs) collect_names(r.statement.rhs, names);
            for (const std::string& n : names) {
                int fun = -1;
                int counts[4];
                if (n == "y" || (pde_parse_term(n, all_funs(), "xyzt", fun, counts) && counts[1] > 0)) dims = 2;
            }
        }
    }
    const int nfun = pick_count(item.field("functions"), 3);
    rd.dims = dims;
    rd.nfun = nfun;
    P.dims = dims;
    P.nfun = nfun;
    P.steady = mode == Mode::Steady;
    if (mode == Mode::March) {
        rd.user_letters = dims == 2 ? "xty" : "xt";
        rd.user_coords = dims == 2 ? std::vector<std::string>{ "x", "t", "y" } : std::vector<std::string>{ "x", "t" };
        rd.coord_map = { { "x", "t" }, { "t", "x" } };
        b.axis_names = dims == 2 ? std::vector<std::string>{ "t", "y" } : std::vector<std::string>{ "t" };
        b.time_name = "x";
    } else {
        const std::string axes = std::string("xyz").substr(0, static_cast<std::size_t>(dims));
        rd.user_letters = axes + (mode == Mode::Evolution ? "t" : "");
        for (char a : axes) {
            rd.user_coords.push_back(std::string(1, a));
            b.axis_names.push_back(std::string(1, a));
        }
        if (mode == Mode::Evolution) {
            rd.user_coords.push_back("t");
            b.time_name = "t";
        }
    }
    static const char* const eq_ids[] = { "eq", "eq_v", "eq_w" };
    static const char* const init_ids[] = { "init", "init_v", "init_w" };
    static const char* const guess_ids[] = { "guess", "guess_v", "guess_w" };
    for (int f = 0; f < nfun; ++f) {
        const std::string text = item.field(eq_ids[f]);
        if (blank(text)) {
            err = "write the equation for " + all_funs()[static_cast<std::size_t>(f)];
            return false;
        }
        Parsed p;
        if (!rd.parse(text, true, true, p, err)) {
            err = (nfun > 1 ? all_funs()[static_cast<std::size_t>(f)] + " equation: " : std::string()) + err;
            return false;
        }
        P.eqs.push_back(p.expr);
        b.shown.push_back(p.statement);
    }
    P.korder.assign(static_cast<std::size_t>(nfun), 0);
    for (const PdeTerm& t : P.terms) P.korder[static_cast<std::size_t>(t.fun)] = std::max(P.korder[static_cast<std::size_t>(t.fun)], t.tord);
    int kmax = 0;
    for (int k : P.korder) kmax = std::max(kmax, k);
    if (mode != Mode::Steady && kmax == 0) {
        err = mode == Mode::March ? "the equation needs an x derivative (like u_xx) to march along x" : "the equation has no time derivative; use Steady state for equations like u_xx + u_yy = 0";
        return false;
    }
    if (kmax > 6) {
        err = "Arbor handles up to sixth time derivatives";
        return false;
    }
    P.init.assign(static_cast<std::size_t>(nfun), {});
    for (int f = 0; f < nfun; ++f) {
        const int k = P.korder[static_cast<std::size_t>(f)];
        std::string text = mode == Mode::Steady ? item.field(guess_ids[f]) : item.field(init_ids[f]);
        std::vector<std::string> parts = mode == Mode::Steady ? std::vector<std::string>{ text } : split_top_level(text, ',');
        if (f == 0 && mode == Mode::Evolution && static_cast<int>(parts.size()) < 2 && k >= 2 && item.fields.find("init2") != item.fields.end() && !blank(item.field("init2"))) {
            if (parts.empty()) parts.push_back("0");
            parts.push_back(item.field("init2"));
        }
        const int want = std::max(k, 1);
        if (mode != Mode::Steady && static_cast<int>(parts.size()) > want) {
            const std::string fn = all_funs()[static_cast<std::size_t>(f)];
            err = "starting values for " + fn + ": this equation needs " + std::to_string(want) + (want == 1 ? " value" : " values") + " (" + fn;
            for (int j = 1; j < want; ++j) err += ", " + fn + "_" + std::string(static_cast<std::size_t>(j), b.time_name.empty() ? 't' : b.time_name[0]);
            err += "), not " + std::to_string(parts.size());
            return false;
        }
        for (int j = 0; j < want; ++j) {
            if (static_cast<std::size_t>(j) >= parts.size() || blank(parts[static_cast<std::size_t>(j)])) {
                P.init[static_cast<std::size_t>(f)].push_back(number(0.0));
                continue;
            }
            Parsed p;
            if (!rd.parse(parts[static_cast<std::size_t>(j)], false, false, p, err)) {
                err = std::string(mode == Mode::Steady ? "starting guess" : "starting values") + (nfun > 1 ? " for " + all_funs()[static_cast<std::size_t>(f)] : std::string()) + ": " + err;
                return false;
            }
            P.init[static_cast<std::size_t>(f)].push_back(p.expr);
        }
        if (mode != Mode::Steady && static_cast<int>(parts.size()) < want && k > 0) b.notes.push_back("missing starting values for " + all_funs()[static_cast<std::size_t>(f)] + " are 0");
    }
    const std::vector<std::string> axis_fields = mode == Mode::March ? std::vector<std::string>{ "t", "y" } : std::vector<std::string>{ "x", "y", "z" };
    const bool legacy_edge = mode == Mode::Evolution && item.fields.find("edge") != item.fields.end() && item.fields.find("edge_xmin") == item.fields.end();
    for (int a = 0; a < dims; ++a) {
        const std::string& an = axis_fields[static_cast<std::size_t>(a)];
        const std::string lo_id = an + "min";
        const std::string hi_id = an + "max";
        double lo = -3.141592653589793, hi = 3.141592653589793;
        if (!blank(item.field(lo_id)) && !parse_number(item.field(lo_id), c.env, lo, err)) {
            err = an + " from: " + err;
            return false;
        }
        if (!blank(item.field(hi_id)) && !parse_number(item.field(hi_id), c.env, hi, err)) {
            err = an + " to: " + err;
            return false;
        }
        if (!(hi > lo)) {
            err = "the " + an + " range has to run from a smaller number to a bigger one";
            return false;
        }
        P.lo[a] = lo;
        P.hi[a] = hi;
        for (int s = 0; s < 2; ++s) {
            const std::string id = "edge_" + an + (s == 0 ? "min" : "max");
            const std::string text = legacy_edge ? item.field("edge") : item.field(id);
            PdeEdge& edge = P.edge[a][s];
            const std::string word = squash(text);
            if (word == "periodic" || word == "wrap" || word == "wraparound") {
                edge.mode = PdeEdgeMode::Periodic;
                continue;
            }
            if (word == "insulated" || word == "neumann" || word == "noflux" || word == "reflecting" || word == "mirror") {
                edge.mode = PdeEdgeMode::Insulated;
                continue;
            }
            if (word.empty() || word == "free" || word == "open" || word == "outflow" || word == "extrapolate" || word == "none") {
                edge.mode = PdeEdgeMode::Free;
                continue;
            }
            int bare = 0;
            for (const std::string& part : split_top_level(text, ',')) {
                if (blank(part)) continue;
                Parsed p;
                if (!rd.parse(part, true, true, p, err)) {
                    err = "edge " + an + (s == 0 ? " from" : " to") + ": " + err;
                    return false;
                }
                PdeCondition cond;
                cond.text = part;
                if (p.statement.relation == Relation::None) {
                    if (bare >= nfun) {
                        err = "edge " + an + (s == 0 ? " from" : " to") + ": too many values";
                        return false;
                    }
                    int zero[3] = { 0, 0, 0 };
                    const int idx = rd.add_term(bare, zero, 0);
                    cond.residual = simplify(sub(variable(P.terms[static_cast<std::size_t>(idx)].name), p.expr));
                    cond.fun = bare;
                    p.terms.push_back(idx);
                    ++bare;
                } else {
                    cond.residual = p.expr;
                    cond.fun = p.primary < 0 ? 0 : p.primary;
                }
                int reach = 0;
                for (int idx : p.terms) {
                    const int o = P.terms[static_cast<std::size_t>(idx)].ord[a];
                    reach = std::max(reach, o == 0 ? 0 : o <= 2 ? 1 : 2);
                    if (P.terms[static_cast<std::size_t>(idx)].tord > P.korder[static_cast<std::size_t>(P.terms[static_cast<std::size_t>(idx)].fun)]) {
                        err = "edge " + an + (s == 0 ? " from" : " to") + ": " + P.terms[static_cast<std::size_t>(idx)].name + " goes past the time derivatives in the equation";
                        return false;
                    }
                }
                cond.reach = reach;
                cond.value = reach == 0;
                edge.conds.push_back(cond);
            }
        }
        if (P.edge[a][0].mode == PdeEdgeMode::Periodic || P.edge[a][1].mode == PdeEdgeMode::Periodic) {
            P.edge[a][0].mode = PdeEdgeMode::Periodic;
            P.edge[a][1].mode = PdeEdgeMode::Periodic;
        }
    }
    const int q = c.env.quality;
    int pts = dims == 1 ? (q <= 1 ? 81 : q >= 3 ? 241 : 161) : dims == 2 ? (q <= 1 ? 31 : q >= 3 ? 71 : 51) : (q <= 1 ? 15 : q >= 3 ? 31 : 23);
    if (!blank(item.field("points")) && squash(item.field("points")) != "auto") {
        double v = 0.0;
        if (!parse_number(item.field("points"), c.env, v, err)) {
            err = "grid points: " + err;
            return false;
        }
        const int most = dims == 1 ? 4001 : dims == 2 ? 301 : 81;
        pts = std::clamp(static_cast<int>(std::lround(v)), 7, most);
    }
    for (int a = 0; a < dims; ++a) P.n[a] = pts;
    if (mode != Mode::Steady) {
        double start = 0.0, end = 4.0, at = 1.0;
        const std::string end_id = mode == Mode::March ? "xmax" : "tmax";
        if (!blank(item.field("start")) && !parse_number(item.field("start"), c.env, start, err)) {
            err = std::string(mode == Mode::March ? "x from: " : "t from: ") + err;
            return false;
        }
        if (!blank(item.field(end_id)) && !parse_number(item.field(end_id), c.env, end, err)) {
            err = std::string(mode == Mode::March ? "x to: " : "t up to: ") + err;
            return false;
        }
        if (!blank(item.field("at")) && !parse_number(item.field("at"), c.env, at, err)) {
            err = b.time_name + "\xE2\x82\x80: " + err;
            return false;
        }
        if (!(end > start)) {
            err = std::string("the ") + (mode == Mode::March ? "x" : "t") + " range has to run forward";
            return false;
        }
        P.t0 = start;
        P.t1 = end;
        b.at = std::clamp(at, start, end);
        int steps = dims == 1 ? (q <= 1 ? 200 : q >= 3 ? 800 : 400) : dims == 2 ? (q <= 1 ? 120 : q >= 3 ? 360 : 240) : (q <= 1 ? 50 : q >= 3 ? 150 : 90);
        if (!blank(item.field("steps")) && squash(item.field("steps")) != "auto") {
            double v = 0.0;
            if (!parse_number(item.field("steps"), c.env, v, err)) {
                err = "time steps: " + err;
                return false;
            }
            steps = std::clamp(static_cast<int>(std::lround(v)), 1, 200000);
        }
        P.steps = steps;
        P.frames = dims == 1 ? (q <= 1 ? 61 : q >= 3 ? 161 : 101) : dims == 2 ? 61 : 41;
        P.frames = std::min(P.frames, steps + 1);
        const std::string m = squash(item.field("method"));
        if (m.find("crank") != std::string::npos || m.find("trap") != std::string::npos) P.method = PdeMethod::Trapezoid;
        else if (m.find("bdf") != std::string::npos) P.method = PdeMethod::Bdf2;
        else if (m.find("euler") != std::string::npos) P.method = PdeMethod::Euler;
        else P.method = kmax >= 2 ? PdeMethod::Trapezoid : PdeMethod::Bdf2;
    }
    if (mode == Mode::March) {
        int px = 0, pt = 0;
        for (const PdeTerm& t : P.terms) {
            px = std::max(px, t.tord);
            pt = std::max(pt, t.ord[0]);
        }
        if (px > pt) b.notes.push_back("marching along x is only stable for wave-like equations; this one has more x derivatives than t derivatives, so small errors can grow very fast (the sideways heat problem is ill-posed)");
    }
    std::string key = std::to_string(static_cast<int>(mode)) + "|" + std::to_string(dims) + "|" + std::to_string(nfun) + "|" + std::to_string(pts) + "|" + std::to_string(P.steps) + "|" + std::to_string(P.frames) + "|" + std::to_string(static_cast<int>(P.method));
    auto num = [](double v) {
        char buf[40];
        std::snprintf(buf, sizeof buf, "%.17g", v);
        return std::string(buf);
    };
    key += "|" + num(P.t0) + "|" + num(P.t1);
    for (int a = 0; a < dims; ++a) {
        key += "|" + num(P.lo[a]) + "," + num(P.hi[a]);
        for (int s = 0; s < 2; ++s) {
            key += "|e" + std::to_string(static_cast<int>(P.edge[a][s].mode));
            for (const PdeCondition& cd : P.edge[a][s].conds) key += ";" + to_string(cd.residual) + "@" + std::to_string(cd.fun);
        }
    }
    for (const ExprPtr& e : P.eqs) key += "|q" + to_string(e);
    for (const auto& iv : P.init) {
        key += "|i";
        for (const ExprPtr& e : iv) key += ";" + to_string(e);
    }
    b.key = key;
    return true;
}

std::mutex& cache_mutex() {
    static std::mutex m;
    return m;
}

std::deque<std::pair<std::string, std::shared_ptr<const PdeSolution>>>& cache() {
    static std::deque<std::pair<std::string, std::shared_ptr<const PdeSolution>>> c;
    return c;
}

std::shared_ptr<const PdeSolution> cached(const std::string& key) {
    std::lock_guard<std::mutex> lock(cache_mutex());
    for (const auto& [k, v] : cache()) {
        if (k == key) return v;
    }
    return nullptr;
}

void remember(const std::string& key, std::shared_ptr<const PdeSolution> s) {
    std::lock_guard<std::mutex> lock(cache_mutex());
    cache().push_front({ key, std::move(s) });
    while (cache().size() > 10) cache().pop_back();
}

std::vector<double> frame_at(const PdeSolution& s, double t, int fun) {
    const std::size_t n = s.points();
    std::vector<double> out(n, 0.0);
    if (s.frames.empty()) return out;
    std::size_t i = 0;
    while (i + 1 < s.times.size() && s.times[i + 1] < t) ++i;
    const std::vector<double>& a = s.frames[i];
    if (i + 1 >= s.times.size() || t <= s.times[i]) {
        for (std::size_t k = 0; k < n; ++k) out[k] = a[static_cast<std::size_t>(fun) * n + k];
        return out;
    }
    const std::vector<double>& b = s.frames[i + 1];
    const double w = std::clamp((t - s.times[i]) / (s.times[i + 1] - s.times[i]), 0.0, 1.0);
    for (std::size_t k = 0; k < n; ++k) out[k] = (1.0 - w) * a[static_cast<std::size_t>(fun) * n + k] + w * b[static_cast<std::size_t>(fun) * n + k];
    return out;
}

Rgba fun_color(const Item& item, int f) {
    if (f == 0) return item_color(item);
    return f == 1 ? rgba(90, 140, 230) : rgba(63, 166, 90);
}

const char* fun_map(int f) {
    return f == 0 ? "viridis" : f == 1 ? "magma" : "ocean";
}

std::string coord_text(double v, double span) {
    return number_text(std::fabs(v) < 1e-9 * span ? 0.0 : v);
}

void color_values(Mesh3& mesh, const std::vector<double>& h, bool by_height, Rgba col, const char* map, double lo, double hi) {
    mesh.col.clear();
    for (double z : h) mesh.col.push_back(by_height ? colormap(map, hi > lo ? std::clamp((z - lo) / (hi - lo), 0.0, 1.0) : 0.5) : col);
}

void minmax(const std::vector<double>& v, double& lo, double& hi) {
    lo = INFINITY;
    hi = -INFINITY;
    for (double x : v) {
        if (!std::isfinite(x)) continue;
        lo = std::min(lo, x);
        hi = std::max(hi, x);
    }
    if (!(hi >= lo)) {
        lo = 0.0;
        hi = 0.0;
    }
}

void draw_slices(const Item& item, Ctx& c, const PdeSolution& s, const std::vector<double>& u, int f, double lo, double hi, std::string& err) {
    const std::vector<std::string> parts = split_top_level(item.field("slices"), ',');
    for (const std::string& part : parts) {
        const std::string t = squash(part);
        if (t.size() < 3 || t[1] != '=') {
            err = "slices: write planes like z = 0, x = 1";
            return;
        }
        const int axis = t[0] == 'x' ? 0 : t[0] == 'y' ? 1 : t[0] == 'z' ? 2 : -1;
        if (axis < 0) {
            err = "slices: write planes like z = 0, x = 1";
            return;
        }
        double at = 0.0;
        std::string e2;
        if (!parse_number(part.substr(part.find('=') + 1), c.env, at, e2)) {
            err = "slices: " + e2;
            return;
        }
        const double span = s.periodic[axis] ? (s.hi[axis] - s.lo[axis]) * (s.n[axis] - 1) / s.n[axis] : s.hi[axis] - s.lo[axis];
        const double pos = (at - s.lo[axis]) / span * (s.n[axis] - 1);
        if (pos < -1e-9 || pos > s.n[axis] - 1 + 1e-9) continue;
        const int i0 = std::clamp(static_cast<int>(std::floor(pos)), 0, s.n[axis] - 2);
        const double w = std::clamp(pos - i0, 0.0, 1.0);
        const int a1 = (axis + 1) % 3;
        const int a2 = (axis + 2) % 3;
        const int n1 = s.n[a1];
        const int n2 = s.n[a2];
        Mesh3 mesh;
        mesh.lit = false;
        for (int j = 0; j < n2; ++j) {
            for (int i = 0; i < n1; ++i) {
                int idx[3];
                idx[a1] = i;
                idx[a2] = j;
                idx[axis] = i0;
                const std::size_t pa = (static_cast<std::size_t>(idx[2]) * s.n[1] + idx[1]) * s.n[0] + idx[0];
                idx[axis] = i0 + 1;
                const std::size_t pb = (static_cast<std::size_t>(idx[2]) * s.n[1] + idx[1]) * s.n[0] + idx[0];
                const double val = (1.0 - w) * u[pa] + w * u[pb];
                double p[3];
                p[axis] = at;
                p[a1] = s.coord(a1, i);
                p[a2] = s.coord(a2, j);
                mesh.pos.push_back(Vec3{ p[0], p[1], p[2] });
                mesh.col.push_back(colormap(fun_map(f), hi > lo ? std::clamp((val - lo) / (hi - lo), 0.0, 1.0) : 0.5));
            }
        }
        for (int j = 0; j + 1 < n2; ++j) {
            for (int i = 0; i + 1 < n1; ++i) {
                const std::uint32_t a = static_cast<std::uint32_t>(j * n1 + i);
                const std::uint32_t b2 = a + 1;
                const std::uint32_t c2 = a + static_cast<std::uint32_t>(n1);
                const std::uint32_t d2 = c2 + 1;
                mesh.tri.insert(mesh.tri.end(), { a, b2, d2, a, d2, c2 });
            }
        }
        mesh.nrm.assign(mesh.pos.size(), Vec3{ axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0 });
        c.scene.s3.meshes.push_back(std::move(mesh));
    }
    (void)item;
}

void draw_iso(const Item& item, Ctx& c, ItemStatus& st, const PdeSolution& s, const std::vector<double>& u, int f, double lo, double hi) {
    if (s.n[0] != s.n[1] || s.n[1] != s.n[2]) return;
    std::vector<double> levels;
    const std::string text = item.field("level");
    if (blank(text) || squash(text) == "auto") levels.push_back(0.5 * (lo + hi));
    else {
        for (const std::string& part : split_top_level(text, ',')) {
            double v = 0.0;
            std::string e2;
            if (!parse_number(part, c.env, v, e2)) {
                st.error = "level: " + e2;
                return;
            }
            levels.push_back(v);
        }
    }
    Grid3 g;
    g.n = s.n[0];
    for (int a = 0; a < 3; ++a) {
        g.lo[a] = s.lo[a];
        g.hi[a] = s.periodic[a] ? s.lo[a] + (s.hi[a] - s.lo[a]) * (s.n[a] - 1) / s.n[a] : s.hi[a];
    }
    for (std::size_t li = 0; li < levels.size(); ++li) {
        g.v.resize(u.size());
        for (std::size_t k = 0; k < u.size(); ++k) g.v[k] = u[k] - levels[li];
        std::vector<Vec3> tris;
        marching_tetrahedra(g, tris);
        if (tris.empty()) continue;
        Mesh3 mesh;
        mesh.pos = tris;
        for (std::uint32_t i = 0; i < tris.size(); ++i) mesh.tri.push_back(i);
        finish_normals(mesh);
        const Rgba col = item.option("height_color") ? colormap(fun_map(f), hi > lo ? std::clamp((levels[li] - lo) / (hi - lo), 0.0, 1.0) : 0.5) : fun_color(item, f);
        mesh.col.assign(mesh.pos.size(), col);
        mesh.alpha = levels.size() > 1 || item.option("slice") ? 0.55 : 0.85;
        c.scene.s3.meshes.push_back(std::move(mesh));
    }
    st.readouts.push_back({ all_funs()[static_cast<std::size_t>(f)] + " level", [&] {
        std::string out;
        for (std::size_t i = 0; i < levels.size(); ++i) out += (i ? ", " : "") + number_text(levels[i]);
        return out;
    }(), std::string() });
}

std::string method_name(PdeMethod m) {
    return m == PdeMethod::Trapezoid ? "Crank\xE2\x80\x93Nicolson" : m == PdeMethod::Bdf2 ? "BDF2" : "backward Euler";
}

void build_any(const Item& item, Ctx& c, ItemStatus& st, Mode mode) {
    Built b;
    std::string err;
    if (!read_problem(item, c, mode, b, err)) {
        st.error = err;
        return;
    }
    const PdeProblem& P = b.P;
    if (!b.shown.empty()) st.mathtext = display_mathtext(b.shown[0]);
    std::shared_ptr<const PdeSolution> sol = cached(b.key);
    if (!sol) {
        auto fresh = std::make_shared<PdeSolution>();
        if (!solve_pde(P, *fresh, err)) {
            if (build_cancelled() && !item_timed_out()) return;
            st.error = err.empty() ? "couldn't solve this equation" : err;
            return;
        }
        if (!fresh->timed_out) remember(b.key, fresh);
        sol = fresh;
    }
    const PdeSolution& s = *sol;
    if (s.frames.empty()) {
        st.error = "no solution";
        return;
    }
    const int dims = P.dims;
    const int nfun = P.nfun;
    const std::string tn = b.time_name;
    const std::string t0n = tn + "\xE2\x82\x80";
    std::vector<int> shown;
    for (int f = 0; f < nfun; ++f) {
        if (nfun == 1 || item.option(std::string("show_") + all_funs()[static_cast<std::size_t>(f)])) shown.push_back(f);
    }
    if (shown.empty()) shown.push_back(0);
    const double tend = s.times.back();
    const double at = mode == Mode::Steady ? 0.0 : std::min(b.at, tend);
    if (mode != Mode::Steady) offer_param(st, t0n, P.t0, P.t1, b.at);
    const bool by_height = item.option("height_color");
    const std::string ax0 = b.axis_names.empty() ? std::string("x") : b.axis_names[0];
    for (int f : shown) {
        const std::string fn = all_funs()[static_cast<std::size_t>(f)];
        const Rgba col = fun_color(item, f);
        const std::vector<double> u = frame_at(s, at, f);
        double lo = 0.0, hi = 0.0;
        minmax(u, lo, hi);
        if (dims == 1 && mode != Mode::Steady) {
            const int nt = static_cast<int>(s.frames.size());
            const int n = s.n[0] + (s.periodic[0] ? 1 : 0);
            double glo = INFINITY, ghi = -INFINITY;
            for (const auto& fr : s.frames) {
                for (int i = 0; i < s.n[0]; ++i) {
                    const double v = fr[static_cast<std::size_t>(f) * s.points() + static_cast<std::size_t>(i)];
                    if (std::isfinite(v)) {
                        glo = std::min(glo, v);
                        ghi = std::max(ghi, v);
                    }
                }
            }
            auto place = [&](double sp, double tm, double z) { return mode == Mode::March ? Vec3{ tm, sp, z } : Vec3{ sp, tm, z }; };
            if (nt >= 2 && item.option("surface")) {
                Mesh3 mesh;
                std::vector<Vec3> grid;
                surface_mesh(c, [&](const std::vector<double>& us, const std::vector<double>& vs, std::vector<Vec3>& out) {
                    out.resize(us.size());
                    for (std::size_t k = 0; k < us.size(); ++k) {
                        const int i = std::clamp(static_cast<int>(std::lround(us[k])), 0, n - 1);
                        const int j = std::clamp(static_cast<int>(std::lround(vs[k])), 0, nt - 1);
                        const double z = s.frames[static_cast<std::size_t>(j)][static_cast<std::size_t>(f) * s.points() + static_cast<std::size_t>(i % s.n[0])];
                        const double sp = s.periodic[0] && i == n - 1 ? s.hi[0] : s.coord(0, i);
                        out[k] = place(sp, s.times[static_cast<std::size_t>(j)], z);
                    }
                }, 0.0, n - 1.0, 0.0, nt - 1.0, n, nt, mesh, grid);
                std::vector<double> heights;
                for (const Vec3& p : mesh.pos) heights.push_back(p.z);
                color_values(mesh, heights, by_height, col, fun_map(f), glo, ghi);
                c.scene.s3.meshes.push_back(std::move(mesh));
                if (item.option("mesh")) mesh_lines(c, grid, n, nt, std::max(1, n / 16), with_alpha(mix(col, rgba(0, 0, 0), 0.45), 140));
            }
            if (item.option("profile")) {
                Line3 line;
                line.color = item.option("surface") ? c.env.theme.text : col;
                line.width = 3.2f;
                line.on_top = true;
                for (int i = 0; i < s.n[0]; ++i) line.pts.push_back(place(s.coord(0, i), at, u[static_cast<std::size_t>(i)]));
                if (s.periodic[0]) line.pts.push_back(place(s.hi[0], at, u[0]));
                c.scene.s3.lines.push_back(std::move(line));
            }
            if (by_height && f == shown[0]) {
                st.legend_min = number_text(glo);
                st.legend_max = number_text(ghi);
                st.legend_map = fun_map(f);
            }
            if (mode == Mode::March) {
                c.scene.s3.axis_x = "x";
                c.scene.s3.axis_y = "t";
            } else {
                c.scene.s3.axis_y = "t";
            }
        } else if (dims == 1) {
            Line3 line;
            line.color = col;
            line.width = 3.0f;
            for (int i = 0; i < s.n[0]; ++i) line.pts.push_back(Vec3{ s.coord(0, i), 0.0, u[static_cast<std::size_t>(i)] });
            if (s.periodic[0]) line.pts.push_back(Vec3{ s.hi[0], 0.0, u[0] });
            c.scene.s3.lines.push_back(std::move(line));
        } else if (dims == 2) {
            const int n0 = s.n[0] + (s.periodic[0] ? 1 : 0);
            const int n1 = s.n[1] + (s.periodic[1] ? 1 : 0);
            Mesh3 mesh;
            std::vector<Vec3> grid;
            surface_mesh(c, [&](const std::vector<double>& us, const std::vector<double>& vs, std::vector<Vec3>& out) {
                out.resize(us.size());
                for (std::size_t k = 0; k < us.size(); ++k) {
                    const int i = std::clamp(static_cast<int>(std::lround(us[k])), 0, n0 - 1);
                    const int j = std::clamp(static_cast<int>(std::lround(vs[k])), 0, n1 - 1);
                    const double z = u[static_cast<std::size_t>(j % s.n[1]) * s.n[0] + static_cast<std::size_t>(i % s.n[0])];
                    const double x = s.periodic[0] && i == n0 - 1 ? s.hi[0] : s.coord(0, i);
                    const double y = s.periodic[1] && j == n1 - 1 ? s.hi[1] : s.coord(1, j);
                    out[k] = Vec3{ x, y, z };
                }
            }, 0.0, n0 - 1.0, 0.0, n1 - 1.0, n0, n1, mesh, grid);
            std::vector<double> heights;
            for (const Vec3& p : mesh.pos) heights.push_back(p.z);
            color_values(mesh, heights, by_height, col, fun_map(f), lo, hi);
            if (shown.size() > 1) mesh.alpha = 0.8;
            c.scene.s3.meshes.push_back(std::move(mesh));
            if (item.option("mesh")) mesh_lines(c, grid, n0, n1, std::max(1, n0 / 16), with_alpha(mix(col, rgba(0, 0, 0), 0.45), 140));
            if (by_height && f == shown[0]) {
                st.legend_min = number_text(lo);
                st.legend_max = number_text(hi);
                st.legend_map = fun_map(f);
            }
            if (mode == Mode::March) c.scene.s3.axis_x = "t";
        } else {
            if (item.option("iso")) draw_iso(item, c, st, s, u, f, lo, hi);
            if (item.option("slice")) {
                std::string e2;
                draw_slices(item, c, s, u, f, lo, hi, e2);
                if (!e2.empty()) st.error = e2;
            }
            if (f == shown[0]) {
                st.legend_min = number_text(lo);
                st.legend_max = number_text(hi);
                st.legend_map = fun_map(f);
            }
        }
        const std::string when = mode == Mode::Steady ? std::string() : ", " + t0n;
        st.readouts.push_back({ fn + (mode == Mode::Steady ? std::string() : " at " + t0n), "from " + number_text(lo) + " to " + number_text(hi), std::string() });
        int idx[3] = { 0, 0, 0 };
        std::string where;
        for (int a = 0; a < dims; ++a) {
            const double mid = 0.5 * (s.lo[a] + s.hi[a]);
            const double span = s.periodic[a] ? (s.hi[a] - s.lo[a]) / s.n[a] : (s.hi[a] - s.lo[a]) / (s.n[a] - 1);
            idx[a] = std::clamp(static_cast<int>(std::lround((mid - s.lo[a]) / span)), 0, s.n[a] - 1);
            where += (a ? ", " : "") + coord_text(s.coord(a, idx[a]), s.hi[a] - s.lo[a]);
        }
        const double centre = u[(static_cast<std::size_t>(idx[2]) * s.n[1] + idx[1]) * s.n[0] + idx[0]];
        if (mode == Mode::March) st.readouts.push_back({ fn + "(" + t0n + ", " + where + ")", number_text(centre), std::string() });
        else st.readouts.push_back({ fn + "(" + where + when + ")", number_text(centre), std::string() });
    }
    for (std::size_t e = 1; e < b.shown.size(); ++e) st.readouts.push_back({ all_funs()[e] + " equation", display_mathtext(b.shown[e]), display_mathtext(b.shown[e]) });
    if (mode != Mode::Steady) {
        std::string orders;
        for (int f = 0; f < nfun; ++f) {
            const int k = P.korder[static_cast<std::size_t>(f)];
            const std::string fn = all_funs()[static_cast<std::size_t>(f)];
            orders += (f ? "; " : "") + fn + ": " + std::to_string(k);
            if (k >= 1) {
                orders += " (starts from " + fn;
                for (int j = 1; j < k; ++j) orders += ", " + fn + "_" + std::string(static_cast<std::size_t>(j), tn[0]);
                orders += ")";
            } else {
                orders += " (solved with the others at each step)";
            }
        }
        st.readouts.push_back({ tn + " derivatives", orders, std::string() });
    }
    std::string grid;
    std::string spacing;
    for (int a = 0; a < dims; ++a) {
        grid += (a ? " \xC3\x97 " : "") + std::to_string(s.n[a]);
        const double hh = s.periodic[a] ? (s.hi[a] - s.lo[a]) / s.n[a] : (s.hi[a] - s.lo[a]) / (s.n[a] - 1);
        spacing += ", \xCE\x94" + b.axis_names[static_cast<std::size_t>(a)] + " = " + number_text(hh);
    }
    st.readouts.push_back({ "grid", grid + " points" + spacing, std::string() });
    if (mode != Mode::Steady) {
        const int frames = std::max(2, P.frames);
        const int per_frame = std::max(1, (P.steps + frames - 2) / (frames - 1));
        const double dt = (P.t1 - P.t0) / (per_frame * (frames - 1));
        st.readouts.push_back({ "time steps", std::to_string(s.steps) + " implicit steps of " + method_name(P.method) + ", \xCE\x94" + tn + " = " + number_text(dt), std::string() });
        st.readouts.push_back({ "solver", (s.linear ? std::string("linear: one solve per step") : "Newton: " + number_text(s.steps > 0 ? static_cast<double>(s.newton) / s.steps : 0.0) + " iterations per step, at most " + std::to_string(s.newton_max)) + ", " + s.solver + (s.linear_iterations > 0 ? " (" + std::to_string(s.linear_iterations) + " GMRES iterations)" : std::string()), std::string() });
    } else {
        st.readouts.push_back({ "solver", (s.linear ? std::string("linear, ") : "Newton: " + std::to_string(s.newton) + " iterations, ") + s.solver + ", largest residual " + number_text(s.residual), std::string() });
    }
    std::string note;
    if (mode == Mode::Evolution && dims == 1) note = "x runs left to right, t runs back along the y axis, and the height is u(x, t). The bright line is u at t\xE2\x82\x80.";
    else if (mode == Mode::March && dims == 1) note = "x runs left to right, t runs back along the y axis, and the height is u(x, t). The bright line is u at x\xE2\x82\x80.";
    else if (mode == Mode::Evolution && dims == 2) note = "The surface is u(x, y, t\xE2\x82\x80); move t\xE2\x82\x80 to watch it change.";
    else if (mode == Mode::March && dims == 2) note = "The surface is u(x\xE2\x82\x80, t, y) over t (left to right) and y; move x\xE2\x82\x80 to march along x.";
    else if (dims == 3) note = "Level surfaces and colored slices of u at " + std::string(mode == Mode::Steady ? "the steady state" : "t\xE2\x82\x80") + ".";
    for (const std::string& n : b.notes) note += (note.empty() ? "" : " ") + std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(n[0])))) + n.substr(1) + ".";
    st.note = note;
    if (s.blew_up) st.error = "the solution blew up near " + tn + " = " + number_text(s.stopped) + "; this equation may be unstable (like heat flowing backward), or it needs a finer grid or more steps";
    else if (!s.problem.empty()) st.error = s.problem + " near " + tn + " = " + number_text(s.stopped) + "; try more time steps or a smaller range";
    else if (s.timed_out) st.error = "this was stopped at " + tn + " = " + number_text(s.stopped) + " after 20 seconds; try fewer grid points or time steps";
}

} // namespace

void build_pde(const Item& item, Ctx& c, ItemStatus& st) {
    build_any(item, c, st, Mode::Evolution);
}

void build_pde_steady(const Item& item, Ctx& c, ItemStatus& st) {
    build_any(item, c, st, Mode::Steady);
}

void build_pde_march(const Item& item, Ctx& c, ItemStatus& st) {
    build_any(item, c, st, Mode::March);
}

} // namespace build
} // namespace arbor
