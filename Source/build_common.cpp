#include "build_common.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>

namespace arbor {
namespace build {

namespace {

const double k_nan = std::nan("");

thread_local const BuildControl* t_control = nullptr;

thread_local std::chrono::steady_clock::time_point t_deadline = std::chrono::steady_clock::time_point::max();
thread_local bool t_timed_out = false;
thread_local bool t_symbols = true;

bool stop_now() {
    if (t_control && t_control->cancelled()) return true;
    if (t_timed_out) return true;
    if (t_deadline != std::chrono::steady_clock::time_point::max() && std::chrono::steady_clock::now() > t_deadline) t_timed_out = true;
    return t_timed_out;
}

ExprPtr to_degrees(const ExprPtr& e) {
    if (!e || e->args.empty()) return e;
    std::vector<ExprPtr> args;
    for (const ExprPtr& a : e->args) args.push_back(to_degrees(a));
    auto rebuilt = std::make_shared<Expr>(*e);
    rebuilt->args = args;
    if (e->op != Op::Call) return simplify(rebuilt);
    static const char* forward[] = { "sin", "cos", "tan", "sec", "csc", "cot" };
    static const char* inverse[] = { "asin", "acos", "atan", "asec", "acsc", "acot", "atan2", "arg" };
    const ExprPtr per = div(variable("pi"), number(180.0));
    for (const char* f : forward) {
        if (e->text == f && args.size() == 1) return call(e->text, { mul(args[0], per) });
    }
    for (const char* f : inverse) {
        if (e->text == f) return div(call(e->text, args), per);
    }
    return simplify(rebuilt);
}

} // namespace

void set_thread_control(const BuildControl* control) {
    t_control = control;
}

void set_thread_symbols(bool on) {
    t_symbols = on;
}

bool symbols_wanted() {
    return t_symbols;
}

bool build_cancelled() {
    return stop_now();
}

void set_item_deadline(double seconds) {
    t_timed_out = false;
    t_deadline = seconds > 0.0 ? std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0)) : std::chrono::steady_clock::time_point::max();
}

bool item_timed_out() {
    return t_timed_out;
}

ParseOptions parse_options(const Env& env, const std::vector<std::string>& vars) {
    ParseOptions o;
    for (const std::string& v : vars) o.known_names.insert(v);
    for (const std::string& n : env.defined) o.known_names.insert(n);
    for (const auto& [name, _] : env.numbers) o.known_names.insert(name);
    for (const auto& [name, f] : env.functions) {
        if (!f.params.empty()) o.function_names.insert(name);
        else o.known_names.insert(name);
    }
    return o;
}

ParseResult parse_field(const std::string& text, const Env& env, const std::vector<std::string>& vars) {
    return parse_statement(text, parse_options(env, vars));
}

ExprPtr prepare(const ExprPtr& e, Env& env, const std::vector<std::string>& vars, std::string& error) {
    if (!e) {
        error = "missing expression";
        return nullptr;
    }
    std::string err;
    ExprPtr x = inline_functions(e, env.functions, err);
    if (!err.empty()) {
        error = err;
        return nullptr;
    }
    if (!vars.empty()) {
        std::map<std::string, ExprPtr> numbers = env.numbers;
        for (const std::string& v : vars) numbers.erase(v);
        x = substitute(x, numbers);
    } else {
        x = substitute(x, env.numbers);
    }
    if (env.degrees) x = to_degrees(x);
    std::set<std::string> names;
    collect_names(x, names);
    std::map<std::string, ExprPtr> missing;
    for (const std::string& n : names) {
        if (std::find(vars.begin(), vars.end(), n) != vars.end()) continue;
        env.wanted.insert(n);
        missing[n] = number(1.0);
    }
    if (!missing.empty()) x = substitute(x, missing);
    return simplify(x);
}

bool parse_number(const std::string& text, Env& env, double& out, std::string& error) {
    std::string t = text;
    if (t.find_first_not_of(" \t") == std::string::npos) {
        error = "empty number";
        return false;
    }
    ParseResult r = parse_expression(t, parse_options(env, {}));
    if (!r.ok) {
        error = r.error;
        return false;
    }
    ExprPtr e = prepare(r.statement.lhs, env, {}, error);
    if (!e) return false;
    std::string err;
    Program p = compile(e, {}, false, err);
    if (!p.valid) {
        error = err;
        return false;
    }
    out = make_hardware_evaluator(p)->real1({});
    if (!std::isfinite(out)) {
        error = "'" + text + "' isn't a finite number";
        return false;
    }
    return true;
}

Func::Func(const ExprPtr& e, const std::vector<std::string>& vars, bool complex, NumberType type) : m_expr(e), m_vars(vars) {
    if (!e) {
        m_error = "missing expression";
        return;
    }
    m_program = compile(e, vars, complex, m_error);
    if (!m_program.valid) return;
    m_eval = std::shared_ptr<Evaluator>(make_evaluator(m_program, type).release());
    m_ok = true;
}

void Func::eval(std::size_t n, const std::vector<const double*>& in, double* out) {
    if (!m_ok) {
        for (std::size_t i = 0; i < n; ++i) out[i] = k_nan;
        return;
    }
    const std::size_t chunk = 1024;
    std::vector<const double*> ptrs(in.size());
    for (std::size_t start = 0; start < n; start += chunk) {
        const std::size_t len = std::min(chunk, n - start);
        if (stop_now()) {
            for (std::size_t i = start; i < n; ++i) out[i] = k_nan;
            return;
        }
        for (std::size_t k = 0; k < in.size(); ++k) ptrs[k] = in[k] + start;
        m_eval->real(len, ptrs.data(), out + start);
    }
}

void Func::eval_complex(std::size_t n, const std::vector<const double*>& re, const std::vector<const double*>& im, double* out_re, double* out_im) {
    if (!m_ok) {
        for (std::size_t i = 0; i < n; ++i) out_re[i] = out_im[i] = k_nan;
        return;
    }
    const std::size_t chunk = 1024;
    std::vector<const double*> pr(re.size());
    std::vector<const double*> pi(im.size());
    for (std::size_t start = 0; start < n; start += chunk) {
        const std::size_t len = std::min(chunk, n - start);
        if (stop_now()) {
            for (std::size_t i = start; i < n; ++i) out_re[i] = out_im[i] = k_nan;
            return;
        }
        for (std::size_t k = 0; k < re.size(); ++k) pr[k] = re[k] + start;
        for (std::size_t k = 0; k < im.size(); ++k) pi[k] = im[k] + start;
        m_eval->complex(len, pr.data(), im.empty() ? nullptr : pi.data(), out_re + start, out_im + start);
    }
}

double Func::at(std::initializer_list<double> in) {
    if (!m_ok) return k_nan;
    return m_eval->real1(in);
}

std::vector<double> Func::map1(const std::vector<double>& xs) {
    std::vector<double> out(xs.size());
    eval(xs.size(), { xs.data() }, out.data());
    return out;
}

std::vector<double> Func::map2(const std::vector<double>& xs, const std::vector<double>& ys) {
    std::vector<double> out(xs.size());
    eval(xs.size(), { xs.data(), ys.data() }, out.data());
    return out;
}

