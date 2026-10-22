#include "build_common.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace arbor {
namespace build {

namespace {

const double k_nan = std::nan("");
const double k_tau = 6.283185307179586;

Rgba blend(const Ctx& c, Rgba col, double t) {
    return mix(col, c.env.theme.background, t);
}

void stroke(Ctx& c, std::vector<Vec2> pts, Rgba color, float width, bool dashed) {
    Stroke2 s;
    s.pts = std::move(pts);
    s.color = color;
    s.width = width;
    s.dashed = dashed;
    c.scene.s2.strokes.push_back(std::move(s));
}

double box_xlo(const Ctx& c) { return c.view3.target.x - c.range; }
double box_xhi(const Ctx& c) { return c.view3.target.x + c.range; }
double box_ylo(const Ctx& c) { return c.view3.target.y - c.range_y; }
double box_yhi(const Ctx& c) { return c.view3.target.y + c.range_y; }

std::string join_commas(const std::string& text) {
    std::string out;
    int depth = 0;
    for (char ch : text) {
        if (ch == '(' || ch == '[' || ch == '{') ++depth;
        if (ch == ')' || ch == ']' || ch == '}') depth = std::max(0, depth - 1);
        if ((ch == ',' || ch == ';') && depth == 0) out += " and ";
        else out += ch;
    }
    return out;
}

struct Condition {
    ExprPtr lhs;
    ExprPtr rhs;
    bool strict = true;
};

ExprPtr signed_region(const ExprPtr& e, std::vector<Condition>& leaves, bool& strict_all, std::string& err) {
    if (!e || e->op != Op::Call) {
        err = "write conditions with < > \xE2\x89\xA4 \xE2\x89\xA5, joined by and, or, not or commas, like y > x^2 and x^2 + y^2 < 9";
        return nullptr;
    }
    const std::string& n = e->text;
    if ((n == "and" || n == "or") && e->args.size() == 2) {
        ExprPtr a = signed_region(e->args[0], leaves, strict_all, err);
        ExprPtr b = a ? signed_region(e->args[1], leaves, strict_all, err) : nullptr;
        if (!a || !b) return nullptr;
        return call(n == "and" ? "min" : "max", { a, b });
    }
    if (n == "not" && e->args.size() == 1) {
        ExprPtr a = signed_region(e->args[0], leaves, strict_all, err);
        return a ? neg(a) : nullptr;
    }
    if ((n == "lt" || n == "le" || n == "gt" || n == "ge") && e->args.size() == 2) {
        const bool strict = n == "lt" || n == "gt";
        strict_all = strict_all && strict;
        leaves.push_back(Condition{ e->args[0], e->args[1], strict });
        return n == "lt" || n == "le" ? sub(e->args[1], e->args[0]) : sub(e->args[0], e->args[1]);
    }
    if (n == "eq" || n == "ne") {
        err = "a region needs < > \xE2\x89\xA4 or \xE2\x89\xA5; = gives a curve (use an implicit curve or surface for that)";
        return nullptr;
    }
    err = "write conditions with < > \xE2\x89\xA4 \xE2\x89\xA5, joined by and, or, not or commas, like y > x^2 and x^2 + y^2 < 9";
    return nullptr;
}

ExprPtr plane_coordinates(const ExprPtr& e) {
    const ExprPtr x = variable("x");
    const ExprPtr y = variable("y");
    return substitute(e, { { "r", call("sqrt", { add(pow(x, number(2.0)), pow(y, number(2.0))) }) },
                           { "theta", call("mod", { call("atan2", { y, x }), mul(number(2.0), variable("pi")) }) } });
}

ExprPtr space_coordinates(const ExprPtr& e) {
    const ExprPtr x = variable("x");
    const ExprPtr y = variable("y");
    const ExprPtr z = variable("z");
    const ExprPtr rho = call("sqrt", { add(add(pow(x, number(2.0)), pow(y, number(2.0))), pow(z, number(2.0))) });
    return substitute(e, { { "r", call("sqrt", { add(pow(x, number(2.0)), pow(y, number(2.0))) }) },
                           { "rho", rho },
                           { "theta", call("mod", { call("atan2", { y, x }), mul(number(2.0), variable("pi")) }) },
                           { "phi", call("acos", { div(z, rho) }) } });
}

ExprPtr curve_of(const std::string& text, const std::string& var, const std::string& other, Env& env, std::string& err) {
    ParseResult r = parse_field(text, env, { var, other });
    if (!r.ok) {
        err = r.error;
        return nullptr;
    }
    ExprPtr raw = r.statement.lhs;
    if (r.statement.relation == Relation::Equal && r.statement.lhs && r.statement.lhs->op == Op::Variable && r.statement.lhs->text == other) raw = r.statement.rhs;
    else if (r.statement.relation != Relation::None) {
        err = "write " + other + " = f(" + var + ") or just f(" + var + ")";
        return nullptr;
    }
    if (depends_on(raw, other)) {
        err = "this curve uses " + other + "; write it as a function of " + var + " only";
        return nullptr;
    }
    return prepare(raw, env, { var }, err);
}

bool bound_text_empty(const std::string& s) {
    return s.find_first_not_of(" \t") == std::string::npos;
}

std::string bound_symbol(double v) {
    return format_double(v, 6);
}

void region_raster(Ctx& c, Func& inside, Rgba fill, double& area) {
    const int step = c.env.quality >= 3 ? 1 : 2;
    Raster2 ras;
    ras.xmin = c.box.xmin;
    ras.xmax = c.box.xmax;
    ras.ymin = c.box.ymin;
    ras.ymax = c.box.ymax;
    ras.w = std::max(1, c.width / step);
    ras.h = std::max(1, c.height / step);
    std::vector<double> xs(static_cast<std::size_t>(ras.w) * ras.h);
    std::vector<double> ys(xs.size());
    for (int j = 0; j < ras.h; ++j) {
        for (int i = 0; i < ras.w; ++i) {
            xs[static_cast<std::size_t>(j) * ras.w + i] = ras.xmin + (ras.xmax - ras.xmin) * (i + 0.5) / ras.w;
            ys[static_cast<std::size_t>(j) * ras.w + i] = ras.ymax - (ras.ymax - ras.ymin) * (j + 0.5) / ras.h;
        }
    }
    const std::vector<double> v = inside.map2(xs, ys);
    ras.px.resize(v.size());
    std::size_t count = 0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const bool in = std::isfinite(v[i]) && v[i] > 0.0;
        if (in) ++count;
        ras.px[i] = in ? fill : Rgba{ 0, 0, 0, 0 };
    }
    area = static_cast<double>(count) * (ras.xmax - ras.xmin) * (ras.ymax - ras.ymin) / static_cast<double>(v.size());
    c.scene.s2.rasters.push_back(std::move(ras));
}

std::vector<Vec2> level_curve(Ctx& c, Func& f) {
    const int cell = c.env.quality >= 3 ? 2 : 3;
    const double padx = (c.box.xmax - c.box.xmin) * 0.01;
    const double pady = (c.box.ymax - c.box.ymin) * 0.01;
    Grid2 g = sample_grid(f, c.box.xmin - padx, c.box.xmax + padx, c.box.ymin - pady, c.box.ymax + pady, c.width / cell + 2, c.height / cell + 2);
    return marching_squares(g, 0.0, &f);
}

struct SurfaceGrid {
    int nu = 0;
    int nv = 0;
    std::vector<Vec3> pts;
};

void add_sheet(Ctx& c, const SurfaceGrid& s, Rgba color, double alpha, bool lines) {
    Mesh3 mesh;
    mesh.alpha = alpha;
    mesh.pos = s.pts;
    mesh.col.assign(s.pts.size(), color);
    auto at = [&](int i, int j) { return static_cast<std::uint32_t>(j * s.nu + i); };
    for (int j = 0; j + 1 < s.nv; ++j) {
        for (int i = 0; i + 1 < s.nu; ++i) {
            const std::uint32_t q[4] = { at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1) };
            bool ok = true;
            for (std::uint32_t k : q) ok = ok && finite(s.pts[k]);
            if (!ok) continue;
            mesh.tri.insert(mesh.tri.end(), { q[0], q[1], q[2], q[0], q[2], q[3] });
        }
    }
    if (mesh.tri.empty()) return;
    finish_normals(mesh);
    c.scene.s3.meshes.push_back(std::move(mesh));
    if (!lines) return;
    const int every_u = std::max(1, (s.nu - 1) / 8);
    const int every_v = std::max(1, (s.nv - 1) / 8);
    for (int i = 0; i < s.nu; i += every_u) {
        Line3 l;
        l.color = with_alpha(blend(c, color, 0.0), 0.55);
        l.width = 1.0f;
        for (int j = 0; j < s.nv; ++j) l.pts.push_back(s.pts[static_cast<std::size_t>(at(i, j))]);
        c.scene.s3.lines.push_back(std::move(l));
    }
    for (int j = 0; j < s.nv; j += every_v) {
        Line3 l;
        l.color = with_alpha(blend(c, color, 0.0), 0.55);
        l.width = 1.0f;
        for (int i = 0; i < s.nu; ++i) l.pts.push_back(s.pts[static_cast<std::size_t>(at(i, j))]);
        c.scene.s3.lines.push_back(std::move(l));
    }
}

} // namespace

