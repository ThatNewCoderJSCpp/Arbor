#include "build_common.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace arbor {
namespace build {

namespace {

const double k_nan = std::nan("");

bool is_var(const ExprPtr& e, const std::string& name) {
    return e && e->op == Op::Variable && e->text == name;
}

Rgba faded(const Ctx& c, Rgba col, double t) {
    return mix(col, c.env.theme.background, t);
}

void add_stroke(Ctx& c, std::vector<Vec2> pts, Rgba color, float width, bool dashed) {
    Stroke2 s;
    s.pts = std::move(pts);
    s.color = color;
    s.width = width;
    s.dashed = dashed;
    c.scene.s2.strokes.push_back(std::move(s));
}

std::string derivative_name(int k) {
    return k == 0 ? std::string("y") : "yd" + std::to_string(k);
}

std::string expand_primes(const std::string& text, int& order) {
    order = 0;
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        bool word_before = false;
        for (std::size_t b = i; b > 0; --b) {
            const unsigned char pc = static_cast<unsigned char>(text[b - 1]);
            if (!(std::isalnum(pc) || pc == '_')) break;
            word_before = std::isalpha(pc) || pc == '_';
        }
        if (ch == 'y' && !word_before) {
            std::size_t j = i + 1;
            int primes = 0;
            while (j < text.size() && (text[j] == '\'' || (static_cast<unsigned char>(text[j]) == 0xE2 && j + 2 < text.size() && static_cast<unsigned char>(text[j + 1]) == 0x80 && (static_cast<unsigned char>(text[j + 2]) == 0xB2 || static_cast<unsigned char>(text[j + 2]) == 0xB3)))) {
                if (text[j] == '\'') {
                    ++primes;
                    ++j;
                } else {
                    primes += static_cast<unsigned char>(text[j + 2]) == 0xB2 ? 1 : 2;
                    j += 3;
                }
            }
            if (primes == 0 && j + 2 < text.size() && text[j] == '^' && text[j + 1] == '(') {
                std::size_t k = j + 2;
                int n = 0;
                while (k < text.size() && std::isdigit(static_cast<unsigned char>(text[k]))) n = n * 10 + (text[k++] - '0');
                if (k < text.size() && text[k] == ')' && k > j + 2) {
                    primes = n;
                    j = k + 1;
                }
            }
            const bool word_after = j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_');
            if (primes > 0 || !word_after) {
                out += derivative_name(primes);
                order = std::max(order, primes);
                i = j - 1;
                continue;
            }
        }
        out += ch;
    }
    return out;
}

std::vector<std::string> split_list(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char ch : text) {
        if (ch == '(' || ch == '[' || ch == '<') ++depth;
        if (ch == ')' || ch == ']' || ch == '>') --depth;
        if ((ch == ',' || ch == ';') && depth == 0) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    if (!cur.empty() || !out.empty()) out.push_back(cur);
    return out;
}

ExprPtr condition_expr(const std::string& text, Env& env, const std::vector<std::string>& vars, std::string& err) {
    ParseOptions o = parse_options(env, vars);
    ParseResult r = parse_condition(text, o);
    if (!r.ok) {
        err = r.error;
        return nullptr;
    }
    return prepare(r.statement.lhs, env, vars, err);
}

ExprPtr value_expr(const std::string& text, Env& env, const std::vector<std::string>& vars, std::string& err) {
    ParseResult r = parse_field(text, env, vars);
    if (!r.ok) {
        err = r.error;
        return nullptr;
    }
    if (r.statement.relation != Relation::None) {
        err = "write a value here, not an equation";
        return nullptr;
    }
    return prepare(r.statement.lhs, env, vars, err);
}

} // namespace