std::vector<double> Func::map3(const std::vector<double>& xs, const std::vector<double>& ys, const std::vector<double>& zs) {
    std::vector<double> out(xs.size());
    eval(xs.size(), { xs.data(), ys.data(), zs.data() }, out.data());
    return out;
}

std::string number_text(double v) {
    return format_double(v, 6);
}

std::string vec_text(const std::vector<double>& v) {
    std::string s = "(";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) s += ", ";
        s += format_double(v[i], 5);
    }
    return s + ")";
}

std::string value_text(const ExprPtr& e, const std::vector<std::string>& vars, const std::vector<double>& at, Env& env) {
    if (!e) return "undefined";
    std::string err;
    Program p = compile(e, vars, false, err);
    if (!p.valid) return "?";
    auto ev = make_evaluator(p, env.value_type);
    const int digits = env.value_type == NumberType::Hardware ? 10 : number_type_digits(env.value_type);
    return ev->text(at, digits);
}

namespace {

bool numeric_derivative(const ExprPtr& e) {
    if (!e) return false;
    if (e->op == Op::Number && e->value == 1e-6) return true;
    for (const ExprPtr& a : e->args) {
        if (numeric_derivative(a)) return true;
    }
    return false;
}

} // namespace

namespace {

bool plain_number(const ExprPtr& e, double& v) {
    if (!e) return false;
    if (e->op == Op::Number && (e->text.empty() || e->text.find('@') == std::string::npos)) {
        v = e->value;
        return std::isfinite(v);
    }
    if (e->op == Op::Neg && plain_number(e->args[0], v)) {
        v = -v;
        return true;
    }
    return false;
}

bool whole(double v) {
    return std::fabs(v) < 1e12 && std::floor(v) == v;
}

ExprPtr fold_decimals(const ExprPtr& e) {
    if (!e || e->args.empty()) return e;
    std::vector<ExprPtr> args;
    bool changed = false;
    for (const ExprPtr& a : e->args) {
        args.push_back(fold_decimals(a));
        changed = changed || args.back() != a;
    }
    ExprPtr r = e;
    if (changed) {
        auto copy = std::make_shared<Expr>(*e);
        copy->args = args;
        r = copy;
    }
    std::vector<double> v(args.size());
    bool numeric = true;
    bool all_whole = true;
    for (std::size_t i = 0; i < args.size(); ++i) {
        numeric = numeric && plain_number(args[i], v[i]);
        all_whole = all_whole && numeric && whole(v[i]);
    }
    if (!numeric || all_whole) return r;
    double out = std::nan("");
    switch (r->op) {
        case Op::Add: out = v[0] + v[1]; break;
        case Op::Sub: out = v[0] - v[1]; break;
        case Op::Mul: out = v[0] * v[1]; break;
        case Op::Div: out = v[1] != 0.0 ? v[0] / v[1] : std::nan(""); break;
        case Op::Pow: out = std::pow(v[0], v[1]); break;
        case Op::Call:
            if (r->text == "sqrt" && v[0] >= 0.0) out = std::sqrt(v[0]);
            break;
        default: break;
    }
    if (!std::isfinite(out)) return r;
    const double rounded = std::stod(format_double(out, 12));
    return rounded < 0 ? neg(number(-rounded)) : number(rounded);
}

} // namespace

std::string symbol_text(const ExprPtr& e) {
    if (!t_symbols || !e || numeric_derivative(e)) return std::string();
    const ExprPtr plain = simplify(e);
    ExprPtr shown = tidy(plain);
    if (shown != plain) {
        std::set<std::string> names;
        collect_names(plain, names);
        std::vector<std::string> vars;
        for (const std::string& n : names) {
            if (!is_builtin_constant(n)) vars.push_back(n);
        }
        std::string e1, e2;
        Program pa = compile(plain, vars, false, e1);
        Program pb = compile(shown, vars, false, e2);
        bool same = pa.valid && pb.valid;
        if (same) {
            auto ea = make_hardware_evaluator(pa);
            auto eb = make_hardware_evaluator(pb);
            const double probes[4] = { 0.7310585786, 1.9318516526, -0.4142135624, 2.6457513111 };
            for (int k = 0; k < 4 && same; ++k) {
                std::vector<double> at(vars.size());
                for (std::size_t i = 0; i < at.size(); ++i) at[i] = probes[(k + i) % 4] + 0.37 * static_cast<double>(i);
                std::vector<const double*> ptrs;
                for (const double& v : at) ptrs.push_back(&v);
                double va = 0.0;
                double vb = 0.0;
                ea->real(1, ptrs.data(), &va);
                eb->real(1, ptrs.data(), &vb);
                if (std::isfinite(va) != std::isfinite(vb)) same = false;
                else if (std::isfinite(va) && std::fabs(va - vb) > 1e-8 * std::max(1.0, std::fabs(va))) same = false;
            }
        }
        if (!same) shown = plain;
    }
    std::string s = to_mathtext(simplify(fold_decimals(shown)));
    if (s.size() > 900) return std::string();
    return s;
}

std::string symbol_vec(const std::vector<ExprPtr>& es) {
    std::string s = "(";
    for (std::size_t i = 0; i < es.size(); ++i) {
        const std::string part = symbol_text(es[i]);
        if (part.empty()) return std::string();
        if (i) s += ", ";
        s += part;
    }
    s += ")";
    return s.size() > 1400 ? std::string() : s;
}

Readout sym_readout(const std::string& label, const ExprPtr& e, const std::vector<std::string>& vars, const std::vector<double>& at, Env& env) {
    return Readout{ label, value_text(e, vars, at, env), symbol_text(e) };
}

Readout sym_vreadout(const std::string& label, const std::vector<ExprPtr>& es, const std::vector<std::string>& vars, const std::vector<double>& at, Env& env) {
    std::string v = "(";
    for (std::size_t i = 0; i < es.size(); ++i) {
        if (i) v += ", ";
        v += value_text(es[i], vars, at, env);
    }
    return Readout{ label, v + ")", symbol_vec(es) };
}

Readout formula_readout(const std::string& label, const ExprPtr& e) {
    if (!t_symbols) return Readout{ label, std::string(), std::string() };
    const std::string s = symbol_text(e);
    return Readout{ label, s.empty() ? to_string(e) : std::string(), s };
}

Readout formula_readout(const std::string& label, const std::string& symbol) {
    return Readout{ label, std::string(), symbol };
}

Readout formula_vreadout(const std::string& label, const std::vector<ExprPtr>& es) {
    if (!t_symbols) return Readout{ label, std::string(), std::string() };
    const std::string s = symbol_vec(es);
    if (!s.empty()) return Readout{ label, std::string(), s };
    std::string v = "(";
    for (std::size_t i = 0; i < es.size(); ++i) v += (i ? ", " : "") + to_string(es[i]);
    return Readout{ label, v + ")", std::string() };
}

std::string complex_value_text(const ExprPtr& e, const std::vector<std::string>& vars, const std::vector<double>& re, const std::vector<double>& im, Env& env) {
    if (!e) return "undefined";
    std::string err;
    Program p = compile(e, vars, true, err);
    if (!p.valid) return "?";
    auto ev = make_evaluator(p, env.value_type);
    const int digits = env.value_type == NumberType::Hardware ? 8 : number_type_digits(env.value_type);
    return ev->complex_text(re, im, digits);
}