void build_region2d(const Item& item, Ctx& c, ItemStatus& st) {
    const std::vector<std::string> vars = { "x", "y", "r", "theta" };
    ParseResult r = parse_condition(join_commas(item.field("eq")), parse_options(c.env, vars));
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    std::vector<Condition> leaves;
    bool strict_all = true;
    std::string err;
    ExprPtr S0 = signed_region(r.statement.lhs, leaves, strict_all, err);
    if (!S0) {
        st.error = err;
        return;
    }
    st.mathtext = to_mathtext(r.statement.lhs);
    ExprPtr S = prepare(plane_coordinates(S0), c.env, { "x", "y" }, err);
    if (!S) {
        st.error = err;
        return;
    }
    Func inside(S, { "x", "y" }, false, NumberType::Hardware);
    if (!inside.ok()) {
        st.error = inside.error();
        return;
    }
    const Rgba col = item_color(item);
    double area = 0.0;
    if (item.option("shade")) region_raster(c, inside, soft(col, 0.26), area);
    else {
        Func counter(S, { "x", "y" }, false, NumberType::Hardware);
        const int n = 300;
        std::vector<double> xs, ys;
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                xs.push_back(c.box.xmin + (c.box.xmax - c.box.xmin) * (i + 0.5) / n);
                ys.push_back(c.box.ymin + (c.box.ymax - c.box.ymin) * (j + 0.5) / n);
            }
        }
        const std::vector<double> v = counter.map2(xs, ys);
        std::size_t count = 0;
        for (double x : v) count += std::isfinite(x) && x > 0.0 ? 1 : 0;
        area = static_cast<double>(count) * (c.box.xmax - c.box.xmin) * (c.box.ymax - c.box.ymin) / static_cast<double>(v.size());
    }
    if (item.option("boundary")) {
        Func edge(S, { "x", "y" }, false, c.env.geo_type);
        if (edge.ok()) stroke(c, level_curve(c, edge), col, 2.4f, strict_all);
    }
    if (item.option("each")) {
        for (const Condition& leaf : leaves) {
            ExprPtr g = prepare(plane_coordinates(sub(leaf.lhs, leaf.rhs)), c.env, { "x", "y" }, err);
            if (!g) continue;
            Func gf(g, { "x", "y" }, false, NumberType::Hardware);
            if (gf.ok()) stroke(c, level_curve(c, gf), blend(c, col, 0.55), 1.4f, true);
        }
    }
    if (area == 0.0) st.note = "no part of this region is in view";
    st.readouts.push_back({ "area in view \xE2\x89\x88", number_text(area), std::string() });
    st.readouts.push_back({ "conditions", std::to_string(leaves.size()), std::string() });
}