void build_ode(const Item& item, Ctx& c, ItemStatus& st) {
    int order = 0;
    const std::string text = expand_primes(item.field("eq"), order);
    if (order < 1) {
        st.error = "write a differential equation with y\xE2\x80\xB2, y\xE2\x80\xB3, ... (or y^(n)), such as y'' = -y";
        return;
    }
    if (order > 9) {
        st.error = "Arbor solves equations up to 9th order";
        return;
    }
    std::vector<std::string> vars = { "x" };
    for (int k = 0; k <= order; ++k) vars.push_back(derivative_name(k));
    ParseResult r = parse_field(text, c.env, vars);
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    const Statement& s = r.statement;
    if (s.relation != Relation::Equal && s.relation != Relation::None) {
        st.error = "a differential equation needs =";
        return;
    }
    const std::string top = derivative_name(order);
    std::string err;
    bool explicit_form = false;
    ExprPtr raw;
    if (s.relation == Relation::Equal && is_var(s.lhs, top) && !depends_on(s.rhs, top)) {
        raw = s.rhs;
        explicit_form = true;
    } else if (s.relation == Relation::Equal && is_var(s.rhs, top) && !depends_on(s.lhs, top)) {
        raw = s.lhs;
        explicit_form = true;
    } else {
        raw = s.relation == Relation::None ? s.lhs : sub(s.lhs, s.rhs);
    }
    ExprPtr F = prepare(raw, c.env, vars, err);
    if (!F) {
        st.error = err;
        return;
    }
    std::string shown = text;
    for (int k = order; k >= 1; --k) {
        const std::string from = derivative_name(k);
        std::string to = "y";
        if (k == 2) to += "\xE2\x80\xB3";
        else if (k == 3) to += "\xE2\x80\xB4";
        else if (k == 1) to += "\xE2\x80\xB2";
        else to += "\xE2\x81\xBD" + std::string(k == 4 ? "\xE2\x81\xB4" : k == 5 ? "\xE2\x81\xB5" : k == 6 ? "\xE2\x81\xB6" : k == 7 ? "\xE2\x81\xB7" : k == 8 ? "\xE2\x81\xB8" : "\xE2\x81\xB9") + "\xE2\x81\xBE";
        std::size_t at = 0;
        while ((at = shown.find(from, at)) != std::string::npos) {
            shown.replace(at, from.size(), to);
            at += to.size();
        }
    }
    st.note = "Order " + std::to_string(order) + ": the starting values are y(x\xE2\x82\x80)" + (order > 1 ? std::string(" through the ") + std::to_string(order - 1) + (order == 2 ? "st" : order == 3 ? "nd" : order == 4 ? "rd" : "th") + " derivative" : std::string()) + ", in order." + (explicit_form ? std::string() : std::string(" The highest derivative is solved for numerically at each step."));
    std::vector<std::string> fvars(vars.begin(), vars.end() - (explicit_form ? 1 : 0));
    Func fn(F, fvars, false, NumberType::Hardware);
    if (!fn.ok()) {
        st.error = fn.error();
        return;
    }
    double x0 = 0.0;
    if (!item.field("x0").empty() && !parse_number(item.field("x0"), c.env, x0, err)) {
        st.error = "x\xE2\x82\x80: " + err;
        return;
    }
    std::vector<double> y0(static_cast<std::size_t>(order), 0.0);
    const std::vector<std::string> given = split_list(item.field("init"));
    for (std::size_t k = 0; k < given.size() && k < y0.size(); ++k) {
        std::string t = given[k];
        if (t.find_first_not_of(" \t") == std::string::npos) continue;
        if (!parse_number(t, c.env, y0[k], err)) {
            st.error = "initial value " + std::to_string(k + 1) + ": " + err;
            return;
        }
    }
    if (given.size() < y0.size()) st.note += " Missing starting values are 0.";
    double xmin = c.box.xmin;
    double xmax = c.box.xmax;
    if (!item.field("min").empty()) parse_number(item.field("min"), c.env, xmin, err);
    if (!item.field("max").empty()) parse_number(item.field("max"), c.env, xmax, err);
    if (!(xmax > xmin)) {
        st.error = "the x range is empty";
        return;
    }
    x0 = std::clamp(x0, xmin, xmax);
    const int n = order;
    std::vector<double> args(fvars.size());
    double last_top = 0.0;
    auto rhs = [&](double x, const std::vector<double>& y, bool& ok) -> double {
        args[0] = x;
        for (int k = 0; k < n; ++k) args[static_cast<std::size_t>(k) + 1] = y[static_cast<std::size_t>(k)];
        if (explicit_form) {
            std::vector<const double*> in;
            for (double& a : args) in.push_back(&a);
            double out = 0.0;
            fn.eval(1, in, &out);
            ok = std::isfinite(out);
            return out;
        }
        double p = last_top;
        bool converged = false;
        for (int it = 0; it < 40; ++it) {
            std::vector<double> a1 = args;
            a1.back() = p;
            std::vector<double> a2 = args;
            const double h = 1e-6 * std::max(1.0, std::fabs(p));
            a2.back() = p + h;
            std::vector<const double*> in1, in2;
            for (double& a : a1) in1.push_back(&a);
            for (double& a : a2) in2.push_back(&a);
            double g1 = 0.0, g2 = 0.0;
            fn.eval(1, in1, &g1);
            fn.eval(1, in2, &g2);
            const double dg = (g2 - g1) / h;
            if (!std::isfinite(g1) || !std::isfinite(dg) || dg == 0.0) {
                ok = false;
                return k_nan;
            }
            const double step = g1 / dg;
            p -= step;
            if (std::fabs(step) <= 1e-12 * std::max(1.0, std::fabs(p))) {
                converged = true;
                break;
            }
        }
        last_top = p;
        ok = converged && std::isfinite(p);
        return p;
    };
    const double span = xmax - xmin;
    const int steps = c.env.quality >= 3 ? 6000 : c.env.quality <= 1 ? 1500 : 3000;
    const double h = span / steps;
    std::map<double, std::vector<double>> path;
    path[x0] = y0;
    const double limit = 1e6 * std::max({ 1.0, c.box.ymax - c.box.ymin, c.box.xmax - c.box.xmin });
    bool blew_up = false;
    for (int dir : { 1, -1 }) {
        std::vector<double> y = y0;
        double x = x0;
        last_top = 0.0;
        const double hh = h * dir;
        while ((dir > 0 && x < xmax - 1e-12 * span) || (dir < 0 && x > xmin + 1e-12 * span)) {
            if (build_cancelled()) return;
            const double step = dir > 0 ? std::min(hh, xmax - x) : std::max(hh, xmin - x);
            bool ok = true;
            auto deriv = [&](double xx, const std::vector<double>& yy, std::vector<double>& out) {
                out.resize(static_cast<std::size_t>(n));
                for (int k = 0; k + 1 < n; ++k) out[static_cast<std::size_t>(k)] = yy[static_cast<std::size_t>(k) + 1];
                bool fine = true;
                out[static_cast<std::size_t>(n) - 1] = rhs(xx, yy, fine);
                ok = ok && fine;
            };
            std::vector<double> k1, k2, k3, k4, t(static_cast<std::size_t>(n));
            deriv(x, y, k1);
            for (int k = 0; k < n; ++k) t[static_cast<std::size_t>(k)] = y[static_cast<std::size_t>(k)] + 0.5 * step * k1[static_cast<std::size_t>(k)];
            deriv(x + 0.5 * step, t, k2);
            for (int k = 0; k < n; ++k) t[static_cast<std::size_t>(k)] = y[static_cast<std::size_t>(k)] + 0.5 * step * k2[static_cast<std::size_t>(k)];
            deriv(x + 0.5 * step, t, k3);
            for (int k = 0; k < n; ++k) t[static_cast<std::size_t>(k)] = y[static_cast<std::size_t>(k)] + step * k3[static_cast<std::size_t>(k)];
            deriv(x + step, t, k4);
            if (!ok) {
                blew_up = true;
                break;
            }
            for (int k = 0; k < n; ++k) {
                const std::size_t q = static_cast<std::size_t>(k);
                y[q] += step / 6.0 * (k1[q] + 2.0 * k2[q] + 2.0 * k3[q] + k4[q]);
            }
            x += step;
            bool runaway = false;
            for (double v : y) runaway = runaway || !std::isfinite(v) || std::fabs(v) > 1e12 * limit;
            if (runaway || std::fabs(y[0]) > limit) {
                blew_up = true;
                break;
            }
            path[x] = y;
        }
    }
    const Rgba col = item_color(item);
    std::vector<Vec2> pts;
    for (const auto& [x, y] : path) pts.push_back(Vec2{ x, y[0] });
    add_stroke(c, pts, col, 2.6f, false);
    c.scene.s2.dots.push_back(Dot2{ Vec2{ x0, y0[0] }, col, 5.0f, false, "" });
    if (item.option("derivatives")) {
        for (int k = 1; k < n && k <= 3; ++k) {
            std::vector<Vec2> dp;
            for (const auto& [x, y] : path) {
                const double v = y[static_cast<std::size_t>(k)];
                dp.push_back(Vec2{ x, std::fabs(v) <= limit ? v : k_nan });
            }
            add_stroke(c, dp, faded(c, col, 0.3 + 0.15 * k), 1.8f, true);
        }
    }
    if (item.option("phase") && n >= 2) {
        Inset2 in;
        in.title = "phase plane (y, y\xE2\x80\xB2)";
        in.xlabel = "y";
        for (const auto& [x, y] : path) {
            if (std::fabs(y[1]) <= limit) in.pts.push_back(Vec2{ y[0], y[1] });
        }
        in.color = col;
        c.scene.s2.insets.push_back(std::move(in));
    }
    if (blew_up) st.note += " The solution stops where it blows up or stops being defined.";
    st.mathtext = shown;
    double at = x0;
    if (parse_number(item.field("at"), c.env, at, err) && std::isfinite(at)) {
        offer_param(st, "x", xmin, xmax, at);
        auto it = path.lower_bound(at);
        if (it != path.begin() && (it == path.end() || std::fabs(std::prev(it)->first - at) < std::fabs(it->first - at))) --it;
        if (it != path.end()) {
            st.readouts.push_back({ "y(" + format_double(it->first, 5) + ")", format_double(it->second[0], 10) });
            for (int k = 1; k < n && k <= 3; ++k) {
                std::string name = "y";
                for (int p = 0; p < k; ++p) name += "\xE2\x80\xB2";
                st.readouts.push_back({ name + "(" + format_double(it->first, 5) + ")", format_double(it->second[static_cast<std::size_t>(k)], 10) });
            }
            c.scene.s2.dots.push_back(Dot2{ Vec2{ it->first, it->second[0] }, col, 4.0f, true, "" });
        }
    }
}