ExprPtr d(const ExprPtr& e, const std::string& var) {
    ExprPtr r = derivative(e, var);
    return r ? tidy(simplify(r)) : nullptr;
}

Rgba item_color(const Item& item) {
    Rgba c;
    if (!parse_color(item.color, c)) c = rgba(79, 143, 230);
    c.a = 255;
    return c;
}

Rgba soft(Rgba c, double alpha) {
    return with_alpha(c, alpha);
}

std::vector<double> linspace(double a, double b, std::size_t n) {
    std::vector<double> out(n);
    if (n == 1) {
        out[0] = a;
        return out;
    }
    for (std::size_t i = 0; i < n; ++i) out[i] = a + (b - a) * static_cast<double>(i) / static_cast<double>(n - 1);
    return out;
}

namespace {

Vec2 to_px(const WorldBox2& box, int w, int h, Vec2 p) {
    return { (p.x - box.xmin) / (box.xmax - box.xmin) * w, (box.ymax - p.y) / (box.ymax - box.ymin) * h };
}

bool far_out(const WorldBox2& b, Vec2 p, Vec2 q) {
    const double wx = b.xmax - b.xmin;
    const double wy = b.ymax - b.ymin;
    if (p.x < b.xmin - wx && q.x < b.xmin - wx) return true;
    if (p.x > b.xmax + wx && q.x > b.xmax + wx) return true;
    if (p.y < b.ymin - wy && q.y < b.ymin - wy) return true;
    if (p.y > b.ymax + wy && q.y > b.ymax + wy) return true;
    return false;
}

} // namespace

double OpenMap::operator()(double u) const {
    if (std::isnan(u)) return u;
    switch (mode) {
        case 1: return a + sign * u / (1.0 - u);
        case 2: return b - sign * u / (1.0 + u);
        case 3: return sign * u / (1.0 - u * u);
        default: return a + (b - a) * u;
    }
}

double OpenMap::dt(double u) const {
    switch (mode) {
        case 1: return sign / ((1.0 - u) * (1.0 - u));
        case 2: return -sign / ((1.0 + u) * (1.0 + u));
        case 3: return sign * (1.0 + u * u) / ((1.0 - u * u) * (1.0 - u * u));
        default: return b - a;
    }
}

bool open_map(double t0, double t1, OpenMap& m, double eps) {
    const bool i0 = std::isinf(t0);
    const bool i1 = std::isinf(t1);
    m = OpenMap{};
    if (!i0 && !i1) return false;
    if (i0 && i1) {
        m.mode = 3;
        m.sign = t1 > 0 ? 1.0 : -1.0;
        m.u0 = -1.0 + eps;
        m.u1 = 1.0 - eps;
    } else if (i1) {
        m.mode = 1;
        m.a = t0;
        m.sign = t1 > 0 ? 1.0 : -1.0;
        m.u0 = 0.0;
        m.u1 = 1.0 - eps;
    } else {
        m.mode = 2;
        m.b = t1;
        m.sign = t0 > 0 ? -1.0 : 1.0;
        m.u0 = -1.0 + eps;
        m.u1 = 0.0;
    }
    return true;
}

void sample_curve2(const CurveSampler2& s, double t0, double t1, std::vector<double>& ts, std::vector<Vec2>& pts) {
    OpenMap om;
    if (open_map(t0, t1, om)) {
        CurveSampler2 w = s;
        w.initial = std::max(s.initial, 1200);
        w.rounds = s.rounds + 3;
        w.eval = [&](const std::vector<double>& us, std::vector<Vec2>& out) {
            std::vector<double> tt(us.size());
            for (std::size_t i = 0; i < us.size(); ++i) tt[i] = om(us[i]);
            s.eval(tt, out);
        };
        sample_curve2(w, om.u0, om.u1, ts, pts);
        for (double& t : ts) t = om(t);
        return;
    }
    ts = linspace(t0, t1, static_cast<std::size_t>(std::max(8, s.initial)));
    s.eval(ts, pts);
    const double tiny = std::fabs(t1 - t0) * 1e-9;
    for (int round = 0; round < s.rounds; ++round) {
        std::vector<double> mids;
        std::vector<std::size_t> after;
        for (std::size_t i = 0; i + 1 < ts.size(); ++i) {
            const Vec2 a = pts[i];
            const Vec2 b = pts[i + 1];
            const bool fa = finite(a);
            const bool fb = finite(b);
            bool need = false;
            if (fa && fb) {
                if (far_out(s.box, a, b)) continue;
                const Vec2 sa = to_px(s.box, s.width, s.height, a);
                const Vec2 sb = to_px(s.box, s.width, s.height, b);
                const double dist = length(sb - sa);
                if (dist > 3.0) need = true;
                else if (dist > 0.8 && i > 0 && i + 2 < ts.size() && finite(pts[i - 1]) && finite(pts[i + 2])) {
                    const Vec2 u = normalized(sa - to_px(s.box, s.width, s.height, pts[i - 1]));
                    const Vec2 v = normalized(sb - sa);
                    if (dot(u, v) < 0.97) need = true;
                }
            } else if (fa != fb) {
                need = true;
            }
            if (need && ts[i + 1] - ts[i] > tiny) {
                mids.push_back(0.5 * (ts[i] + ts[i + 1]));
                after.push_back(i);
            }
        }
        if (mids.empty()) break;
        std::vector<Vec2> mp;
        s.eval(mids, mp);
        std::vector<double> nt;
        std::vector<Vec2> np;
        nt.reserve(ts.size() + mids.size());
        np.reserve(ts.size() + mids.size());
        std::size_t m = 0;
        for (std::size_t i = 0; i < ts.size(); ++i) {
            nt.push_back(ts[i]);
            np.push_back(pts[i]);
            if (m < after.size() && after[m] == i) {
                nt.push_back(mids[m]);
                np.push_back(mp[m]);
                ++m;
            }
        }
        ts.swap(nt);
        pts.swap(np);
    }
    std::vector<double> ot;
    std::vector<Vec2> op;
    for (std::size_t i = 0; i < ts.size(); ++i) {
        if (i > 0 && finite(pts[i]) && finite(pts[i - 1])) {
            const Vec2 sa = to_px(s.box, s.width, s.height, pts[i - 1]);
            const Vec2 sb = to_px(s.box, s.width, s.height, pts[i]);
            if (std::fabs(sb.y - sa.y) > s.height * 0.6 && !far_out(s.box, pts[i - 1], pts[i])) {
                ot.push_back(k_nan);
                op.push_back(Vec2{ k_nan, k_nan });
            }
        }
        ot.push_back(ts[i]);
        op.push_back(pts[i]);
    }
    ts.swap(ot);
    pts.swap(op);
}