void build_between2d(const Item& item, Ctx& c, ItemStatus& st) {
    std::string var = "x";
    {
        ParseResult probe = parse_field(item.field("eq"), c.env, { "x", "y" });
        std::string want = item.field("var");
        want.erase(std::remove_if(want.begin(), want.end(), [](char ch) { return std::isspace(static_cast<unsigned char>(ch)); }), want.end());
        if (want == "y") var = "y";
        if (probe.ok && probe.statement.relation == Relation::Equal && probe.statement.lhs && probe.statement.lhs->op == Op::Variable && probe.statement.lhs->text == "x") var = "y";
    }
    const std::string other = var == "x" ? "y" : "x";
    const bool in_y = var == "y";
    std::string err;
    const ExprPtr lower = curve_of(item.field("eq"), var, other, c.env, err);
    const ExprPtr upper = lower ? curve_of(item.field("upper"), var, other, c.env, err) : nullptr;
    if (!lower || !upper) {
        st.error = err;
        return;
    }
    st.mathtext = to_mathtext(lower) + " \\leq\\ " + other + " \\leq\\ " + to_mathtext(upper);
    Func fl(lower, { var }, false, NumberType::Hardware);
    Func fu(upper, { var }, false, NumberType::Hardware);
    if (!fl.ok() || !fu.ok()) {
        st.error = fl.ok() ? fu.error() : fl.error();
        return;
    }
    const double view_a = in_y ? c.box.ymin : c.box.xmin;
    const double view_b = in_y ? c.box.ymax : c.box.xmax;
    double a = view_a;
    double b = view_b;
    const bool auto_a = bound_text_empty(item.field("min"));
    const bool auto_b = bound_text_empty(item.field("max"));
    if (!auto_a && !parse_number(item.field("min"), c.env, a, err)) {
        st.error = "from: " + err;
        return;
    }
    if (!auto_b && !parse_number(item.field("max"), c.env, b, err)) {
        st.error = "to: " + err;
        return;
    }
    if (auto_a || auto_b) {
        const int n = 4000;
        std::vector<double> crossings;
        double prev_t = view_a;
        double prev = fu.at({ prev_t }) - fl.at({ prev_t });
        for (int i = 1; i <= n; ++i) {
            const double t = view_a + (view_b - view_a) * i / n;
            const double v = fu.at({ t }) - fl.at({ t });
            if (std::isfinite(prev) && std::isfinite(v) && ((prev <= 0.0 && v > 0.0) || (prev >= 0.0 && v < 0.0))) {
                double lo = prev_t, hi = t, flo = prev;
                for (int k = 0; k < 60; ++k) {
                    const double mid = 0.5 * (lo + hi);
                    const double fm = fu.at({ mid }) - fl.at({ mid });
                    if ((fm > 0.0) == (flo > 0.0)) {
                        lo = mid;
                        flo = fm;
                    } else {
                        hi = mid;
                    }
                }
                crossings.push_back(0.5 * (lo + hi));
            }
            prev_t = t;
            prev = v;
        }
        if (auto_a && auto_b && crossings.size() >= 2) {
            a = crossings.front();
            b = crossings.back();
            st.note = "the ends are where the curves cross (" + format_double(a, 6) + " and " + format_double(b, 6) + ")";
        } else if (auto_a && !auto_b) {
            double best = view_a;
            for (double t : crossings) if (t < b) best = t;
            a = best;
        } else if (auto_b && !auto_a) {
            double best = view_b;
            for (auto it = crossings.rbegin(); it != crossings.rend(); ++it) if (*it > a) best = *it;
            b = best;
        } else {
            st.note = "the curves don't cross twice in view, so the region runs across the view; set from and to";
        }
    }
    if (a > b) std::swap(a, b);
    if (!(b > a)) {
        st.error = "the range is empty";
        return;
    }
    const Rgba col = item_color(item);
    const int n = 600;
    std::vector<Vec2> low, high;
    for (int i = 0; i <= n; ++i) {
        const double t = a + (b - a) * i / n;
        const double lv = fl.at({ t });
        const double uv = fu.at({ t });
        low.push_back(in_y ? Vec2{ lv, t } : Vec2{ t, lv });
        high.push_back(in_y ? Vec2{ uv, t } : Vec2{ t, uv });
    }
    if (item.option("shade")) {
        const Rgba fill = soft(col, 0.26);
        for (int i = 0; i < n; ++i) {
            const Vec2 p0 = low[static_cast<std::size_t>(i)], p1 = low[static_cast<std::size_t>(i + 1)];
            const Vec2 q0 = high[static_cast<std::size_t>(i)], q1 = high[static_cast<std::size_t>(i + 1)];
            if (!finite(p0) || !finite(p1) || !finite(q0) || !finite(q1)) continue;
            c.scene.s2.tris.push_back(Tri2{ p0, p1, q1, fill });
            c.scene.s2.tris.push_back(Tri2{ p0, q1, q0, fill });
        }
    }
    if (item.option("boundary")) {
        stroke(c, low, col, 2.4f, false);
        stroke(c, high, col, 2.4f, false);
        stroke(c, { low.front(), high.front() }, col, 1.8f, false);
        stroke(c, { low.back(), high.back() }, col, 1.8f, false);
    }
    if (item.option("slices")) {
        const int k = 12;
        for (int i = 0; i < k; ++i) {
            const double t0 = a + (b - a) * i / k;
            const double t1 = a + (b - a) * (i + 1) / k;
            const double tm = 0.5 * (t0 + t1);
            const double lv = fl.at({ tm });
            const double uv = fu.at({ tm });
            if (!std::isfinite(lv) || !std::isfinite(uv)) continue;
            auto P = [&](double t, double v) { return in_y ? Vec2{ v, t } : Vec2{ t, v }; };
            stroke(c, { P(t0, lv), P(t1, lv), P(t1, uv), P(t0, uv), P(t0, lv) }, blend(c, col, 0.35), 1.2f, false);
        }
    }
    const Integral A = integrate_open([&](double t) { return std::fabs(fu.at({ t }) - fl.at({ t })); }, a, b, 1000);
    const Integral Mt = integrate_open([&](double t) { return t * (fu.at({ t }) - fl.at({ t })); }, a, b, 1000);
    const Integral Mo = integrate_open([&](double t) { const double u = fu.at({ t }), l = fl.at({ t }); return 0.5 * (u * u - l * l); }, a, b, 1000);
    const Integral S = integrate_open([&](double t) { return fu.at({ t }) - fl.at({ t }); }, a, b, 1000);
    std::string sym;
    if (symbols_wanted()) {
        const std::string diff = symbol_text(sub(upper, lower));
        if (!diff.empty()) sym = "\\int\\_{" + bound_symbol(a) + "}^{" + bound_symbol(b) + "} (" + diff + ") d" + var;
    }
    if (!A.ok) st.readouts.push_back({ "area", "undefined somewhere in the range", sym });
    else if (!A.finite) st.readouts.push_back({ "area", "\xE2\x88\x9E (the integral diverges)", sym });
    else {
        st.readouts.push_back({ "area", number_text(A.value), sym });
        if (S.ok && S.finite && std::fabs(S.value - A.value) > 1e-9 * std::max(1.0, A.value)) st.readouts.push_back({ "signed area (upper - lower)", number_text(S.value), std::string() });
        if (Mt.ok && Mo.ok && Mt.finite && Mo.finite && S.ok && std::fabs(S.value) > 1e-300) {
            const double scale = std::max({ 1.0, std::fabs(a), std::fabs(b) });
            double ct = Mt.value / S.value;
            double co = Mo.value / S.value;
            if (std::fabs(ct) < 1e-12 * scale) ct = 0.0;
            if (std::fabs(co) < 1e-12 * scale) co = 0.0;
            const Vec2 centroid = in_y ? Vec2{ co, ct } : Vec2{ ct, co };
            st.readouts.push_back({ "centroid", vec_text({ centroid.x, centroid.y }), std::string() });
            if (item.option("centroid") && finite(centroid)) c.scene.s2.dots.push_back(Dot2{ centroid, col, 5.0f, false, "" });
        }
    }
    st.readouts.push_back({ var + " from", format_double(a, 8) + " to " + format_double(b, 8), std::string() });
}