void build_piecewise(const Item& item, Ctx& c, ItemStatus& st) {
    Item copy = item;
    copy.kind = "function";
    std::string cond = item.field("eq");
    std::string then_text = item.field("then");
    std::string else_text = item.field("else");
    if (then_text.empty()) then_text = "0";
    std::string eq = "y = if(" + cond + ", " + then_text + (else_text.empty() ? std::string() : ", " + else_text) + ")";
    copy.fields["eq"] = eq;
    build_function(copy, c, st);
    st.mathtext = "y = " + then_text + " if " + cond + (else_text.empty() ? std::string() : ", otherwise " + else_text);
}

void build_sequence(const Item& item, Ctx& c, ItemStatus& st) {
    std::string err;
    const std::vector<std::string> vars = { "a", "k" };
    ExprPtr cond = condition_expr(item.field("eq"), c.env, vars, err);
    if (!cond) {
        st.error = "condition: " + err;
        return;
    }
    ExprPtr yes = value_expr(item.field("then"), c.env, vars, err);
    if (!yes) {
        st.error = "if true: " + err;
        return;
    }
    ExprPtr no = item.field("else").empty() ? yes : value_expr(item.field("else"), c.env, vars, err);
    if (!no) {
        st.error = "if false: " + err;
        return;
    }
    ExprPtr stop;
    if (!item.field("stop").empty()) {
        stop = condition_expr(item.field("stop"), c.env, vars, err);
        if (!stop) {
            st.error = "stop when: " + err;
            return;
        }
    }
    double a = 27.0;
    if (!parse_number(item.field("start"), c.env, a, err)) {
        st.error = "start: " + err;
        return;
    }
    double steps = 200.0;
    parse_number(item.field("steps"), c.env, steps, err);
    const long total = static_cast<long>(std::clamp(steps, 1.0, 200000.0));
    Func fc(cond, vars, false, NumberType::Hardware);
    Func fy(yes, vars, false, NumberType::Hardware);
    Func fnn(no, vars, false, NumberType::Hardware);
    Func fs = stop ? Func(stop, vars, false, NumberType::Hardware) : Func();
    std::vector<Vec2> pts;
    pts.push_back(Vec2{ 0.0, a });
    double biggest = a;
    long k = 0;
    long stopped = -1;
    for (; k < total; ++k) {
        if (k % 4096 == 0 && build_cancelled()) return;
        const double kk = static_cast<double>(k);
        if (stop && fs.at({ a, kk }) != 0.0 && k > 0) {
            stopped = k;
            break;
        }
        const double test = fc.at({ a, kk });
        if (!std::isfinite(test)) break;
        a = test != 0.0 ? fy.at({ a, kk }) : fnn.at({ a, kk });
        if (!std::isfinite(a)) break;
        biggest = std::max(biggest, a);
        pts.push_back(Vec2{ kk + 1.0, a });
    }
    const Rgba col = item_color(item);
    add_stroke(c, pts, faded(c, col, 0.35), 1.6f, false);
    if (pts.size() <= 400) {
        for (const Vec2& p : pts) c.scene.s2.dots.push_back(Dot2{ p, col, 3.0f, false, "" });
    } else {
        add_stroke(c, pts, col, 2.0f, false);
    }
    st.mathtext = "a_{k+1} = " + item.field("then") + " if " + item.field("eq") + (item.field("else").empty() ? std::string() : ", otherwise " + item.field("else"));
    st.readouts.push_back({ "terms", std::to_string(pts.size()) });
    if (stopped >= 0) st.readouts.push_back({ "stopped at step", std::to_string(stopped) });
    st.readouts.push_back({ "largest", format_double(biggest, 12) });
    st.readouts.push_back({ "last", format_double(pts.back().y, 12) });
    st.note = "a is the current term and k its step; the horizontal axis is k.";
}