void sample_curve3(const CurveSampler3& s, double t0, double t1, std::vector<double>& ts, std::vector<Vec3>& pts) {
    OpenMap om;
    if (open_map(t0, t1, om)) {
        CurveSampler3 w = s;
        w.initial = std::max(s.initial, 1200);
        w.rounds = s.rounds + 3;
        w.eval = [&](const std::vector<double>& us, std::vector<Vec3>& out) {
            std::vector<double> tt(us.size());
            for (std::size_t i = 0; i < us.size(); ++i) tt[i] = om(us[i]);
            s.eval(tt, out);
        };
        sample_curve3(w, om.u0, om.u1, ts, pts);
        for (double& t : ts) t = om(t);
        return;
    }
    ts = linspace(t0, t1, static_cast<std::size_t>(std::max(8, s.initial)));
    s.eval(ts, pts);
    const double limit = s.scale / 180.0;
    const double tiny = std::fabs(t1 - t0) * 1e-9;
    for (int round = 0; round < s.rounds; ++round) {
        std::vector<double> mids;
        std::vector<std::size_t> after;
        for (std::size_t i = 0; i + 1 < ts.size(); ++i) {
            const bool fa = finite(pts[i]);
            const bool fb = finite(pts[i + 1]);
            bool need = false;
            if (fa && fb) need = length(pts[i + 1] - pts[i]) > limit && length(pts[i + 1] - pts[i]) < s.scale * 50.0;
            else if (fa != fb) need = true;
            if (need && ts[i + 1] - ts[i] > tiny) {
                mids.push_back(0.5 * (ts[i] + ts[i + 1]));
                after.push_back(i);
            }
        }
        if (mids.empty()) break;
        std::vector<Vec3> mp;
        s.eval(mids, mp);
        std::vector<double> nt;
        std::vector<Vec3> np;
        std::size_t m = 0;
        for (std::size_t i = 0; i < ts.size(); ++i) {
            nt.push_back(ts[i]);
            np.push_back(pts[i]);
            if (m < after.size() && after[m] == i) {
                nt.push_back(mids[m]);
                np.push_back(mp[m]);
                ++m;
            }
        }
        ts.swap(nt);
        pts.swap(np);
    }
}

Grid2 sample_grid(Func& f, double x0, double x1, double y0, double y1, int nx, int ny) {
    Grid2 g;
    g.nx = nx;
    g.ny = ny;
    g.x0 = x0;
    g.x1 = x1;
    g.y0 = y0;
    g.y1 = y1;
    std::vector<double> xs(static_cast<std::size_t>(nx) * ny);
    std::vector<double> ys(xs.size());
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            xs[static_cast<std::size_t>(j) * nx + i] = g.x(i);
            ys[static_cast<std::size_t>(j) * nx + i] = g.y(j);
        }
    }
    g.v = f.map2(xs, ys);
    return g;
}

std::vector<Vec2> marching_squares(const Grid2& g, double level, Func* refine) {
    const int nx = g.nx;
    const int ny = g.ny;
    const std::size_t hcount = static_cast<std::size_t>(nx - 1) * ny;
    const std::size_t total = hcount + static_cast<std::size_t>(nx) * (ny - 1);
    std::vector<Vec2> cross(total, Vec2{ k_nan, k_nan });
    std::vector<char> valid(total, 0);
    std::vector<std::size_t> crossing;
    auto val = [&](int i, int j) { return g.at(i, j) - level; };
    auto edge = [&](std::size_t e, int ia, int ja, int ib, int jb) {
        const double a = val(ia, ja);
        const double b = val(ib, jb);
        if (!std::isfinite(a) || !std::isfinite(b)) return;
        if ((a < 0.0) == (b < 0.0)) return;
        const double t = a / (a - b);
        const Vec2 pa{ g.x(ia), g.y(ja) };
        const Vec2 pb{ g.x(ib), g.y(jb) };
        cross[e] = pa + (pb - pa) * t;
        valid[e] = 1;
        crossing.push_back(e);
    };
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i + 1 < nx; ++i) edge(static_cast<std::size_t>(j) * (nx - 1) + i, i, j, i + 1, j);
    }
    for (int j = 0; j + 1 < ny; ++j) {
        for (int i = 0; i < nx; ++i) edge(hcount + static_cast<std::size_t>(j) * nx + i, i, j, i, j + 1);
    }
    if (refine && refine->ok() && !crossing.empty()) {
        std::vector<double> xs;
        std::vector<double> ys;
        std::vector<double> mags;
        std::vector<std::pair<Vec2, Vec2>> ends;
        std::vector<std::pair<double, double>> ends_v;
        for (std::size_t e : crossing) {
            int ia, ja, ib, jb;
            if (e < hcount) {
                ja = jb = static_cast<int>(e / (nx - 1));
                ia = static_cast<int>(e % (nx - 1));
                ib = ia + 1;
            } else {
                const std::size_t k = e - hcount;
                ja = static_cast<int>(k / nx);
                jb = ja + 1;
                ia = ib = static_cast<int>(k % nx);
            }
            xs.push_back(0.5 * (g.x(ia) + g.x(ib)));
            ys.push_back(0.5 * (g.y(ja) + g.y(jb)));
            mags.push_back(std::max(std::fabs(val(ia, ja)), std::fabs(val(ib, jb))));
            ends.push_back({ Vec2{ g.x(ia), g.y(ja) }, Vec2{ g.x(ib), g.y(jb) } });
            ends_v.push_back({ val(ia, ja), val(ib, jb) });
        }
        const std::vector<double> mid = refine->map2(xs, ys);
        std::vector<double> cx(crossing.size()), cy(crossing.size());
        for (std::size_t k = 0; k < crossing.size(); ++k) {
            cx[k] = cross[crossing[k]].x;
            cy[k] = cross[crossing[k]].y;
        }
        const std::vector<double> at = refine->map2(cx, cy);
        std::vector<double> tx1(crossing.size()), ty1(crossing.size()), tx2(crossing.size()), ty2(crossing.size());
        for (std::size_t k = 0; k < crossing.size(); ++k) {
            const Vec2 q1 = ends[k].first + (ends[k].second - ends[k].first) * (1.0 / 3.0);
            const Vec2 q2 = ends[k].first + (ends[k].second - ends[k].first) * (2.0 / 3.0);
            tx1[k] = q1.x;
            ty1[k] = q1.y;
            tx2[k] = q2.x;
            ty2[k] = q2.y;
        }
        const std::vector<double> third1 = refine->map2(tx1, ty1);
        const std::vector<double> third2 = refine->map2(tx2, ty2);
        for (std::size_t k = 0; k < crossing.size(); ++k) {
            const double m = mid[k] - level;
            const double f = at[k] - level;
            const double a = ends_v[k].first;
            const double b = ends_v[k].second;
            const double lo = std::min(a, b);
            const double hi = std::max(a, b);
            const double slack = 0.02 * (hi - lo);
            const double s1 = third1[k] - level;
            const double s2 = third2[k] - level;
            const double tol = 1e-9 * (hi - lo);
            const double over = std::max({ 0.0, s1 - hi, s2 - hi });
            const double under = std::max({ 0.0, lo - s1, lo - s2 });
            const bool monotone = std::isfinite(s1) && std::isfinite(s2) && over <= 0.5 * std::fabs(hi) + tol && under <= 0.5 * std::fabs(lo) + tol;
            if (!monotone || !std::isfinite(m) || std::fabs(m) > mags[k] * 1.5 + 1e-12 || !std::isfinite(f) || f < lo - slack || f > hi + slack) {
                valid[crossing[k]] = 0;
                continue;
            }
            const Vec2 pa = ends[k].first;
            const Vec2 pb = ends[k].second;
            const Vec2 pc = cross[crossing[k]];
            if (f == 0.0) continue;
            const bool left = (f < 0.0) != (a < 0.0);
            const Vec2 p0 = left ? pa : pc;
            const Vec2 p1 = left ? pc : pb;
            const double f0 = left ? a : f;
            const double f1 = left ? f : b;
            if ((f0 < 0.0) == (f1 < 0.0) || f0 == f1) continue;
            cross[crossing[k]] = p0 + (p1 - p0) * (f0 / (f0 - f1));
        }
    }
    std::vector<Vec2> out;
    auto seg = [&](std::size_t a, std::size_t b) {
        if (!valid[a] || !valid[b]) return;
        out.push_back(cross[a]);
        out.push_back(cross[b]);
        out.push_back(Vec2{ k_nan, k_nan });
    };
    for (int j = 0; j + 1 < ny; ++j) {
        for (int i = 0; i + 1 < nx; ++i) {
            const double v0 = val(i, j);
            const double v1 = val(i + 1, j);
            const double v2 = val(i + 1, j + 1);
            const double v3 = val(i, j + 1);
            if (!std::isfinite(v0) || !std::isfinite(v1) || !std::isfinite(v2) || !std::isfinite(v3)) continue;
            const int code = (v0 < 0.0 ? 1 : 0) | (v1 < 0.0 ? 2 : 0) | (v2 < 0.0 ? 4 : 0) | (v3 < 0.0 ? 8 : 0);
            if (code == 0 || code == 15) continue;
            const std::size_t e0 = static_cast<std::size_t>(j) * (nx - 1) + i;
            const std::size_t e2 = static_cast<std::size_t>(j + 1) * (nx - 1) + i;
            const std::size_t e3 = hcount + static_cast<std::size_t>(j) * nx + i;
            const std::size_t e1 = hcount + static_cast<std::size_t>(j) * nx + i + 1;
            switch (code) {
                case 1: case 14: seg(e3, e0); break;
                case 2: case 13: seg(e0, e1); break;
                case 3: case 12: seg(e3, e1); break;
                case 4: case 11: seg(e1, e2); break;
                case 6: case 9: seg(e0, e2); break;
                case 7: case 8: seg(e3, e2); break;
                case 5: case 10: {
                    const double c = 0.25 * (v0 + v1 + v2 + v3);
                    const bool center_neg = c < 0.0;
                    if ((code == 5) == center_neg) {
                        seg(e3, e2);
                        seg(e0, e1);
                    } else {
                        seg(e3, e0);
                        seg(e1, e2);
                    }
                    break;
                }
                default: break;
            }
        }
    }
    return out;
}