void build_polar_region(const Item& item, Ctx& c, ItemStatus& st) {
    std::string err;
    const ExprPtr inner = curve_of(item.field("eq"), "theta", "r", c.env, err);
    const ExprPtr outer = inner ? curve_of(item.field("upper"), "theta", "r", c.env, err) : nullptr;
    if (!inner || !outer) {
        st.error = err;
        return;
    }
    st.mathtext = to_mathtext(inner) + " \\leq\\ r \\leq\\ " + to_mathtext(outer);
    double a = 0.0;
    double b = k_tau;
    if (!bound_text_empty(item.field("min")) && !parse_number(item.field("min"), c.env, a, err)) {
        st.error = "\xCE\xB8 from: " + err;
        return;
    }
    if (!bound_text_empty(item.field("max")) && !parse_number(item.field("max"), c.env, b, err)) {
        st.error = "\xCE\xB8 to: " + err;
        return;
    }
    if (c.env.degrees) {
        a *= k_tau / 360.0;
        b *= k_tau / 360.0;
    }
    if (a > b) std::swap(a, b);
    Func fi(inner, { "theta" }, false, NumberType::Hardware);
    Func fo(outer, { "theta" }, false, NumberType::Hardware);
    if (!fi.ok() || !fo.ok()) {
        st.error = fi.ok() ? fo.error() : fi.error();
        return;
    }
    const Rgba col = item_color(item);
    const int n = 720;
    std::vector<Vec2> in_pts, out_pts;
    for (int i = 0; i <= n; ++i) {
        const double t = a + (b - a) * i / n;
        const double ri = fi.at({ t });
        const double ro = fo.at({ t });
        in_pts.push_back(Vec2{ ri * std::cos(t), ri * std::sin(t) });
        out_pts.push_back(Vec2{ ro * std::cos(t), ro * std::sin(t) });
    }
    if (item.option("shade")) {
        const Rgba fill = soft(col, 0.26);
        for (int i = 0; i < n; ++i) {
            const Vec2 p0 = in_pts[static_cast<std::size_t>(i)], p1 = in_pts[static_cast<std::size_t>(i + 1)];
            const Vec2 q0 = out_pts[static_cast<std::size_t>(i)], q1 = out_pts[static_cast<std::size_t>(i + 1)];
            if (!finite(p0) || !finite(p1) || !finite(q0) || !finite(q1)) continue;
            c.scene.s2.tris.push_back(Tri2{ p0, p1, q1, fill });
            c.scene.s2.tris.push_back(Tri2{ p0, q1, q0, fill });
        }
    }
    if (item.option("boundary")) {
        stroke(c, in_pts, col, 2.2f, false);
        stroke(c, out_pts, col, 2.4f, false);
        if (b - a < k_tau - 1e-9) {
            stroke(c, { in_pts.front(), out_pts.front() }, col, 1.8f, false);
            stroke(c, { in_pts.back(), out_pts.back() }, col, 1.8f, false);
        }
    }
    const Integral A = integrate_open([&](double t) { const double o = fo.at({ t }), i = fi.at({ t }); return 0.5 * std::fabs(o * o - i * i); }, a, b, 1000);
    std::string sym;
    if (symbols_wanted()) {
        const std::string o2 = symbol_text(pow(outer, number(2.0)));
        const std::string i2 = symbol_text(pow(inner, number(2.0)));
        if (!o2.empty() && !i2.empty()) sym = "{(1)/(2)} \\int\\_{" + bound_symbol(a) + "}^{" + bound_symbol(b) + "} (" + o2 + " - (" + i2 + ")) d\\theta\\";
    }
    if (!A.ok) st.readouts.push_back({ "area", "undefined somewhere in the range", sym });
    else st.readouts.push_back({ "area", A.finite ? number_text(A.value) : std::string("\xE2\x88\x9E"), sym });
}