void build_escape(const Item& item, Ctx& c, ItemStatus& st) {
    std::string err;
    const std::vector<std::string> vars = { "z", "c" };
    ParseResult r = parse_field(item.field("eq"), c.env, vars);
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    ExprPtr step = r.statement.relation == Relation::None ? r.statement.lhs : r.statement.rhs;
    step = prepare(step, c.env, vars, err);
    if (!step) {
        st.error = err;
        return;
    }
    ExprPtr keep = condition_expr(item.field("while").empty() ? std::string("|z| <= 2") : item.field("while"), c.env, vars, err);
    if (!keep) {
        st.error = "keep going while: " + err;
        return;
    }
    ExprPtr z0 = value_expr(item.field("z0").empty() ? std::string("0") : item.field("z0"), c.env, vars, err);
    if (!z0) {
        st.error = "z\xE2\x82\x80: " + err;
        return;
    }
    const bool julia = item.option("julia");
    ExprPtr cfix = value_expr(item.field("c").empty() ? std::string("-0.8 + 0.156i") : item.field("c"), c.env, {}, err);
    if (julia && !cfix) {
        st.error = "c: " + err;
        return;
    }
    double maxit = 120.0;
    parse_number(item.field("max"), c.env, maxit, err);
    const int iters = static_cast<int>(std::clamp(maxit, 2.0, 5000.0));
    Func fstep(step, vars, true, NumberType::Hardware);
    Func fkeep(keep, vars, true, NumberType::Hardware);
    Func fz0(z0, vars, true, NumberType::Hardware);
    if (!fstep.ok()) {
        st.error = fstep.error();
        return;
    }
    double cre = 0.0, cim = 0.0;
    if (julia) {
        Func fc(cfix, {}, true, NumberType::Hardware);
        fc.eval_complex(1, {}, {}, &cre, &cim);
    }
    const int px = c.env.quality >= 3 ? 1 : 2;
    Raster2 ras;
    ras.xmin = c.box.xmin;
    ras.xmax = c.box.xmax;
    ras.ymin = c.box.ymin;
    ras.ymax = c.box.ymax;
    ras.w = std::max(1, c.width / px);
    ras.h = std::max(1, c.height / px);
    const std::size_t n = static_cast<std::size_t>(ras.w) * ras.h;
    std::vector<double> pr(n), pi(n);
    for (int j = 0; j < ras.h; ++j) {
        for (int i = 0; i < ras.w; ++i) {
            pr[static_cast<std::size_t>(j) * ras.w + i] = ras.xmin + (ras.xmax - ras.xmin) * (i + 0.5) / ras.w;
            pi[static_cast<std::size_t>(j) * ras.w + i] = ras.ymax - (ras.ymax - ras.ymin) * (j + 0.5) / ras.h;
        }
    }
    std::vector<double> cr(n), ci(n), zr(n), zi(n);
    if (julia) {
        std::fill(cr.begin(), cr.end(), cre);
        std::fill(ci.begin(), ci.end(), cim);
        zr = pr;
        zi = pi;
    } else {
        cr = pr;
        ci = pi;
        fz0.eval_complex(n, { pr.data(), pr.data() }, { pi.data(), pi.data() }, zr.data(), zi.data());
    }
    std::vector<int> count(n, -1);
    std::vector<double> mod_at_escape(n, 0.0);
    std::vector<std::size_t> active(n);
    for (std::size_t k = 0; k < n; ++k) active[k] = k;
    std::vector<double> ar, ai, br, bi, outr, outi, kr, ki;
    for (int it = 0; it < iters && !active.empty(); ++it) {
        if (build_cancelled()) return;
        const std::size_t m = active.size();
        ar.resize(m);
        ai.resize(m);
        br.resize(m);
        bi.resize(m);
        for (std::size_t q = 0; q < m; ++q) {
            ar[q] = zr[active[q]];
            ai[q] = zi[active[q]];
            br[q] = cr[active[q]];
            bi[q] = ci[active[q]];
        }
        kr.resize(m);
        ki.resize(m);
        fkeep.eval_complex(m, { ar.data(), br.data() }, { ai.data(), bi.data() }, kr.data(), ki.data());
        std::vector<std::size_t> still;
        still.reserve(m);
        for (std::size_t q = 0; q < m; ++q) {
            if (std::isfinite(kr[q]) && kr[q] != 0.0) still.push_back(q);
            else {
                count[active[q]] = it;
                mod_at_escape[active[q]] = std::hypot(ar[q], ai[q]);
            }
        }
        const std::size_t s2 = still.size();
        std::vector<double> sr(s2), si(s2), tr(s2), ti(s2);
        for (std::size_t q = 0; q < s2; ++q) {
            sr[q] = ar[still[q]];
            si[q] = ai[still[q]];
            tr[q] = br[still[q]];
            ti[q] = bi[still[q]];
        }
        outr.resize(s2);
        outi.resize(s2);
        fstep.eval_complex(s2, { sr.data(), tr.data() }, { si.data(), ti.data() }, outr.data(), outi.data());
        std::vector<std::size_t> next;
        next.reserve(s2);
        for (std::size_t q = 0; q < s2; ++q) {
            const std::size_t idx = active[still[q]];
            zr[idx] = outr[q];
            zi[idx] = outi[q];
            next.push_back(idx);
        }
        active.swap(next);
    }
    ras.px.resize(n);
    const Rgba inside = c.env.theme.background;
    for (std::size_t k = 0; k < n; ++k) {
        if (count[k] < 0) {
            ras.px[k] = with_alpha(inside, 1.0);
            continue;
        }
        double smooth = count[k];
        if (mod_at_escape[k] > 1.0) smooth = count[k] + 1.0 - std::log(std::log(mod_at_escape[k]) + 1.0) / std::log(2.0);
        const double t = std::fmod(std::max(0.0, smooth) / 40.0, 1.0);
        ras.px[k] = colormap("turbo", 0.1 + 0.85 * (0.5 - 0.5 * std::cos(t * 6.283185307179586)));
    }
    c.scene.s2.rasters.push_back(std::move(ras));
    st.legend_map = "";
    st.mathtext = julia ? "z \xE2\x86\xA6 " + to_mathtext(step) + ", c = " + item.field("c") : "z \xE2\x86\xA6 " + to_mathtext(step) + ", c = the point";
    st.note = julia ? "Each point is a starting z; the color counts the steps before the condition fails. Dark points never escape." : "Each point is a value of c, starting from z\xE2\x82\x80; the color counts the steps before the condition fails. Dark points never escape.";
    st.readouts.push_back({ "steps at most", std::to_string(iters) });
}