Vec3 Grid3::pos(int i, int j, int k) const {
    const double d = static_cast<double>(n - 1);
    return Vec3{ lo[0] + (hi[0] - lo[0]) * i / d, lo[1] + (hi[1] - lo[1]) * j / d, lo[2] + (hi[2] - lo[2]) * k / d };
}

void marching_tetrahedra(const Grid3& g, std::vector<Vec3>& tris) {
    static const int corner[8][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } };
    static const int tets[6][4] = { { 0, 5, 1, 6 }, { 0, 1, 2, 6 }, { 0, 2, 3, 6 }, { 0, 3, 7, 6 }, { 0, 7, 4, 6 }, { 0, 4, 5, 6 } };
    const int n = g.n;
    for (int k = 0; k + 1 < n; ++k) {
        for (int j = 0; j + 1 < n; ++j) {
            for (int i = 0; i + 1 < n; ++i) {
                double v[8];
                Vec3 p[8];
                bool ok = true;
                int neg = 0;
                for (int c = 0; c < 8; ++c) {
                    v[c] = g.at(i + corner[c][0], j + corner[c][1], k + corner[c][2]);
                    if (!std::isfinite(v[c])) ok = false;
                    if (v[c] < 0.0) ++neg;
                    p[c] = g.pos(i + corner[c][0], j + corner[c][1], k + corner[c][2]);
                }
                if (!ok || neg == 0 || neg == 8) continue;
                for (const auto& t : tets) {
                    const int a[4] = { t[0], t[1], t[2], t[3] };
                    int inside[4];
                    int outside[4];
                    int ni = 0;
                    int no = 0;
                    for (int q = 0; q < 4; ++q) {
                        if (v[a[q]] < 0.0) inside[ni++] = a[q];
                        else outside[no++] = a[q];
                    }
                    auto lerp = [&](int x, int y) {
                        const double t2 = v[x] / (v[x] - v[y]);
                        return p[x] + (p[y] - p[x]) * t2;
                    };
                    if (ni == 1 || no == 1) {
                        const int single = ni == 1 ? inside[0] : outside[0];
                        const int* rest = ni == 1 ? outside : inside;
                        tris.push_back(lerp(single, rest[0]));
                        tris.push_back(lerp(single, rest[1]));
                        tris.push_back(lerp(single, rest[2]));
                    } else if (ni == 2) {
                        const Vec3 q0 = lerp(inside[0], outside[0]);
                        const Vec3 q1 = lerp(inside[0], outside[1]);
                        const Vec3 q2 = lerp(inside[1], outside[1]);
                        const Vec3 q3 = lerp(inside[1], outside[0]);
                        tris.push_back(q0);
                        tris.push_back(q1);
                        tris.push_back(q2);
                        tris.push_back(q0);
                        tris.push_back(q2);
                        tris.push_back(q3);
                    }
                }
            }
        }
    }
}

void add_arrow2(Scene2& s, Vec2 from, Vec2 to, Rgba color, float width) {
    if (!finite(from) || !finite(to)) return;
    Arrow2 a;
    a.from = from;
    a.to = to;
    a.color = color;
    a.width = width;
    s.arrows.push_back(a);
}

void add_text2(Scene2& s, Vec2 at, const std::string& text, Rgba color) {
    s.texts.push_back(Text2{ at, text, color });
}

Stroke2 line_through(Vec2 p, Vec2 dir, const WorldBox2& box, Rgba color, float width, bool dashed) {
    Stroke2 s;
    s.color = color;
    s.width = width;
    s.dashed = dashed;
    const Vec2 u = normalized(dir);
    if (!finite(p) || !finite(u) || (u.x == 0.0 && u.y == 0.0)) return s;
    const double reach = 4.0 * std::hypot(box.xmax - box.xmin, box.ymax - box.ymin) + length(p - Vec2{ 0.5 * (box.xmin + box.xmax), 0.5 * (box.ymin + box.ymax) });
    s.pts = { p - u * reach, p + u * reach };
    return s;
}

std::vector<Vec2> circle_points(Vec2 c, double r, int n) {
    std::vector<Vec2> out;
    for (int i = 0; i <= n; ++i) {
        const double a = 6.283185307179586 * i / n;
        out.push_back(Vec2{ c.x + r * std::cos(a), c.y + r * std::sin(a) });
    }
    return out;
}

void offer_param(ItemStatus& st, const std::string& label, double lo, double hi, double value) {
    if (!std::isfinite(lo) || !std::isfinite(hi) || !(hi > lo)) return;
    st.has_param = true;
    st.param_label = label;
    st.param_min = lo;
    st.param_max = hi;
    st.param_value = std::isfinite(value) ? value : lo;
}