void build_region3d(const Item& item, Ctx& c, ItemStatus& st) {
    const std::vector<std::string> vars = { "x", "y", "z", "r", "rho", "theta", "phi" };
    ParseResult r = parse_condition(join_commas(item.field("eq")), parse_options(c.env, vars));
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    std::vector<Condition> leaves;
    bool strict_all = true;
    std::string err;
    ExprPtr S0 = signed_region(r.statement.lhs, leaves, strict_all, err);
    if (!S0) {
        st.error = err;
        return;
    }
    st.mathtext = to_mathtext(r.statement.lhs);
    ExprPtr S = prepare(space_coordinates(S0), c.env, { "x", "y", "z" }, err);
    if (!S) {
        st.error = err;
        return;
    }
    const ExprPtr outside = neg(S);
    Func fo(outside, { "x", "y", "z" }, false, NumberType::Hardware);
    if (!fo.ok()) {
        st.error = fo.error();
        return;
    }
    const Rgba col = item_color(item);
    if (item.option("shade")) region_caps(c, fo, Relation::Less, soft(col, 0.9));
    double res = 44.0;
    parse_number(item.field("resolution"), c.env, res, err);
    const int n = static_cast<int>(std::clamp(res * (c.env.quality >= 3 ? 1.4 : c.env.quality <= 1 ? 0.7 : 1.0), 12.0, 100.0));
    Grid3 g;
    g.n = n;
    g.lo[0] = box_xlo(c);
    g.hi[0] = box_xhi(c);
    g.lo[1] = box_ylo(c);
    g.hi[1] = box_yhi(c);
    g.lo[2] = c.zmin;
    g.hi[2] = c.zmax;
    const std::size_t total = static_cast<std::size_t>(n) * n * n;
    std::vector<double> xs(total), ys(total), zs(total);
    for (int k = 0; k < n; ++k) {
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                const std::size_t idx = (static_cast<std::size_t>(k) * n + j) * n + i;
                const Vec3 p = g.pos(i, j, k);
                xs[idx] = p.x;
                ys[idx] = p.y;
                zs[idx] = p.z;
            }
        }
    }
    g.v = fo.map3(xs, ys, zs);
    std::size_t inside = 0;
    const int m = c.env.quality >= 3 ? 140 : 100;
    double cell = 0.0;
    {
        const std::size_t count = static_cast<std::size_t>(m) * m;
        std::vector<double> px(count), py(count), pz(count);
        const double dx = (g.hi[0] - g.lo[0]) / m, dy = (g.hi[1] - g.lo[1]) / m, dz = (g.hi[2] - g.lo[2]) / m;
        cell = dx * dy * dz;
        for (int k = 0; k < m; ++k) {
            for (int j = 0; j < m; ++j) {
                for (int i = 0; i < m; ++i) {
                    const std::size_t idx = static_cast<std::size_t>(j) * m + i;
                    px[idx] = g.lo[0] + dx * (i + 0.5);
                    py[idx] = g.lo[1] + dy * (j + 0.5);
                    pz[idx] = g.lo[2] + dz * (k + 0.5);
                }
            }
            const std::vector<double> v = fo.map3(px, py, pz);
            for (double x : v) inside += std::isfinite(x) && x < 0.0 ? 1 : 0;
            if (build_cancelled()) break;
        }
    }
    if (item.option("boundary")) {
        std::vector<Vec3> tris;
        marching_tetrahedra(g, tris);
        if (!tris.empty()) {
            Mesh3 mesh;
            mesh.pos = tris;
            for (std::uint32_t i = 0; i < tris.size(); ++i) mesh.tri.push_back(i);
            finish_normals(mesh);
            mesh.col.assign(mesh.pos.size(), col);
            mesh.alpha = item.option("shade") ? 0.55 : 0.85;
            c.scene.s3.meshes.push_back(std::move(mesh));
        }
    }
    if (inside == 0) st.note = "no part of this region is in the box";
    st.readouts.push_back({ "volume in the box \xE2\x89\x88", number_text(static_cast<double>(inside) * cell), std::string() });
    st.readouts.push_back({ "conditions", std::to_string(leaves.size()), std::string() });
}