void build_turmite(const Item& item, Ctx& c, ItemStatus& st) {
    std::string err;
    const std::vector<std::string> vars = { "cell", "k", "x", "y" };
    ExprPtr cond = condition_expr(item.field("eq").empty() ? std::string("cell = 0") : item.field("eq"), c.env, vars, err);
    if (!cond) {
        st.error = "condition: " + err;
        return;
    }
    auto parse_actions = [](const std::string& text, std::vector<std::string>& out, std::string& e) {
        std::string t;
        for (char ch : text) t += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        for (char& ch : t) {
            if (ch == ',' || ch == ';') ch = ' ';
        }
        std::istringstream in(t);
        std::string w;
        while (in >> w) {
            if (w == "R" || w == "RIGHT") out.push_back("R");
            else if (w == "L" || w == "LEFT") out.push_back("L");
            else if (w == "U" || w == "BACK" || w == "UTURN") out.push_back("U");
            else if (w == "N" || w == "STAY" || w == "STRAIGHT") out.push_back("N");
            else if (w == "F" || w == "FLIP") out.push_back("F");
            else if (w == "M" || w == "MOVE" || w == "FORWARD") out.push_back("M");
            else if (w.rfind("SET", 0) == 0 || w.rfind("S", 0) == 0) {
                const std::string num = w.substr(w.rfind("SET", 0) == 0 ? 3 : 1);
                if (num.empty() || num.find_first_not_of("0123456789") != std::string::npos) {
                    e = "unknown action '" + w + "' (use R, L, U, N, F, M or S1, S2, ...)";
                    return false;
                }
                out.push_back("S" + num);
            } else {
                e = "unknown action '" + w + "' (use R, L, U, N, F, M or S1, S2, ...)";
                return false;
            }
        }
        return true;
    };
    std::vector<std::string> yes, no;
    if (!parse_actions(item.field("then").empty() ? std::string("R F M") : item.field("then"), yes, err) || !parse_actions(item.field("else").empty() ? std::string("L F M") : item.field("else"), no, err)) {
        st.error = err;
        return;
    }
    double steps = 11000.0;
    parse_number(item.field("steps"), c.env, steps, err);
    const long total = static_cast<long>(std::clamp(steps, 0.0, 2000000.0));
    double size = 0.1;
    parse_number(item.field("cell"), c.env, size, err);
    size = std::max(1e-6, size);
    Func fc(cond, vars, false, NumberType::Hardware);
    std::map<std::pair<long, long>, int> grid;
    long x = 0, y = 0;
    int dx = 0, dy = 1;
    long k = 0;
    for (; k < total; ++k) {
        if (k % 65536 == 0 && build_cancelled()) return;
        const auto key = std::make_pair(x, y);
        const auto found = grid.find(key);
        const int cell = found == grid.end() ? 0 : found->second;
        const double v = fc.at({ static_cast<double>(cell), static_cast<double>(k), static_cast<double>(x), static_cast<double>(y) });
        if (!std::isfinite(v)) break;
        for (const std::string& a : v != 0.0 ? yes : no) {
            if (a == "R") {
                const int t = dx;
                dx = dy;
                dy = -t;
            } else if (a == "L") {
                const int t = dx;
                dx = -dy;
                dy = t;
            } else if (a == "U") {
                dx = -dx;
                dy = -dy;
            } else if (a == "F") {
                const int now = grid.count(key) ? grid[key] : 0;
                grid[key] = now == 0 ? 1 : 0;
            } else if (a[0] == 'S') {
                grid[key] = std::atoi(a.c_str() + 1);
            } else if (a == "M") {
                x += dx;
                y += dy;
            }
        }
        if (grid.size() > 4000000) break;
    }
    long lo_x = 0, hi_x = 0, lo_y = 0, hi_y = 0;
    long filled = 0;
    for (const auto& [p, v] : grid) {
        if (!v) continue;
        ++filled;
        lo_x = std::min(lo_x, p.first);
        hi_x = std::max(hi_x, p.first);
        lo_y = std::min(lo_y, p.second);
        hi_y = std::max(hi_y, p.second);
    }
    const Rgba col = item_color(item);
    const long w = hi_x - lo_x + 1;
    const long h = hi_y - lo_y + 1;
    if (filled > 0 && w * h <= 16000000) {
        Raster2 ras;
        ras.xmin = (lo_x - 0.5) * size;
        ras.xmax = (hi_x + 0.5) * size;
        ras.ymin = (lo_y - 0.5) * size;
        ras.ymax = (hi_y + 0.5) * size;
        ras.w = static_cast<int>(w);
        ras.h = static_cast<int>(h);
        ras.px.assign(static_cast<std::size_t>(w * h), Rgba{ 0, 0, 0, 0 });
        for (const auto& [p, v] : grid) {
            if (!v) continue;
            const std::size_t idx = static_cast<std::size_t>((hi_y - p.second) * w + (p.first - lo_x));
            ras.px[idx] = v == 1 ? col : colormap("turbo", std::fmod(v * 0.17, 1.0));
        }
        c.scene.s2.rasters.push_back(std::move(ras));
    }
    c.scene.s2.dots.push_back(Dot2{ Vec2{ x * size, y * size }, c.env.theme.accent, 4.0f, false, "" });
    st.mathtext = "if " + (item.field("eq").empty() ? std::string("cell = 0") : item.field("eq")) + ": " + (item.field("then").empty() ? std::string("R F M") : item.field("then")) + "; otherwise: " + (item.field("else").empty() ? std::string("L F M") : item.field("else"));
    st.readouts.push_back({ "steps taken", std::to_string(k) });
    st.readouts.push_back({ "cells set", std::to_string(filled) });
    st.readouts.push_back({ "ant at", "(" + std::to_string(x) + ", " + std::to_string(y) + ")" });
    st.note = "Actions: R/L turn right/left, U turn around, N no turn, F flip the cell (0 \xE2\x86\x94 1), S2 set it to 2, M move forward. The condition can use cell, k (the step), x and y.";
}

} // namespace build
} // namespace arbor