bool limit_closed(const Item& item, const std::string& field) {
    const auto it = item.options.find(field + "_closed");
    return it == item.options.end() ? true : it->second;
}

bool limit_style_chosen(const Item& item, const std::string& field) {
    return item.options.count(field + "_closed") > 0;
}

bool read_limits(const Item& item, const std::string& lo_field, const std::string& hi_field, Env& env, Limits& out, std::string& error) {
    out = Limits{};
    auto one = [&](const std::string& field, Limit& l) {
        std::string text = item.field(field);
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
        l.closed = limit_closed(item, field);
        if (text.empty()) return true;
        double v = 0.0;
        std::string e;
        if (!parse_number(text, env, v, e)) {
            error = field + ": " + e;
            return false;
        }
        if (std::isnan(v)) return true;
        l.set = std::isfinite(v);
        l.value = v;
        return true;
    };
    if (!one(lo_field, out.lo) || !one(hi_field, out.hi)) return false;
    if (out.lo.set && out.hi.set && out.lo.value > out.hi.value) {
        error = "the lower limit is above the upper one";
        return false;
    }
    return true;
}

ExprPtr restrict_to_limits(const ExprPtr& f, const Item& item, const std::vector<std::string>& vars, Env& env, std::string& error) {
    ExprPtr cond;
    for (const std::string& v : vars) {
        Limits l;
        if (!read_limits(item, v + "min", v + "max", env, l, error)) return nullptr;
        if (l.lo.set) {
            ExprPtr c = call(l.lo.closed ? "ge" : "gt", { variable(v), number(l.lo.value) });
            cond = cond ? call("and", { cond, c }) : c;
        }
        if (l.hi.set) {
            ExprPtr c = call(l.hi.closed ? "le" : "lt", { variable(v), number(l.hi.value) });
            cond = cond ? call("and", { cond, c }) : c;
        }
    }
    if (!cond) return f;
    return call("if", { cond, f });
}

bool parse_range(const Item& item, const char* lo, const char* hi, Env& env, Range& out, std::string& error) {
    double a = 0.0;
    double b = 0.0;
    if (!parse_number(item.field(lo), env, a, error)) return false;
    if (!parse_number(item.field(hi), env, b, error)) return false;
    if (a == b) {
        error = "the range is empty";
        return false;
    }
    out.a = a;
    out.b = b;
    return true;
}

bool parse_open_range(const Item& item, const char* lo, const char* hi, Env& env, Range& out, std::string& error) {
    auto one = [&](const char* field, double empty, double& v) {
        std::string text = item.field(field);
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
        if (text.empty()) {
            v = empty;
            return true;
        }
        std::string bare;
        for (char ch : text) {
            if (!std::isspace(static_cast<unsigned char>(ch))) bare += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        double sign = 1.0;
        if (!bare.empty() && (bare[0] == '-' || bare[0] == '+')) {
            sign = bare[0] == '-' ? -1.0 : 1.0;
            bare.erase(0, 1);
        }
        if (bare == "inf" || bare == "infinity" || bare == "\xE2\x88\x9E" || bare == "oo") {
            v = sign * INFINITY;
            return true;
        }
        if (!parse_number(text, env, v, error)) return false;
        if (std::isnan(v)) {
            error = std::string(field) + " isn't a number";
            return false;
        }
        return true;
    };
    double a = 0.0;
    double b = 0.0;
    if (!one(lo, -INFINITY, a) || !one(hi, INFINITY, b)) return false;
    if (a == b) {
        error = "the range is empty";
        return false;
    }
    out.a = a;
    out.b = b;
    return true;
}

bool range_unbounded(const Range& r) {
    return std::isinf(r.a) || std::isinf(r.b);
}

namespace {

double gauss_panels(const std::function<double(double)>& f, double a, double b, int m, bool& ok) {
    static const double gx[5] = { -0.9061798459386640, -0.5384693101056831, 0.0, 0.5384693101056831, 0.9061798459386640 };
    static const double gw[5] = { 0.2369268850561891, 0.4786286704993665, 0.5688888888888889, 0.4786286704993665, 0.2369268850561891 };
    const double h = (b - a) / m;
    double sum = 0.0;
    for (int i = 0; i < m; ++i) {
        const double mid = a + h * (i + 0.5);
        for (int q = 0; q < 5; ++q) {
            const double v = f(mid + 0.5 * h * gx[q]);
            if (!std::isfinite(v)) {
                ok = false;
                return k_nan;
            }
            sum += gw[q] * v;
        }
    }
    return sum * 0.5 * h;
}

Integral tail(const std::function<double(double)>& f, double a, double dir) {
    Integral out;
    double sum = 0.0;
    double step = 1.0;
    double start = a;
    int quiet = 0;
    double last = INFINITY;
    int growing = 0;
    for (int k = 0; k < 90; ++k) {
        if (build_cancelled()) {
            out.ok = false;
            return out;
        }
        const double end = start + dir * step;
        bool ok = true;
        const double part = gauss_panels(f, start, end, 48, ok) * dir;
        if (!ok) {
            out.ok = false;
            return out;
        }
        sum += part;
        const double mag = std::fabs(part);
        if (mag <= 1e-14 * std::max(std::fabs(sum), 1e-300)) {
            if (++quiet >= 3) {
                out.value = sum;
                return out;
            }
        } else {
            quiet = 0;
        }
        if (k > 6 && mag > 0.7 * last) ++growing;
        else growing = 0;
        if (growing >= 8) {
            out.finite = false;
            out.value = sum > 0 ? INFINITY : -INFINITY;
            return out;
        }
        last = mag;
        start = end;
        step *= 2.0;
    }
    out.finite = false;
    out.value = sum > 0 ? INFINITY : -INFINITY;
    return out;
}

} // namespace

Integral integrate_open(const std::function<double(double)>& f, double a, double b, int panels) {
    Integral out;
    if (!std::isinf(a) && !std::isinf(b)) {
        const double i1 = gauss_panels(f, a, b, panels, out.ok);
        if (!out.ok) return out;
        const double i2 = gauss_panels(f, a, b, panels * 2, out.ok);
        if (!out.ok) return out;
        if (std::fabs(i2 - i1) <= 1e-10 * std::max(1.0, std::fabs(i2))) {
            out.value = i2;
            return out;
        }
        const double i3 = gauss_panels(f, a, b, panels * 4, out.ok);
        if (!out.ok) return out;
        const double d1 = std::fabs(i2 - i1);
        const double d2 = std::fabs(i3 - i2);
        if (d2 > 1e-6 * std::max(1.0, std::fabs(i3)) && d2 >= 0.9 * d1) {
            out.finite = false;
            out.value = i3 > 0 ? INFINITY : -INFINITY;
            return out;
        }
        out.value = i3;
        return out;
    }
    const double sgn = a < b ? 1.0 : -1.0;
    double lo = std::min(a, b);
    double hi = std::max(a, b);
    double sum = 0.0;
    const double mid = std::isinf(lo) && std::isinf(hi) ? 0.0 : std::isinf(lo) ? hi - 1.0 : lo + 1.0;
    if (!std::isinf(lo) || !std::isinf(hi)) {
        const double fa = std::isinf(lo) ? mid : lo;
        const double fb = std::isinf(lo) ? hi : mid;
        sum += gauss_panels(f, fa, fb, panels / 4 + 1, out.ok);
        if (!out.ok) return out;
    }
    if (std::isinf(hi)) {
        const Integral t = tail(f, mid, 1.0);
        if (!t.ok) return t;
        if (!t.finite) out.finite = false;
        sum += t.value;
    }
    if (std::isinf(lo)) {
        const Integral t = tail(f, mid, -1.0);
        if (!t.ok) return t;
        if (!t.finite) out.finite = false;
        sum += t.value;
    }
    out.value = sgn * sum;
    if (!out.finite && std::isfinite(out.value)) out.value = INFINITY;
    return out;
}

void slider_span(const Range& r, double t0, double& lo, double& hi) {
    lo = std::min(r.a, r.b);
    hi = std::max(r.a, r.b);
    if (std::isinf(lo) && std::isinf(hi)) {
        lo = std::min(-10.0, t0 - 1.0);
        hi = std::max(10.0, t0 + 1.0);
    } else if (std::isinf(lo)) {
        lo = std::min(hi - 10.0, t0 - 1.0);
    } else if (std::isinf(hi)) {
        hi = std::max(lo + 10.0, t0 + 1.0);
    }
    if (r.a > r.b) std::swap(lo, hi);
}

void direction_arrows2(Ctx& c, const std::vector<Vec2>& pts, Rgba color) {
    const double w = c.box.xmax - c.box.xmin;
    const double hgt = c.box.ymax - c.box.ymin;
    auto inside = [&](const Vec2& p) { return finite(p) && p.x >= c.box.xmin && p.x <= c.box.xmax && p.y >= c.box.ymin && p.y <= c.box.ymax; };
    auto px = [&](const Vec2& p) { return Vec2{ (p.x - c.box.xmin) / w * c.width, (p.y - c.box.ymin) / hgt * c.height }; };
    double total = 0.0;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        if (inside(pts[i]) && inside(pts[i - 1])) total += length(px(pts[i]) - px(pts[i - 1]));
    }
    if (total < 40.0) return;
    const double gap = std::max(90.0, total / 7.0);
    double run = gap * 0.5;
    const double len = 0.035 * std::min(w, hgt);
    for (std::size_t i = 1; i + 1 < pts.size(); ++i) {
        if (!inside(pts[i]) || !inside(pts[i - 1])) continue;
        run += length(px(pts[i]) - px(pts[i - 1]));
        if (run < gap) continue;
        const Vec2 dir = pts[i + 1] - pts[i - 1];
        if (!finite(dir) || length(dir) <= 0.0) continue;
        run = 0.0;
        const Vec2 u = normalized(dir);
        add_arrow2(c.scene.s2, pts[i] - u * (len * 0.5), pts[i] + u * (len * 0.5), color, 2.0f);
    }
}