void build_between3d(const Item& item, Ctx& c, ItemStatus& st) {
    std::string err;
    auto surface_of = [&](const std::string& text, const char* label) -> ExprPtr {
        ParseResult r = parse_field(text, c.env, { "x", "y", "z" });
        if (!r.ok) {
            err = std::string(label) + ": " + r.error;
            return nullptr;
        }
        ExprPtr raw = r.statement.lhs;
        if (r.statement.relation == Relation::Equal && raw && raw->op == Op::Variable && raw->text == "z") raw = r.statement.rhs;
        else if (r.statement.relation != Relation::None) {
            err = std::string(label) + ": write z = f(x, y) or just f(x, y)";
            return nullptr;
        }
        if (depends_on(raw, "z")) {
            err = std::string(label) + ": write it as a function of x and y";
            return nullptr;
        }
        return prepare(raw, c.env, { "x", "y" }, err);
    };
    auto bound_of = [&](const std::string& text, const char* label) -> ExprPtr {
        ParseResult r = parse_field(text, c.env, { "x" });
        if (!r.ok) {
            err = std::string(label) + ": " + r.error;
            return nullptr;
        }
        ExprPtr raw = r.statement.lhs;
        if (r.statement.relation == Relation::Equal && raw && raw->op == Op::Variable && raw->text == "y") raw = r.statement.rhs;
        if (depends_on(raw, "y")) {
            err = std::string(label) + ": the y limits can use x but not y";
            return nullptr;
        }
        return prepare(raw, c.env, { "x" }, err);
    };
    const ExprPtr bottom = surface_of(item.field("eq"), "bottom");
    const ExprPtr top = bottom ? surface_of(item.field("upper"), "top") : nullptr;
    const ExprPtr ylow = top ? bound_of(item.field("ymin"), "y from") : nullptr;
    const ExprPtr yhigh = ylow ? bound_of(item.field("ymax"), "y to") : nullptr;
    if (!bottom || !top || !ylow || !yhigh) {
        st.error = err;
        return;
    }
    double a = 0.0, b = 1.0;
    if (!parse_number(item.field("xmin"), c.env, a, err) || !parse_number(item.field("xmax"), c.env, b, err)) {
        st.error = "x: " + err;
        return;
    }
    if (a > b) std::swap(a, b);
    st.mathtext = to_mathtext(bottom) + " \\leq\\ z \\leq\\ " + to_mathtext(top);
    Func fb(bottom, { "x", "y" }, false, NumberType::Hardware);
    Func ft(top, { "x", "y" }, false, NumberType::Hardware);
    Func gl(ylow, { "x" }, false, NumberType::Hardware);
    Func gh(yhigh, { "x" }, false, NumberType::Hardware);
    for (Func* f : { &fb, &ft, &gl, &gh }) {
        if (!f->ok()) {
            st.error = f->error();
            return;
        }
    }
    const Rgba col = item_color(item);
    const int nu = c.env.quality >= 3 ? 70 : 46;
    const int nv = nu;
    auto base_point = [&](double u, double v) {
        const double x = a + (b - a) * u;
        const double y0 = gl.at({ x });
        const double y1 = gh.at({ x });
        return Vec2{ x, y0 + (y1 - y0) * v };
    };
    const bool lines = item.option("mesh");
    for (int which = 0; which < 2; ++which) {
        if (which == 0 ? !item.option("bottom") : !item.option("top")) continue;
        Func& f = which == 0 ? fb : ft;
        SurfaceGrid s;
        s.nu = nu;
        s.nv = nv;
        for (int j = 0; j < nv; ++j) {
            for (int i = 0; i < nu; ++i) {
                const Vec2 p = base_point(static_cast<double>(i) / (nu - 1), static_cast<double>(j) / (nv - 1));
                s.pts.push_back(Vec3{ p.x, p.y, f.at({ p.x, p.y }) });
            }
        }
        add_sheet(c, s, which == 0 ? blend(c, col, 0.25) : col, which == 0 ? 0.75 : 0.9, lines);
    }
    if (item.option("walls")) {
        const int m = 24;
        for (int side = 0; side < 4; ++side) {
            SurfaceGrid s;
            s.nu = side < 2 ? nu : nv;
            s.nv = m;
            for (int j = 0; j < m; ++j) {
                const double w = static_cast<double>(j) / (m - 1);
                for (int i = 0; i < s.nu; ++i) {
                    const double t = static_cast<double>(i) / (s.nu - 1);
                    const Vec2 p = side == 0 ? base_point(t, 0.0) : side == 1 ? base_point(t, 1.0) : side == 2 ? base_point(0.0, t) : base_point(1.0, t);
                    const double z0 = fb.at({ p.x, p.y });
                    const double z1 = ft.at({ p.x, p.y });
                    s.pts.push_back(Vec3{ p.x, p.y, z0 + (z1 - z0) * w });
                }
            }
            add_sheet(c, s, soft(col, 0.85), 0.4, false);
        }
    }
    if (item.option("base")) {
        Line3 l;
        l.color = with_alpha(col, 0.8);
        l.width = 1.8f;
        l.dashed = true;
        for (int i = 0; i < nu; ++i) {
            const Vec2 p = base_point(static_cast<double>(i) / (nu - 1), 0.0);
            l.pts.push_back(Vec3{ p.x, p.y, c.zmin });
        }
        for (int i = nu - 1; i >= 0; --i) {
            const Vec2 p = base_point(static_cast<double>(i) / (nu - 1), 1.0);
            l.pts.push_back(Vec3{ p.x, p.y, c.zmin });
        }
        if (!l.pts.empty()) l.pts.push_back(l.pts.front());
        c.scene.s3.lines.push_back(std::move(l));
    }
    const Integral V = integrate_open([&](double x) {
        const double y0 = gl.at({ x });
        const double y1 = gh.at({ x });
        if (!std::isfinite(y0) || !std::isfinite(y1)) return k_nan;
        static const double gx[5] = { -0.9061798459386640, -0.5384693101056831, 0.0, 0.5384693101056831, 0.9061798459386640 };
        static const double gw[5] = { 0.2369268850561891, 0.4786286704993665, 0.5688888888888889, 0.4786286704993665, 0.2369268850561891 };
        const int panels = 40;
        const double h = (y1 - y0) / panels;
        double sum = 0.0;
        for (int p = 0; p < panels; ++p) {
            const double mid = y0 + h * (p + 0.5);
            for (int q = 0; q < 5; ++q) {
                const double y = mid + 0.5 * h * gx[q];
                sum += gw[q] * (ft.at({ x, y }) - fb.at({ x, y }));
            }
        }
        return sum * 0.5 * h;
    }, a, b, 200);
    std::string sym;
    if (symbols_wanted()) {
        const std::string diff = symbol_text(sub(top, bottom));
        const std::string y0 = symbol_text(ylow);
        const std::string y1 = symbol_text(yhigh);
        if (!diff.empty() && !y0.empty() && !y1.empty()) sym = "\\int\\_{" + bound_symbol(a) + "}^{" + bound_symbol(b) + "} \\int\\_{" + y0 + "}^{" + y1 + "} (" + diff + ") dy dx";
    }
    if (!V.ok) st.readouts.push_back({ "volume", "undefined somewhere in the region", sym });
    else st.readouts.push_back({ "volume", V.finite ? number_text(V.value) : std::string("\xE2\x88\x9E"), sym });
}

} // namespace build
} // namespace arbor