namespace arbor {
namespace build {

void build_rotation(const Item& item, Ctx& c, ItemStatus& st) {
    std::string err;
    PlaneCurve pc;
    if (!plane_curve(item.field("eq"), c.env, pc, err)) {
        st.error = err;
        return;
    }
    ParseResult ar = parse_field(item.field("angle").empty() ? std::string("pi/6") : item.field("angle"), c.env, {});
    if (!ar.ok) {
        st.error = "angle: " + ar.error;
        return;
    }
    ExprPtr A = prepare(ar.statement.relation == Relation::None ? ar.statement.lhs : ar.statement.rhs, c.env, {}, err);
    if (!A) {
        st.error = "angle: " + err;
        return;
    }
    if (c.env.degrees) A = mul(A, number(3.141592653589793 / 180.0));
    double angle = 0.0;
    {
        Func fa(A, {}, false, NumberType::Hardware);
        angle = fa.at({});
    }
    if (!std::isfinite(angle)) {
        st.error = "the angle isn't a number";
        return;
    }
    double cx = 0.0, cy = 0.0;
    if (!item.field("cx").empty() && !parse_number(item.field("cx"), c.env, cx, err)) {
        st.error = "center x: " + err;
        return;
    }
    if (!item.field("cy").empty() && !parse_number(item.field("cy"), c.env, cy, err)) {
        st.error = "center y: " + err;
        return;
    }
    const ExprPtr cosA = simplify(call("cos", { A }));
    const ExprPtr sinA = simplify(call("sin", { A }));
    const ExprPtr dx = sub(pc.x, number(cx));
    const ExprPtr dy = sub(pc.y, number(cy));
    const ExprPtr X2 = tidy(simplify(add(number(cx), sub(mul(dx, cosA), mul(dy, sinA)))));
    const ExprPtr Y2 = tidy(simplify(add(number(cy), add(mul(dx, sinA), mul(dy, cosA)))));
    double t0 = 0.0, t1 = 6.283185307179586;
    if (pc.kind == PlaneCurve::Kind::OfX) {
        t0 = c.box.xmin;
        t1 = c.box.xmax;
        const double pad = 0.5 * (t1 - t0);
        t0 -= pad;
        t1 += pad;
    } else if (pc.kind == PlaneCurve::Kind::OfY) {
        t0 = c.box.ymin;
        t1 = c.box.ymax;
        const double pad = 0.5 * (t1 - t0);
        t0 -= pad;
        t1 += pad;
    }
    if (!item.field("min").empty() && !parse_number(item.field("min"), c.env, t0, err)) {
        st.error = "from: " + err;
        return;
    }
    if (!item.field("max").empty() && !parse_number(item.field("max"), c.env, t1, err)) {
        st.error = "to: " + err;
        return;
    }
    if (!(t1 > t0)) {
        st.error = "the range is empty";
        return;
    }
    Func fx(pc.x, { "t" }, false, c.env.geo_type);
    Func fy(pc.y, { "t" }, false, c.env.geo_type);
    Func gx(X2, { "t" }, false, c.env.geo_type);
    Func gy(Y2, { "t" }, false, c.env.geo_type);
    if (!gx.ok() || !gy.ok()) {
        st.error = !gx.ok() ? gx.error() : gy.error();
        return;
    }
    auto trace = [&](Func& px, Func& py) {
        CurveSampler2 s;
        s.box = c.box;
        s.width = c.width;
        s.height = c.height;
        s.initial = std::max(300, c.width / 2);
        s.rounds = 6;
        s.eval = [&](const std::vector<double>& ts, std::vector<Vec2>& out) {
            const std::vector<double> a = px.map1(ts);
            const std::vector<double> b = py.map1(ts);
            out.resize(ts.size());
            for (std::size_t i = 0; i < ts.size(); ++i) out[i] = Vec2{ a[i], b[i] };
        };
        std::vector<double> ts;
        std::vector<Vec2> pts;
        sample_curve2(s, t0, t1, ts, pts);
        return pts;
    };
    const Rgba col = item_color(item);
    if (item.option("original")) add_stroke(c, trace(fx, fy), faded(c, col, 0.55), 1.8f, true);
    add_stroke(c, trace(gx, gy), col, 2.6f, false);
    c.scene.s2.dots.push_back(Dot2{ Vec2{ cx, cy }, c.env.theme.accent, 4.5f, false, "" });
    const double deg = angle * 180.0 / 3.141592653589793;
    st.mathtext = "\xE2\x9F\xA8" + to_mathtext(X2) + ", " + to_mathtext(Y2) + "\xE2\x9F\xA9";
    st.readouts.push_back({ "angle", number_text(angle) + " rad = " + number_text(deg) + "\xC2\xB0" });
    st.readouts.push_back(formula_vreadout("r(t)", { X2, Y2 }));
    if (pc.kind != PlaneCurve::Kind::Parametric && pc.original.lhs) {
        const ExprPtr xs = sub(variable("x"), number(cx));
        const ExprPtr ys = sub(variable("y"), number(cy));
        const ExprPtr back_x = add(number(cx), add(mul(xs, cosA), mul(ys, sinA)));
        const ExprPtr back_y = add(number(cy), sub(mul(ys, cosA), mul(xs, sinA)));
        const std::map<std::string, ExprPtr> sub_map = { { "x", back_x }, { "y", back_y } };
        const ExprPtr lhs = tidy(simplify(substitute(pc.original.lhs, sub_map)));
        const ExprPtr rhs = pc.original.rhs ? tidy(simplify(substitute(pc.original.rhs, sub_map))) : number(0.0);
        {
            const std::string ls = symbol_text(lhs);
            const std::string rs = symbol_text(rhs);
            if (!ls.empty() && !rs.empty()) st.readouts.push_back(formula_readout("equation", ls + " = " + rs));
            else st.readouts.push_back({ "equation", to_string(lhs) + " = " + to_string(rhs), std::string() });
        }
    }
    st.note = "t runs over the original curve's variable; the rotated point is the original point turned about the center (the dot) by the angle, counterclockwise.";
}

namespace {

struct RaceCurve {
    std::vector<double> xs;
    std::vector<double> ys;
    std::vector<double> ts;
    bool ok = false;
    std::string problem;
};

RaceCurve race_curve(Func& f, double x0, double x1, double g) {
    RaceCurve out;
    const int n = 4000;
    const double start = f.at({ x0 });
    if (!std::isfinite(start)) {
        out.problem = "the curve isn't defined at the start";
        return out;
    }
    double t = 0.0;
    double px = x0;
    double py = start;
    out.xs.push_back(px);
    out.ys.push_back(py);
    out.ts.push_back(0.0);
    for (int i = 1; i <= n; ++i) {
        const double s = static_cast<double>(i) / n;
        const double x = x0 + (x1 - x0) * s * s;
        const double y = f.at({ x });
        if (!std::isfinite(y)) {
            out.problem = "the curve isn't defined everywhere between the ends";
            return out;
        }
        const double ym = f.at({ 0.5 * (x + px) });
        const double drop = start - (std::isfinite(ym) ? ym : 0.5 * (y + py));
        if (drop <= 0.0) {
            if (i == 1 && drop == 0.0) {
                out.problem = "the curve starts flat, so a bead resting at the start never moves";
                return out;
            }
            out.problem = "the curve climbs above its start, so the bead can't get over it";
            return out;
        }
        const double ds = std::hypot(x - px, y - py);
        t += ds / std::sqrt(2.0 * g * drop);
        out.xs.push_back(x);
        out.ys.push_back(y);
        out.ts.push_back(t);
        px = x;
        py = y;
    }
    out.ok = true;
    return out;
}

Vec2 race_position(const RaceCurve& r, double t) {
    if (r.ts.empty()) return Vec2{ k_nan, k_nan };
    if (t >= r.ts.back()) return Vec2{ r.xs.back(), r.ys.back() };
    const auto it = std::upper_bound(r.ts.begin(), r.ts.end(), t);
    const std::size_t i = static_cast<std::size_t>(std::max<std::ptrdiff_t>(1, it - r.ts.begin()));
    const double a = (t - r.ts[i - 1]) / std::max(1e-300, r.ts[i] - r.ts[i - 1]);
    return Vec2{ r.xs[i - 1] + a * (r.xs[i] - r.xs[i - 1]), r.ys[i - 1] + a * (r.ys[i] - r.ys[i - 1]) };
}

} // namespace

void build_brachistochrone(const Item& item, Ctx& c, ItemStatus& st) {
    std::string err;
    double x0 = 0.0, y0 = 0.0, x1 = 3.141592653589793, y1 = -2.0, g = 9.81;
    const char* names[5] = { "x0", "y0", "x1", "y1", "g" };
    double* slots[5] = { &x0, &y0, &x1, &y1, &g };
    const char* labels[5] = { "from x", "from y", "to x", "to y", "g" };
    for (int k = 0; k < 5; ++k) {
        if (item.field(names[k]).empty()) continue;
        if (!parse_number(item.field(names[k]), c.env, *slots[k], err)) {
            st.error = std::string(labels[k]) + ": " + err;
            return;
        }
    }
    if (!(g > 0.0)) {
        st.error = "g has to be positive";
        return;
    }
    const double dy = y0 - y1;
    const double dx = std::fabs(x1 - x0);
    const double side = x1 >= x0 ? 1.0 : -1.0;
    if (!(dy > 0.0)) {
        st.error = "the end has to be below the start: a bead starting at rest can't climb";
        return;
    }
    if (dx < 1e-12) {
        st.error = "the end is straight below the start, so the fastest path is just falling";
        return;
    }
    const double ratio = dx / dy;
    double lo = 1e-9, hi = 6.283185307179586 - 1e-9;
    for (int it = 0; it < 200; ++it) {
        const double mid = 0.5 * (lo + hi);
        const double f = (mid - std::sin(mid)) / (1.0 - std::cos(mid));
        if (f < ratio) lo = mid;
        else hi = mid;
    }
    const double th = 0.5 * (lo + hi);
    const double R = dy / (1.0 - std::cos(th));
    const double t_cycloid = th * std::sqrt(R / g);
    const double L = std::hypot(dx, dy);
    const double t_line = L * std::sqrt(2.0 / (g * dy));

    const Rgba col = item_color(item);
    auto near = [&](const Rgba& o) { return std::abs(col.r - o.r) + std::abs(col.g - o.g) + std::abs(col.b - o.b) < 150; };
    const Rgba line_col = near(rgba(232, 160, 60)) ? rgba(150, 150, 160) : rgba(232, 160, 60);
    const Rgba race_col = near(rgba(63, 166, 90)) ? rgba(90, 140, 230) : rgba(63, 166, 90);
    auto cycloid_at = [&](double a) { return Vec2{ x0 + side * R * (a - std::sin(a)), y0 - R * (1.0 - std::cos(a)) }; };
    {
        std::vector<Vec2> pts;
        const int n = 400;
        for (int i = 0; i <= n; ++i) pts.push_back(cycloid_at(th * i / n));
        add_stroke(c, pts, col, 3.0f, false);
    }
    if (item.option("whole")) {
        std::vector<Vec2> pts;
        for (int i = 0; i <= 400; ++i) pts.push_back(cycloid_at(6.283185307179586 * i / 400.0));
        add_stroke(c, pts, faded(c, col, 0.55), 1.6f, true);
    }
    if (item.option("line")) add_stroke(c, { Vec2{ x0, y0 }, Vec2{ x1, y1 } }, line_col, 2.2f, false);

    RaceCurve race;
    ExprPtr race_expr;
    if (item.option("curve") && !item.field("eq").empty()) {
        ParseResult r = parse_field(item.field("eq"), c.env, { "x" });
        if (!r.ok) {
            st.error = "the other curve: " + r.error;
            return;
        }
        ExprPtr raw = r.statement.relation == Relation::None ? r.statement.lhs : is_var(r.statement.lhs, "y") ? r.statement.rhs : is_var(r.statement.rhs, "y") ? r.statement.lhs : nullptr;
        if (!raw || depends_on(raw, "y")) {
            st.error = "write the other curve as y = f(x)";
            return;
        }
        race_expr = prepare(raw, c.env, { "x" }, err);
        if (!race_expr) {
            st.error = "the other curve: " + err;
            return;
        }
        Func f(race_expr, { "x" }, false, NumberType::Hardware);
        if (!f.ok()) {
            st.error = "the other curve: " + f.error();
            return;
        }
        race = race_curve(f, x0, x1, g);
        std::vector<Vec2> pts;
        for (int i = 0; i <= 400; ++i) {
            const double x = x0 + (x1 - x0) * i / 400.0;
            pts.push_back(Vec2{ x, f.at({ x }) });
        }
        add_stroke(c, pts, race_col, 2.2f, false);
    }

    c.scene.s2.dots.push_back(Dot2{ Vec2{ x0, y0 }, c.env.theme.text, 5.0f, false, "A" });
    c.scene.s2.dots.push_back(Dot2{ Vec2{ x1, y1 }, c.env.theme.text, 5.0f, false, "B" });

    double longest = std::max(t_cycloid, item.option("line") ? t_line : 0.0);
    if (race.ok) longest = std::max(longest, race.ts.back());
    double t = 0.0;
    if (!item.field("at").empty() && !parse_number(item.field("at"), c.env, t, err)) {
        st.error = "t: " + err;
        return;
    }
    offer_param(st, "t", 0.0, longest * 1.05, t);
    if (item.option("beads")) {
        const double a = std::min(th, std::max(0.0, t) * std::sqrt(g / R));
        const Vec2 p = cycloid_at(a);
        c.scene.s2.dots.push_back(Dot2{ p, col, 7.0f, false, "" });
        if (item.option("circle")) {
            const Vec2 center{ x0 + side * R * a, y0 - R };
            std::vector<Vec2> ring;
            for (int i = 0; i <= 120; ++i) ring.push_back(Vec2{ center.x + R * std::cos(6.283185307179586 * i / 120.0), center.y + R * std::sin(6.283185307179586 * i / 120.0) });
            add_stroke(c, ring, faded(c, col, 0.35), 1.6f, false);
            add_stroke(c, { center, p }, faded(c, col, 0.35), 1.4f, false);
            add_stroke(c, { Vec2{ center.x - 3.0 * R, y0 }, Vec2{ center.x + 3.0 * R, y0 } }, faded(c, c.env.theme.text, 0.6), 1.2f, true);
        }
        if (item.option("line")) {
            const double s = std::min(L, 0.5 * g * (dy / L) * t * t);
            c.scene.s2.dots.push_back(Dot2{ Vec2{ x0 + (x1 - x0) * s / L, y0 - dy * s / L }, line_col, 7.0f, false, "" });
        }
        if (race.ok) c.scene.s2.dots.push_back(Dot2{ race_position(race, std::max(0.0, t)), race_col, 7.0f, false, "" });
    }

    const std::string sx = side > 0 ? " + " : " - ";
    st.mathtext = "x = " + number_text(x0) + sx + number_text(R) + "(\\theta\\ - sin \\theta\\), y = " + number_text(y0) + " - " + number_text(R) + "(1 - cos \\theta\\)";
    st.readouts.push_back({ "cycloid", "R = " + number_text(R) + ", \xCE\xB8 from 0 to " + number_text(th), "x(\\theta\\) = x_{0} " + sx + " R(\\theta\\ - sin \\theta\\), y(\\theta\\) = y_{0} - R(1 - cos \\theta\\)" });
    st.readouts.push_back({ "time on the cycloid", number_text(t_cycloid), "T = \\theta\\_{B} sqrt(R/g)" });
    if (item.option("line")) st.readouts.push_back({ "time on the straight line", number_text(t_line), "T = L sqrt(2/(g \\Delta\\y))" });
    if (race_expr) {
        if (race.ok) st.readouts.push_back({ "time on y = " + to_string(race_expr), number_text(race.ts.back()), "T = \\int\\ sqrt(1 + y\xE2\x80\xB2^{2})/sqrt(2g(y_{0} - y)) dx" });
        else st.readouts.push_back({ "time on y = " + to_string(race_expr), race.problem, std::string() });
    }
    st.note = "A bead slides from rest at A to B with gravity g pulling down and no friction. The cycloid (Bernoulli's brachistochrone) is the fastest path, faster than the straight line even though it's longer. Started anywhere on the cycloid, a bead also reaches its lowest point in the same time (the tautochrone). Move t to race the beads.";
}

} // namespace build
} // namespace arbor