void direction_arrows3(Ctx& c, const std::vector<Vec3>& pts, Rgba color) {
    const double reach = 1.5 * c.range;
    auto inside = [&](const Vec3& p) { return finite(p) && std::fabs(p.x - c.view3.target.x) <= reach && std::fabs(p.y - c.view3.target.y) <= reach && p.z >= c.zmin - 0.5 * reach && p.z <= c.zmax + 0.5 * reach; };
    double total = 0.0;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        if (inside(pts[i]) && inside(pts[i - 1])) total += length(pts[i] - pts[i - 1]);
    }
    if (total <= 0.0) return;
    const double gap = std::max(0.25 * c.range, total / 7.0);
    double run = gap * 0.5;
    const double len = 0.12 * c.range;
    for (std::size_t i = 1; i + 1 < pts.size(); ++i) {
        if (!inside(pts[i]) || !inside(pts[i - 1])) continue;
        run += length(pts[i] - pts[i - 1]);
        if (run < gap) continue;
        const Vec3 dir = pts[i + 1] - pts[i - 1];
        if (!finite(dir) || length(dir) <= 0.0) continue;
        run = 0.0;
        const Vec3 u = normalized(dir);
        c.scene.s3.arrows.push_back(Arrow3{ pts[i] - u * (len * 0.5), pts[i] + u * (len * 0.5), color, 2.4f, "" });
    }
}

double robust_min(std::vector<double> v) {
    v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !std::isfinite(x); }), v.end());
    if (v.empty()) return 0.0;
    const std::size_t k = v.size() / 50;
    std::nth_element(v.begin(), v.begin() + static_cast<long>(k), v.end());
    return v[k];
}

double robust_max(std::vector<double> v) {
    v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !std::isfinite(x); }), v.end());
    if (v.empty()) return 1.0;
    const std::size_t k = v.size() - 1 - v.size() / 50;
    std::nth_element(v.begin(), v.begin() + static_cast<long>(k), v.end());
    return v[k];
}

void surface_mesh(Ctx& c, const std::function<void(const std::vector<double>&, const std::vector<double>&, std::vector<Vec3>&)>& pos, double u0, double u1, double v0, double v1, int nu, int nv, Mesh3& mesh, std::vector<Vec3>& grid) {
    (void)c;
    std::vector<double> us(static_cast<std::size_t>(nu) * nv);
    std::vector<double> vs(us.size());
    for (int j = 0; j < nv; ++j) {
        for (int i = 0; i < nu; ++i) {
            us[static_cast<std::size_t>(j) * nu + i] = u0 + (u1 - u0) * i / (nu - 1);
            vs[static_cast<std::size_t>(j) * nu + i] = v0 + (v1 - v0) * j / (nv - 1);
        }
    }
    pos(us, vs, grid);
    mesh.pos = grid;
    mesh.nrm.assign(grid.size(), Vec3{ 0.0, 0.0, 1.0 });
    auto at = [&](int i, int j) -> const Vec3& { return grid[static_cast<std::size_t>(j) * nu + i]; };
    for (int j = 0; j < nv; ++j) {
        for (int i = 0; i < nu; ++i) {
            const int il = std::max(0, i - 1);
            const int ir = std::min(nu - 1, i + 1);
            const int jd = std::max(0, j - 1);
            const int ju = std::min(nv - 1, j + 1);
            Vec3 du = at(ir, j) - at(il, j);
            Vec3 dv = at(i, ju) - at(i, jd);
            if (!finite(du)) du = finite(at(ir, j) - at(i, j)) ? at(ir, j) - at(i, j) : at(i, j) - at(il, j);
            if (!finite(dv)) dv = finite(at(i, ju) - at(i, j)) ? at(i, ju) - at(i, j) : at(i, j) - at(i, jd);
            const Vec3 n = normalized(cross(du, dv));
            if (finite(n) && length(n) > 0.0) mesh.nrm[static_cast<std::size_t>(j) * nu + i] = n;
        }
    }
    const double zlo = c.zmin - 0.05 * (c.zmax - c.zmin);
    const double zhi = c.zmax + 0.05 * (c.zmax - c.zmin);
    for (int j = 0; j + 1 < nv; ++j) {
        for (int i = 0; i + 1 < nu; ++i) {
            const std::uint32_t a = static_cast<std::uint32_t>(j * nu + i);
            const std::uint32_t b = a + 1;
            const std::uint32_t d2 = static_cast<std::uint32_t>((j + 1) * nu + i);
            const std::uint32_t e = d2 + 1;
            const Vec3* q[4] = { &grid[a], &grid[b], &grid[e], &grid[d2] };
            bool ok = true;
            bool any_in = false;
            for (const Vec3* p : q) {
                if (!finite(*p)) ok = false;
                else if (p->z >= zlo && p->z <= zhi) any_in = true;
            }
            if (!ok || !any_in) continue;
            mesh.tri.insert(mesh.tri.end(), { a, b, e, a, e, d2 });
        }
    }
}

void mesh_lines(Ctx& c, const std::vector<Vec3>& grid, int nu, int nv, int every, Rgba color) {
    every = std::max(1, every);
    const double zlo = c.zmin - 0.05 * (c.zmax - c.zmin);
    const double zhi = c.zmax + 0.05 * (c.zmax - c.zmin);
    auto ok = [&](const Vec3& p) { return finite(p) && p.z >= zlo && p.z <= zhi; };
    Line3 lines;
    lines.color = color;
    lines.width = 1.2f;
    const Vec3 brk{ k_nan, k_nan, k_nan };
    for (int j = 0; j < nv; j += every) {
        for (int i = 0; i < nu; ++i) {
            const Vec3& p = grid[static_cast<std::size_t>(j) * nu + i];
            lines.pts.push_back(ok(p) ? p : brk);
        }
        lines.pts.push_back(brk);
    }
    for (int i = 0; i < nu; i += every) {
        for (int j = 0; j < nv; ++j) {
            const Vec3& p = grid[static_cast<std::size_t>(j) * nu + i];
            lines.pts.push_back(ok(p) ? p : brk);
        }
        lines.pts.push_back(brk);
    }
    c.scene.s3.lines.push_back(std::move(lines));
}

void finish_normals(Mesh3& mesh) {
    if (mesh.nrm.size() == mesh.pos.size()) return;
    mesh.nrm.assign(mesh.pos.size(), Vec3{ 0.0, 0.0, 0.0 });
    for (std::size_t t = 0; t + 2 < mesh.tri.size(); t += 3) {
        const Vec3& a = mesh.pos[mesh.tri[t]];
        const Vec3& b = mesh.pos[mesh.tri[t + 1]];
        const Vec3& c = mesh.pos[mesh.tri[t + 2]];
        const Vec3 n = cross(b - a, c - a);
        for (int k = 0; k < 3; ++k) mesh.nrm[mesh.tri[t + k]] = mesh.nrm[mesh.tri[t + k]] + n;
    }
    for (Vec3& n : mesh.nrm) n = normalized(n);
}

} // namespace build
} // namespace arbor

namespace arbor {
namespace build {

bool plane_curve(const std::string& text, Env& env, PlaneCurve& out, std::string& error) {
    ParseResult r = parse_field(text, env, { "x", "y", "t" });
    if (!r.ok) {
        error = r.error;
        return false;
    }
    const Statement& s = r.statement;
    auto var = [](const ExprPtr& e, const char* n) { return e && e->op == Op::Variable && e->text == n; };
    const ExprPtr t = variable("t");
    ExprPtr X, Y;
    if (s.relation == Relation::None && component_count(s.lhs) == 2) {
        X = component(s.lhs, 0);
        Y = component(s.lhs, 1);
        out.kind = PlaneCurve::Kind::Parametric;
    } else if (s.relation == Relation::Equal && component_count(s.rhs) == 2) {
        X = component(s.rhs, 0);
        Y = component(s.rhs, 1);
        out.kind = PlaneCurve::Kind::Parametric;
    } else if (s.relation == Relation::Equal && var(s.lhs, "y") && !depends_on(s.rhs, "y")) {
        X = t;
        Y = substitute(s.rhs, { { "x", t } });
        out.kind = PlaneCurve::Kind::OfX;
    } else if (s.relation == Relation::Equal && var(s.lhs, "x") && !depends_on(s.rhs, "x")) {
        X = substitute(s.rhs, { { "y", t } });
        Y = t;
        out.kind = PlaneCurve::Kind::OfY;
    } else if (s.relation == Relation::None && !depends_on(s.lhs, "y")) {
        X = t;
        Y = substitute(s.lhs, { { "x", t } });
        out.kind = PlaneCurve::Kind::OfX;
    } else {
        error = "write the curve as y = f(x), x = g(y) or \xE2\x9F\xA8x(t), y(t)\xE2\x9F\xA9";
        return false;
    }
    out.x = prepare(X, env, { "t" }, error);
    if (!out.x) return false;
    out.y = prepare(Y, env, { "t" }, error);
    if (!out.y) return false;
    out.original = s;
    return true;
}

bool plane_line(const std::string& text, Env& env, Vec2& point, Vec2& dir, std::string& error) {
    std::string t;
    for (char ch : text) {
        if (!std::isspace(static_cast<unsigned char>(ch))) t += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (t.empty() || t == "x" || t == "x-axis" || t == "xaxis" || t == "y=0") {
        point = Vec2{ 0.0, 0.0 };
        dir = Vec2{ 1.0, 0.0 };
        return true;
    }
    if (t == "y" || t == "y-axis" || t == "yaxis" || t == "x=0") {
        point = Vec2{ 0.0, 0.0 };
        dir = Vec2{ 0.0, 1.0 };
        return true;
    }
    ParseResult r = parse_field(text, env, { "x", "y" });
    if (!r.ok) {
        error = r.error;
        return false;
    }
    const Statement& s = r.statement;
    auto var = [](const ExprPtr& e, const char* n) { return e && e->op == Op::Variable && e->text == n; };
    const bool of_x = s.relation == Relation::Equal && var(s.lhs, "y") && !depends_on(s.rhs, "y");
    const bool of_y = s.relation == Relation::Equal && var(s.lhs, "x") && !depends_on(s.rhs, "x");
    if (!of_x && !of_y) {
        error = "write the line as x-axis, y-axis, y = m x + b or x = c";
        return false;
    }
    const char* in = of_x ? "x" : "y";
    ExprPtr g = prepare(s.rhs, env, { in }, error);
    if (!g) return false;
    Func f(g, { in }, false, NumberType::Hardware);
    const double g0 = f.at({ 0.0 });
    const double g1 = f.at({ 1.0 });
    const double g2 = f.at({ 2.0 });
    const double g5 = f.at({ -3.0 });
    if (!std::isfinite(g0) || !std::isfinite(g1) || std::fabs(g2 - (2.0 * g1 - g0)) > 1e-9 * (1.0 + std::fabs(g2)) || std::fabs(g5 - (g0 - 3.0 * (g1 - g0))) > 1e-9 * (1.0 + std::fabs(g5))) {
        error = "the axis has to be a straight line";
        return false;
    }
    const double m = g1 - g0;
    if (of_x) {
        point = Vec2{ 0.0, g0 };
        dir = normalized(Vec2{ 1.0, m });
    } else {
        point = Vec2{ g0, 0.0 };
        dir = normalized(Vec2{ m, 1.0 });
    }
    return true;
}

} // namespace build
} // namespace arbor
