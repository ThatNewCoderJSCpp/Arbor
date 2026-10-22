#include "build_common.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace arbor {
namespace build {

namespace {

const double k_nan = std::nan("");

bool is_var(const ExprPtr& e, const char* name) {
    return e && e->op == Op::Variable && e->text == name;
}

int grid_size(const Ctx& c, int low, int mid, int high) {
    return c.env.quality <= 1 ? low : c.env.quality >= 3 ? high : mid;
}

double xlo(const Ctx& c) { return c.view3.target.x - c.range; }
double xhi(const Ctx& c) { return c.view3.target.x + c.range; }
double ylo(const Ctx& c) { return c.view3.target.y - c.range_y; }
double yhi(const Ctx& c) { return c.view3.target.y + c.range_y; }

Rgba height_color(const Ctx& c, double z) {
    const double t = c.zmax > c.zmin ? (z - c.zmin) / (c.zmax - c.zmin) : 0.5;
    return colormap("viridis", 0.08 + 0.9 * t);
}

void color_mesh(Ctx& c, Mesh3& mesh, const Item& item, bool by_height) {
    const Rgba base = item_color(item);
    mesh.col.resize(mesh.pos.size());
    for (std::size_t i = 0; i < mesh.pos.size(); ++i) mesh.col[i] = by_height && finite(mesh.pos[i]) ? height_color(c, mesh.pos[i].z) : base;
}

void add_quad(Ctx& c, Vec3 center, Vec3 a, Vec3 b, Rgba color, double alpha) {
    Mesh3 m;
    m.pos = { center - a - b, center + a - b, center + a + b, center - a + b };
    const Vec3 n = normalized(cross(a, b));
    m.nrm = { n, n, n, n };
    m.col = { color, color, color, color };
    m.tri = { 0, 1, 2, 0, 2, 3 };
    m.alpha = alpha;
    m.lit = false;
    c.scene.s3.meshes.push_back(std::move(m));
}

std::string vtext(const std::vector<ExprPtr>& es, const std::vector<std::string>& vars, const std::vector<double>& at, Env& env) {
    std::string s = "(";
    for (std::size_t i = 0; i < es.size(); ++i) {
        if (i) s += ", ";
        s += es[i] ? value_text(es[i], vars, at, env) : std::string("?");
    }
    return s + ")";
}

struct ParamSurface {
    ExprPtr x;
    ExprPtr y;
    ExprPtr z;
    std::string u;
    std::string v;
    double u0 = 0.0;
    double u1 = 1.0;
    double v0 = 0.0;
    double v1 = 1.0;
};

void build_param(const ParamSurface& ps, const Item& item, Ctx& c, ItemStatus& st) {
    Func fx(ps.x, { ps.u, ps.v }, false, c.env.geo_type);
    Func fy(ps.y, { ps.u, ps.v }, false, c.env.geo_type);
    Func fz(ps.z, { ps.u, ps.v }, false, c.env.geo_type);
    if (!fx.ok() || !fy.ok() || !fz.ok()) {
        st.error = !fx.ok() ? fx.error() : !fy.ok() ? fy.error() : fz.error();
        return;
    }
    const int n = grid_size(c, 40, 72, 120);
    Mesh3 mesh;
    std::vector<Vec3> grid;
    surface_mesh(c, [&](const std::vector<double>& us, const std::vector<double>& vs, std::vector<Vec3>& out) {
        const std::vector<double> a = fx.map2(us, vs);
        const std::vector<double> b = fy.map2(us, vs);
        const std::vector<double> d2 = fz.map2(us, vs);
        out.resize(us.size());
        for (std::size_t i = 0; i < us.size(); ++i) out[i] = Vec3{ a[i], b[i], d2[i] };
    }, ps.u0, ps.u1, ps.v0, ps.v1, n, n, mesh, grid);
    color_mesh(c, mesh, item, item.option("height_color"));
    if (item.option("gauss")) {
        const ExprPtr xu = d(ps.x, ps.u), yu = d(ps.y, ps.u), zu = d(ps.z, ps.u);
        const ExprPtr xv = d(ps.x, ps.v), yv = d(ps.y, ps.v), zv = d(ps.z, ps.v);
        if (xu && yu && zu && xv && yv && zv) {
            const ExprPtr xuu = d(xu, ps.u), yuu = d(yu, ps.u), zuu = d(zu, ps.u);
            const ExprPtr xuv = d(xu, ps.v), yuv = d(yu, ps.v), zuv = d(zu, ps.v);
            const ExprPtr xvv = d(xv, ps.v), yvv = d(yv, ps.v), zvv = d(zv, ps.v);
            if (xuu && yuu && zuu && xuv && yuv && zuv && xvv && yvv && zvv) {
                std::vector<double> us(grid.size());
                std::vector<double> vs(grid.size());
                for (int j = 0; j < n; ++j) {
                    for (int i = 0; i < n; ++i) {
                        us[static_cast<std::size_t>(j) * n + i] = ps.u0 + (ps.u1 - ps.u0) * i / (n - 1);
                        vs[static_cast<std::size_t>(j) * n + i] = ps.v0 + (ps.v1 - ps.v0) * j / (n - 1);
                    }
                }
                auto ev = [&](const ExprPtr& e) { Func f(e, { ps.u, ps.v }, false, NumberType::Hardware); return f.map2(us, vs); };
                const auto Xu = ev(xu), Yu = ev(yu), Zu = ev(zu), Xv = ev(xv), Yv = ev(yv), Zv = ev(zv);
                const auto Xuu = ev(xuu), Yuu = ev(yuu), Zuu = ev(zuu), Xuv = ev(xuv), Yuv = ev(yuv), Zuv = ev(zuv), Xvv = ev(xvv), Yvv = ev(yvv), Zvv = ev(zvv);
                std::vector<double> K(grid.size());
                for (std::size_t i = 0; i < grid.size(); ++i) {
                    const Vec3 ru{ Xu[i], Yu[i], Zu[i] };
                    const Vec3 rv{ Xv[i], Yv[i], Zv[i] };
                    const Vec3 nn = normalized(cross(ru, rv));
                    const double E = dot(ru, ru), F = dot(ru, rv), G = dot(rv, rv);
                    const double L = dot(Vec3{ Xuu[i], Yuu[i], Zuu[i] }, nn);
                    const double M = dot(Vec3{ Xuv[i], Yuv[i], Zuv[i] }, nn);
                    const double N = dot(Vec3{ Xvv[i], Yvv[i], Zvv[i] }, nn);
                    const double den = E * G - F * F;
                    K[i] = den != 0.0 ? (L * N - M * M) / den : k_nan;
                }
                std::vector<double> mags;
                for (double k : K) mags.push_back(std::fabs(k));
                const double top = std::max(1e-12, robust_max(mags));
                for (std::size_t i = 0; i < K.size() && i < mesh.col.size(); ++i) mesh.col[i] = colormap("coolwarm", 0.5 + 0.5 * std::clamp(K[i] / top, -1.0, 1.0));
                st.legend_min = number_text(-top);
                st.legend_max = number_text(top);
                st.legend_map = "coolwarm";
                st.note = "Colored by Gaussian curvature K: red is positive (sphere-like), blue negative (saddle-like).";
            }
        }
    }
    if (mesh.tri.empty()) st.note = "no part of this surface is in the box";
    c.scene.s3.meshes.push_back(std::move(mesh));
    if (item.option("mesh")) mesh_lines(c, grid, n, n, std::max(1, n / 16), with_alpha(mix(item_color(item), rgba(0, 0, 0), 0.45), 0.55 * 255 / 255.0 > 0 ? 140 : 140));
    if (item.option("normals")) {
        const int every = std::max(1, n / 9);
        for (int j = every / 2; j < n; j += every) {
            for (int i = every / 2; i < n; i += every) {
                const std::size_t k = static_cast<std::size_t>(j) * n + i;
                const Vec3 p = grid[k];
                if (!finite(p)) continue;
                Vec3 du = grid[static_cast<std::size_t>(j) * n + std::min(n - 1, i + 1)] - grid[static_cast<std::size_t>(j) * n + std::max(0, i - 1)];
                Vec3 dv = grid[static_cast<std::size_t>(std::min(n - 1, j + 1)) * n + i] - grid[static_cast<std::size_t>(std::max(0, j - 1)) * n + i];
                const Vec3 nn = normalized(cross(du, dv));
                if (!finite(nn) || length(nn) == 0.0) continue;
                c.scene.s3.arrows.push_back(Arrow3{ p, p + nn * (c.range * 0.22), c.env.theme.accent, 1.8f, "" });
            }
        }
    }
}

} // namespace

void build_surface(const Item& item, Ctx& c, ItemStatus& st) {
    ParseResult r = parse_field(item.field("eq"), c.env, { "x", "y", "z" });
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    const Statement& s = r.statement;
    ExprPtr raw;
    if (s.relation == Relation::None) raw = s.lhs;
    else if (s.relation == Relation::Equal && is_var(s.lhs, "z") && !depends_on(s.rhs, "z")) raw = s.rhs;
    else if (s.relation == Relation::Equal && is_var(s.rhs, "z") && !depends_on(s.lhs, "z")) raw = s.lhs;
    else {
        build_implicit3d(item, c, st);
        return;
    }
    if (depends_on(raw, "z")) {
        build_implicit3d(item, c, st);
        return;
    }
    std::string err;
    ExprPtr F = prepare(raw, c.env, { "x", "y" }, err);
    if (!F) {
        st.error = err;
        return;
    }
    st.mathtext = "z = " + to_mathtext(F);
    const ExprPtr Fd = restrict_to_limits(F, item, { "x", "y" }, c.env, err);
    if (!Fd) {
        st.error = err;
        return;
    }
    Limits lx, ly;
    read_limits(item, "xmin", "xmax", c.env, lx, err);
    read_limits(item, "ymin", "ymax", c.env, ly, err);
    const double sx0 = lx.lo.set ? std::max(xlo(c), lx.lo.value) : xlo(c);
    const double sx1 = lx.hi.set ? std::min(xhi(c), lx.hi.value) : xhi(c);
    const double sy0 = ly.lo.set ? std::max(ylo(c), ly.lo.value) : ylo(c);
    const double sy1 = ly.hi.set ? std::min(yhi(c), ly.hi.value) : yhi(c);
    if (!(sx1 > sx0) || !(sy1 > sy0)) {
        st.note = "the domain is outside the box";
        return;
    }
    Func fz(Fd, { "x", "y" }, false, c.env.geo_type);
    if (!fz.ok()) {
        st.error = fz.error();
        return;
    }
    const int n = grid_size(c, 40, 80, 130);
    Mesh3 mesh;
    std::vector<Vec3> grid;
    surface_mesh(c, [&](const std::vector<double>& us, const std::vector<double>& vs, std::vector<Vec3>& out) {
        const std::vector<double> zs = fz.map2(us, vs);
        out.resize(us.size());
        for (std::size_t i = 0; i < us.size(); ++i) out[i] = Vec3{ us[i], vs[i], zs[i] };
    }, sx0, sx1, sy0, sy1, n, n, mesh, grid);
    color_mesh(c, mesh, item, item.option("height_color"));
    if (item.option("height_color")) {
        st.legend_min = number_text(c.zmin);
        st.legend_max = number_text(c.zmax);
        st.legend_map = "viridis";
    }
    if (item.option("plane")) mesh.alpha = 0.92;
    if (mesh.tri.empty()) st.note = "no part of this surface is in the box";
    c.scene.s3.meshes.push_back(std::move(mesh));
    const Rgba col = item_color(item);
    if (item.option("mesh")) mesh_lines(c, grid, n, n, std::max(1, n / 16), with_alpha(rgba(0, 0, 0), 0.32));

    if (item.option("contours") || item.option("floor")) {
        Grid2 g;
        g.nx = n;
        g.ny = n;
        g.x0 = xlo(c);
        g.x1 = xhi(c);
        g.y0 = ylo(c);
        g.y1 = yhi(c);
        g.v.resize(grid.size());
        for (std::size_t i = 0; i < grid.size(); ++i) g.v[i] = grid[i].z;
        const int levels = 14;
        for (int k = 1; k < levels; ++k) {
            const double level = c.zmin + (c.zmax - c.zmin) * k / levels;
            const std::vector<Vec2> seg = marching_squares(g, level, nullptr);
            if (item.option("contours")) {
                Line3 l;
                l.color = with_alpha(c.env.theme.text, 0.75);
                l.width = 1.3f;
                for (const Vec2& p : seg) l.pts.push_back(finite(p) ? Vec3{ p.x, p.y, level } : Vec3{ k_nan, k_nan, k_nan });
                c.scene.s3.lines.push_back(std::move(l));
            }
            if (item.option("floor")) {
                Line3 l;
                l.color = height_color(c, level);
                l.width = 1.6f;
                for (const Vec2& p : seg) l.pts.push_back(finite(p) ? Vec3{ p.x, p.y, c.zmin } : Vec3{ k_nan, k_nan, k_nan });
                c.scene.s3.lines.push_back(std::move(l));
            }
        }
    }
    const ExprPtr fx = d(F, "x");
    const ExprPtr fy = d(F, "y");
    if (item.option("traces")) {
        double cx = 0.0;
        double cy = 0.0;
        std::string e2;
        const std::vector<double> ts = linspace(-c.range_y, c.range_y, 300);
        const std::vector<double> txs = linspace(-c.range, c.range, 300);
        if (parse_number(item.field("cx"), c.env, cx, e2)) {
            std::vector<double> xs(ts.size(), cx);
            std::vector<double> ys(ts.size());
            for (std::size_t i = 0; i < ts.size(); ++i) ys[i] = c.view3.target.y + ts[i];
            const std::vector<double> zs = fz.map2(xs, ys);
            Line3 l;
            l.color = rgba(240, 140, 46);
            l.width = 3.4f;
            for (std::size_t i = 0; i < ts.size(); ++i) l.pts.push_back(std::isfinite(zs[i]) && zs[i] >= c.zmin && zs[i] <= c.zmax ? Vec3{ cx, ys[i], zs[i] } : Vec3{ k_nan, k_nan, k_nan });
            c.scene.s3.lines.push_back(std::move(l));
            add_quad(c, Vec3{ cx, c.view3.target.y, 0.5 * (c.zmin + c.zmax) }, Vec3{ 0.0, c.range_y, 0.0 }, Vec3{ 0.0, 0.0, 0.5 * (c.zmax - c.zmin) }, rgba(240, 140, 46), 0.16);
            st.readouts.push_back(formula_readout("trace x = " + format_double(cx, 5), "z = " + symbol_text(substitute(F, { { "x", number(cx) } }))));
        }
        if (parse_number(item.field("cy"), c.env, cy, e2)) {
            std::vector<double> ys(ts.size(), cy);
            std::vector<double> xs(ts.size());
            for (std::size_t i = 0; i < ts.size(); ++i) xs[i] = c.view3.target.x + txs[i];
            const std::vector<double> zs = fz.map2(xs, ys);
            Line3 l;
            l.color = rgba(217, 72, 143);
            l.width = 3.4f;
            for (std::size_t i = 0; i < ts.size(); ++i) l.pts.push_back(std::isfinite(zs[i]) && zs[i] >= c.zmin && zs[i] <= c.zmax ? Vec3{ xs[i], cy, zs[i] } : Vec3{ k_nan, k_nan, k_nan });
            c.scene.s3.lines.push_back(std::move(l));
            add_quad(c, Vec3{ c.view3.target.x, cy, 0.5 * (c.zmin + c.zmax) }, Vec3{ c.range, 0.0, 0.0 }, Vec3{ 0.0, 0.0, 0.5 * (c.zmax - c.zmin) }, rgba(217, 72, 143), 0.16);
            st.readouts.push_back(formula_readout("trace y = " + format_double(cy, 5), "z = " + symbol_text(substitute(F, { { "y", number(cy) } }))));
        }
    }
    if (fx && fy && (item.option("normals") || item.option("gradient"))) {
        Func gx(fx, { "x", "y" }, false, NumberType::Hardware);
        Func gy(fy, { "x", "y" }, false, NumberType::Hardware);
        const int every = std::max(1, n / 10);
        double top = 1e-12;
        std::vector<std::pair<Vec3, Vec2>> grads;
        for (int j = every / 2; j < n; j += every) {
            for (int i = every / 2; i < n; i += every) {
                const Vec3 p = grid[static_cast<std::size_t>(j) * n + i];
                if (!finite(p)) continue;
                const Vec2 gr{ gx.at({ p.x, p.y }), gy.at({ p.x, p.y }) };
                if (!finite(gr)) continue;
                if (item.option("normals") && p.z >= c.zmin && p.z <= c.zmax) {
                    const Vec3 nn = normalized(Vec3{ -gr.x, -gr.y, 1.0 });
                    c.scene.s3.arrows.push_back(Arrow3{ p, p + nn * (c.range * 0.2), c.env.theme.accent, 1.8f, "" });
                }
                grads.push_back({ p, gr });
                top = std::max(top, length(gr));
            }
        }
        if (item.option("gradient")) {
            const double spacing = 2.0 * c.range / (n / every);
            for (const auto& [p, gr] : grads) {
                if (length(gr) == 0.0) continue;
                const Vec2 u = normalized(gr) * (0.7 * spacing);
                const Vec3 base{ p.x, p.y, c.zmin };
                c.scene.s3.arrows.push_back(Arrow3{ base, base + Vec3{ u.x, u.y, 0.0 }, colormap("viridis", 0.2 + 0.8 * length(gr) / top), 1.6f, "" });
            }
        }
    }
    double x0 = 0.0;
    double y0 = 0.0;
    std::string e3;
    if (parse_number(item.field("x0"), c.env, x0, e3) && parse_number(item.field("y0"), c.env, y0, e3)) {
        const double z0 = fz.at({ x0, y0 });
        const std::vector<std::string> xy = { "x", "y" };
        st.readouts.push_back(sym_readout("f(x\xE2\x82\x80, y\xE2\x82\x80)", F, xy, { x0, y0 }, c.env));
        if (fx && fy) {
            st.readouts.push_back(sym_readout("\xE2\x88\x82" "f/\xE2\x88\x82x", fx, xy, { x0, y0 }, c.env));
            st.readouts.push_back(sym_readout("\xE2\x88\x82" "f/\xE2\x88\x82y", fy, xy, { x0, y0 }, c.env));
            st.readouts.push_back(sym_readout("|\xE2\x88\x87" "f|", simplify(call("sqrt", { add(pow(fx, number(2.0)), pow(fy, number(2.0))) })), xy, { x0, y0 }, c.env));
            const ExprPtr fxx = d(fx, "x");
            const ExprPtr fyy = d(fy, "y");
            const ExprPtr fxy = d(fx, "y");
            if (fxx && fyy && fxy) {
                const ExprPtr g = add(add(number(1.0), pow(fx, number(2.0))), pow(fy, number(2.0)));
                const ExprPtr K = simplify(div(sub(mul(fxx, fyy), pow(fxy, number(2.0))), pow(g, number(2.0))));
                const ExprPtr H = simplify(div(add(sub(mul(add(number(1.0), pow(fx, number(2.0))), fyy), mul(mul(number(2.0), mul(fx, fy)), fxy)), mul(add(number(1.0), pow(fy, number(2.0))), fxx)), mul(number(2.0), pow(g, number(1.5)))));
                st.readouts.push_back(sym_readout("Gaussian curvature K", K, xy, { x0, y0 }, c.env));
                st.readouts.push_back(sym_readout("mean curvature H", H, xy, { x0, y0 }, c.env));
                st.readouts.push_back(sym_readout("Hessian det", simplify(sub(mul(fxx, fyy), pow(fxy, number(2.0)))), xy, { x0, y0 }, c.env));
            }
            if (item.option("plane") && std::isfinite(z0)) {
                Func gx(fx, xy, false, NumberType::Hardware);
                Func gy(fy, xy, false, NumberType::Hardware);
                const double a = gx.at({ x0, y0 });
                const double b = gy.at({ x0, y0 });
                if (std::isfinite(a) && std::isfinite(b)) {
                    const double s2 = c.range * 0.35;
                    add_quad(c, Vec3{ x0, y0, z0 }, Vec3{ s2, 0.0, a * s2 }, Vec3{ 0.0, s2, b * s2 }, c.env.theme.accent, 0.4);
                    const Vec3 nn = normalized(Vec3{ -a, -b, 1.0 });
                    c.scene.s3.arrows.push_back(Arrow3{ Vec3{ x0, y0, z0 }, Vec3{ x0, y0, z0 } + nn * (c.range * 0.35), c.env.theme.accent, 2.4f, "n" });
                    st.readouts.push_back({ "tangent plane", "z = " + format_double(z0, 5) + " + " + format_double(a, 5) + "(x - " + format_double(x0, 4) + ") + " + format_double(b, 5) + "(y - " + format_double(y0, 4) + ")" });
                }
            }
        }
        if (std::isfinite(z0) && (item.option("plane") || item.option("traces"))) c.scene.s3.dots.push_back(Dot3{ Vec3{ x0, y0, z0 }, col, 6.0f, "" });
    }
}

bool inside_relation(Relation rel, double v) {
    switch (rel) {
        case Relation::Less: return v < 0.0;
        case Relation::LessEqual: return v <= 0.0;
        case Relation::Greater: return v > 0.0;
        case Relation::GreaterEqual: return v >= 0.0;
        default: return false;
    }
}

void region_caps(Ctx& c, Func& f, Relation rel, Rgba color) {
    const int m = c.env.quality >= 3 ? 60 : 36;
    const double lo[3] = { xlo(c), ylo(c), c.zmin };
    const double hi[3] = { xhi(c), yhi(c), c.zmax };
    Mesh3 caps;
    caps.alpha = 0.32;
    caps.lit = false;
    for (int axis = 0; axis < 3; ++axis) {
        const int a = (axis + 1) % 3;
        const int b = (axis + 2) % 3;
        for (int side = 0; side < 2; ++side) {
            const double fixed = side ? hi[axis] : lo[axis];
            std::vector<double> xs, ys, zs;
            for (int j = 0; j <= m; ++j) {
                for (int i = 0; i <= m; ++i) {
                    double p[3];
                    p[axis] = fixed;
                    p[a] = lo[a] + (hi[a] - lo[a]) * i / m;
                    p[b] = lo[b] + (hi[b] - lo[b]) * j / m;
                    xs.push_back(p[0]);
                    ys.push_back(p[1]);
                    zs.push_back(p[2]);
                }
            }
            const std::vector<double> v = f.map3(xs, ys, zs);
            auto at = [&](int i, int j) { return static_cast<std::size_t>(j) * (m + 1) + i; };
            for (int j = 0; j < m; ++j) {
                for (int i = 0; i < m; ++i) {
                    const std::size_t q[4] = { at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1) };
                    bool all = true;
                    for (std::size_t k : q) all = all && std::isfinite(v[k]) && inside_relation(rel, v[k]);
                    if (!all) continue;
                    const std::uint32_t base = static_cast<std::uint32_t>(caps.pos.size());
                    for (std::size_t k : q) {
                        caps.pos.push_back(Vec3{ xs[k], ys[k], zs[k] });
                        Vec3 nn{ 0, 0, 0 };
                        (axis == 0 ? nn.x : axis == 1 ? nn.y : nn.z) = side ? 1.0 : -1.0;
                        caps.nrm.push_back(nn);
                        caps.col.push_back(color);
                    }
                    caps.tri.insert(caps.tri.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
                }
            }
        }
    }
    if (!caps.tri.empty()) c.scene.s3.meshes.push_back(std::move(caps));
}

void implicit_in_coordinates(const Item& item, Ctx& c, ItemStatus& st, const ExprPtr& F, const std::vector<std::string>& vars, const double lo[3], const double hi[3], const std::function<Vec3(double, double, double)>& place, Relation rel, const ExprPtr& cartesian = nullptr) {
    const bool region = rel != Relation::None && rel != Relation::Equal;
    if (region && cartesian) {
        Func walls(cartesian, { "x", "y", "z" }, false, NumberType::Hardware);
        if (walls.ok()) region_caps(c, walls, rel, soft(item_color(item), 0.9));
    }
    Func fn(F, vars, false, c.env.geo_type);
    if (!fn.ok()) {
        st.error = fn.error();
        return;
    }
    std::string err;
    double res = 44.0;
    parse_number(item.field("resolution"), c.env, res, err);
    const int n = static_cast<int>(std::clamp(res * (c.env.quality >= 3 ? 1.4 : c.env.quality <= 1 ? 0.7 : 1.0), 12.0, 100.0));
    Grid3 g;
    g.n = n;
    for (int a = 0; a < 3; ++a) {
        g.lo[a] = lo[a];
        g.hi[a] = hi[a];
    }
    const std::size_t total = static_cast<std::size_t>(n) * n * n;
    std::vector<double> us(total), vs(total), ws(total);
    for (int k = 0; k < n; ++k) {
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                const std::size_t idx = (static_cast<std::size_t>(k) * n + j) * n + i;
                const Vec3 p = g.pos(i, j, k);
                us[idx] = p.x;
                vs[idx] = p.y;
                ws[idx] = p.z;
            }
        }
    }
    g.v = fn.map3(us, vs, ws);
    std::vector<Vec3> tris;
    marching_tetrahedra(g, tris);
    Mesh3 mesh;
    for (const Vec3& t : tris) {
        const Vec3 p = place(t.x, t.y, t.z);
        mesh.pos.push_back(p);
    }
    for (std::uint32_t i = 0; i < mesh.pos.size(); ++i) mesh.tri.push_back(i);
    if (mesh.tri.empty()) {
        st.note = "no part of this surface is in the box";
        return;
    }
    finish_normals(mesh);
    color_mesh(c, mesh, item, item.option("height_color"));
    const bool inequality = rel != Relation::None && rel != Relation::Equal;
    if (inequality) {
        mesh.alpha = 0.45;
        st.note = cartesian ? "the region is shaded on the walls of the box; the surface is its edge" : "the shaded surface is the edge of the region";
    }
    c.scene.s3.meshes.push_back(std::move(mesh));
    st.readouts.push_back({ "triangles", std::to_string(tris.size() / 3) });
}

void build_spherical_implicit(const Item& item, Ctx& c, ItemStatus& st, const Statement& s) {
    std::string err;
    ExprPtr F = prepare(sub(s.lhs, s.rhs ? s.rhs : number(0.0)), c.env, { "rho", "theta", "phi" }, err);
    if (!F) {
        st.error = err;
        return;
    }
    st.mathtext = to_mathtext(s.lhs) + " " + (s.relation == Relation::None ? "=" : relation_text(s.relation)) + " " + (s.rhs ? to_mathtext(s.rhs) : std::string("0"));
    double reach = 0.0;
    for (double x : { xlo(c), xhi(c) }) {
        for (double y : { ylo(c), yhi(c) }) {
            for (double z : { c.zmin, c.zmax }) reach = std::max(reach, std::sqrt(x * x + y * y + z * z));
        }
    }
    const double lo[3] = { 0.0, 0.0, 0.0 };
    const double hi[3] = { reach, 6.283185307179586, 3.141592653589793 };
    implicit_in_coordinates(item, c, st, F, { "rho", "theta", "phi" }, lo, hi, [](double rho, double th, double ph) {
        return Vec3{ rho * std::sin(ph) * std::cos(th), rho * std::sin(ph) * std::sin(th), rho * std::cos(ph) };
    }, s.relation);
}

char cylinder_axis(const Item& item) {
    std::string a;
    for (char ch : item.field("axis")) {
        if (!std::isspace(static_cast<unsigned char>(ch))) a += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (a == "x" || a == "x-axis" || a == "xaxis") return 'x';
    if (a == "y" || a == "y-axis" || a == "yaxis") return 'y';
    return 'z';
}

Vec3 cylinder_point(char axis, double r, double th, double h) {
    if (axis == 'x') return Vec3{ h, r * std::cos(th), r * std::sin(th) };
    if (axis == 'y') return Vec3{ r * std::sin(th), h, r * std::cos(th) };
    return Vec3{ r * std::cos(th), r * std::sin(th), h };
}

ExprPtr cylinder_to_cartesian(const ExprPtr& F, char axis) {
    const ExprPtr x = variable("x");
    const ExprPtr y = variable("y");
    const ExprPtr z = variable("z");
    const ExprPtr a = axis == 'x' ? y : axis == 'y' ? z : x;
    const ExprPtr b = axis == 'x' ? z : axis == 'y' ? x : y;
    const ExprPtr r = call("sqrt", { add(pow(a, number(2.0)), pow(b, number(2.0))) });
    std::map<std::string, ExprPtr> with = { { "r", r }, { "theta", call("atan2", { b, a }) } };
    return substitute(F, with);
}

ExprPtr mixed_to_cylinder(const ExprPtr& F, char axis) {
    const ExprPtr r = variable("r");
    const ExprPtr h = variable(std::string(1, axis));
    return substitute(F, { { "rho", call("sqrt", { add(pow(r, number(2.0)), pow(h, number(2.0))) }) }, { "phi", call("atan2", { r, h }) } });
}

void build_cylindrical_implicit(const Item& item, Ctx& c, ItemStatus& st, const Statement& s) {
    std::string err;
    const char axis = cylinder_axis(item);
    const std::string along(1, axis);
    ExprPtr F0 = prepare(sub(s.lhs, s.rhs ? s.rhs : number(0.0)), c.env, { "r", "theta", along, "rho", "phi" }, err);
    if (!F0) {
        st.error = err;
        return;
    }
    const ExprPtr F = mixed_to_cylinder(F0, axis);
    st.mathtext = to_mathtext(s.lhs) + " " + (s.relation == Relation::None ? "=" : relation_text(s.relation)) + " " + (s.rhs ? to_mathtext(s.rhs) : std::string("0"));
    const double ax[2] = { xlo(c), xhi(c) };
    const double ay[2] = { ylo(c), yhi(c) };
    const double az[2] = { c.zmin, c.zmax };
    const double* p1 = axis == 'x' ? ay : ax;
    const double* p2 = axis == 'z' ? ay : az;
    const double* along_range = axis == 'x' ? ax : axis == 'y' ? ay : az;
    double reach = 0.0;
    for (double u : { p1[0], p1[1] }) {
        for (double v : { p2[0], p2[1] }) reach = std::max(reach, std::hypot(u, v));
    }
    const double lo[3] = { 0.0, 0.0, along_range[0] };
    const double hi[3] = { reach, 6.283185307179586, along_range[1] };
    implicit_in_coordinates(item, c, st, F, { "r", "theta", along }, lo, hi, [axis](double r, double th, double h) {
        return cylinder_point(axis, r, th, h);
    }, s.relation, cylinder_to_cartesian(F, axis));
    if (axis != 'z' && st.note.empty()) st.note = std::string("r is the distance from the ") + axis + " axis; \xCE\xB8 turns around it" + (axis == 'x' ? " from y toward z." : " from z toward x.");
    if (depends_on(F0, "rho") || depends_on(F0, "phi")) {
        const std::string more = std::string("\xCF\x81 = \xE2\x88\x9A(r\xC2\xB2 + ") + along + "\xC2\xB2) is the distance from the origin and \xCF\x86 the angle from the " + along + " axis.";
        st.note = st.note.empty() ? more : st.note + " " + more;
    }
}

void build_implicit3d(const Item& item, Ctx& c, ItemStatus& st) {
    ParseResult r = parse_field(item.field("eq"), c.env, { "x", "y", "z" });
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    const Statement& s = r.statement;
    const bool inequality = s.relation != Relation::None && s.relation != Relation::Equal;
    ExprPtr raw = s.relation == Relation::None ? s.lhs : sub(s.lhs, s.rhs);
    std::string err;
    ExprPtr F0 = prepare(raw, c.env, { "x", "y", "z" }, err);
    if (!F0) {
        st.error = err;
        return;
    }
    const ExprPtr F = restrict_to_limits(F0, item, { "x", "y", "z" }, c.env, err);
    if (!F) {
        st.error = err;
        return;
    }
    st.mathtext = to_mathtext(s.lhs) + " " + (s.relation == Relation::None ? "=" : relation_text(s.relation)) + " " + (s.rhs ? to_mathtext(s.rhs) : std::string("0"));
    Func fn(F, { "x", "y", "z" }, false, c.env.geo_type);
    if (inequality && fn.ok()) {
        Func hw(F, { "x", "y", "z" }, false, NumberType::Hardware);
        region_caps(c, hw, s.relation, soft(item_color(item), 0.9));
        st.note = "the region is shaded on the walls of the box; the surface is its edge";
    }
    if (!fn.ok()) {
        st.error = fn.error();
        return;
    }
    double res = item.kind == "surface" ? grid_size(c, 28, 44, 70) : 40.0;
    if (item.kind != "surface") parse_number(item.field("resolution"), c.env, res, err);
    const int n = static_cast<int>(std::clamp(res * (c.env.quality >= 3 ? 1.5 : c.env.quality <= 1 ? 0.7 : 1.0), 10.0, 110.0));
    Grid3 g;
    g.n = n;
    g.lo[0] = xlo(c);
    g.hi[0] = xhi(c);
    g.lo[1] = ylo(c);
    g.hi[1] = yhi(c);
    g.lo[2] = c.zmin;
    g.hi[2] = c.zmax;
    const std::size_t total = static_cast<std::size_t>(n) * n * n;
    std::vector<double> xs(total);
    std::vector<double> ys(total);
    std::vector<double> zs(total);
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
    g.v = fn.map3(xs, ys, zs);
    std::vector<Vec3> tris;
    marching_tetrahedra(g, tris);
    if (tris.empty()) {
        st.note = "no part of this surface is in the box";
        return;
    }
    Mesh3 mesh;
    mesh.pos = tris;
    for (std::uint32_t i = 0; i < tris.size(); ++i) mesh.tri.push_back(i);
    const ExprPtr gx = d(F, "x");
    const ExprPtr gy = d(F, "y");
    const ExprPtr gz = d(F, "z");
    if (gx && gy && gz) {
        std::vector<double> px(tris.size()), py(tris.size()), pz(tris.size());
        for (std::size_t i = 0; i < tris.size(); ++i) {
            px[i] = tris[i].x;
            py[i] = tris[i].y;
            pz[i] = tris[i].z;
        }
        Func fgx(gx, { "x", "y", "z" }, false, NumberType::Hardware);
        Func fgy(gy, { "x", "y", "z" }, false, NumberType::Hardware);
        Func fgz(gz, { "x", "y", "z" }, false, NumberType::Hardware);
        const std::vector<double> a = fgx.map3(px, py, pz);
        const std::vector<double> b = fgy.map3(px, py, pz);
        const std::vector<double> cc = fgz.map3(px, py, pz);
        mesh.nrm.resize(tris.size());
        for (std::size_t i = 0; i < tris.size(); ++i) mesh.nrm[i] = normalized(Vec3{ a[i], b[i], cc[i] });
        bool bad = false;
        for (const Vec3& nn : mesh.nrm) {
            if (!finite(nn)) bad = true;
        }
        if (bad) mesh.nrm.clear();
        st.readouts.push_back(formula_vreadout("\xE2\x88\x87" "F", { gx, gy, gz }));
    }
    finish_normals(mesh);
    color_mesh(c, mesh, item, item.option("height_color"));
    if (inequality) mesh.alpha = 0.5;
    if (item.option("normals")) {
        const std::size_t step = std::max<std::size_t>(3, tris.size() / 3 / 80 * 3);
        for (std::size_t i = 0; i < tris.size(); i += step) {
            c.scene.s3.arrows.push_back(Arrow3{ tris[i], tris[i] + mesh.nrm[i] * (c.range * 0.18), c.env.theme.accent, 1.6f, "" });
        }
    }
    c.scene.s3.meshes.push_back(std::move(mesh));
    st.readouts.push_back({ "triangles", std::to_string(tris.size() / 3) });
}

void build_curve3d(const Item& item, Ctx& c, ItemStatus& st) {
    ParseResult r = parse_field(item.field("eq"), c.env, { "t" });
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    ExprPtr v = r.statement.relation == Relation::None ? r.statement.lhs : r.statement.rhs;
    if (component_count(v) != 3) {
        st.error = "write the curve as \xE2\x9F\xA8x(t), y(t), z(t)\xE2\x9F\xA9";
        return;
    }
    std::string err;
    ExprPtr X = prepare(component(v, 0), c.env, { "t" }, err);
    ExprPtr Y = X ? prepare(component(v, 1), c.env, { "t" }, err) : nullptr;
    ExprPtr Z = Y ? prepare(component(v, 2), c.env, { "t" }, err) : nullptr;
    if (!X || !Y || !Z) {
        st.error = err;
        return;
    }
    st.mathtext = "r(t) = \xE2\x9F\xA8" + to_mathtext(X) + ", " + to_mathtext(Y) + ", " + to_mathtext(Z) + "\xE2\x9F\xA9";
    Range range;
    if (!parse_open_range(item, "min", "max", c.env, range, err)) {
        st.error = err;
        return;
    }
    Func fx(X, { "t" }, false, c.env.geo_type);
    Func fy(Y, { "t" }, false, c.env.geo_type);
    Func fz(Z, { "t" }, false, c.env.geo_type);
    if (!fx.ok() || !fy.ok() || !fz.ok()) {
        st.error = !fx.ok() ? fx.error() : !fy.ok() ? fy.error() : fz.error();
        return;
    }
    CurveSampler3 s;
    s.scale = 2.0 * c.range;
    s.initial = 600;
    s.eval = [&](const std::vector<double>& t, std::vector<Vec3>& out) {
        const std::vector<double> a = fx.map1(t);
        const std::vector<double> b = fy.map1(t);
        const std::vector<double> d2 = fz.map1(t);
        out.resize(t.size());
        for (std::size_t i = 0; i < t.size(); ++i) out[i] = Vec3{ a[i], b[i], d2[i] };
    };
    std::vector<double> ts;
    std::vector<Vec3> pts;
    sample_curve3(s, range.a, range.b, ts, pts);
    const Rgba col = item_color(item);
    Line3 line;
    line.pts = pts;
    line.color = col;
    line.width = 3.0f;
    const ExprPtr X1 = d(X, "t"), Y1 = d(Y, "t"), Z1 = d(Z, "t");
    const ExprPtr X2 = X1 ? d(X1, "t") : nullptr, Y2 = Y1 ? d(Y1, "t") : nullptr, Z2 = Z1 ? d(Z1, "t") : nullptr;
    const ExprPtr X3 = X2 ? d(X2, "t") : nullptr, Y3 = Y2 ? d(Y2, "t") : nullptr, Z3 = Z2 ? d(Z2, "t") : nullptr;
    ExprPtr kappa;
    ExprPtr tau;
    if (X2 && Y2 && Z2) {
        const ExprPtr cx = simplify(sub(mul(Y1, Z2), mul(Z1, Y2)));
        const ExprPtr cy = simplify(sub(mul(Z1, X2), mul(X1, Z2)));
        const ExprPtr cz = simplify(sub(mul(X1, Y2), mul(Y1, X2)));
        const ExprPtr cross2 = add(add(pow(cx, number(2.0)), pow(cy, number(2.0))), pow(cz, number(2.0)));
        const ExprPtr speed2 = add(add(pow(X1, number(2.0)), pow(Y1, number(2.0))), pow(Z1, number(2.0)));
        kappa = simplify(div(call("sqrt", { cross2 }), pow(speed2, number(1.5))));
        if (X3 && Y3 && Z3) tau = simplify(div(add(add(mul(cx, X3), mul(cy, Y3)), mul(cz, Z3)), cross2));
    }
    if ((item.option("curvature_color") && kappa) || (item.option("torsion_color") && tau)) {
        const bool by_tau = !(item.option("curvature_color") && kappa);
        Func fk(by_tau ? tau : kappa, { "t" }, false, NumberType::Hardware);
        const std::vector<double> k = fk.map1(ts);
        std::vector<double> mags;
        for (double kv : k) mags.push_back(std::fabs(kv));
        const double top = std::max(1e-12, robust_max(mags));
        for (double kv : k) line.colors.push_back(by_tau ? colormap("coolwarm", 0.5 + 0.5 * std::clamp(kv / top, -1.0, 1.0)) : colormap("turbo", std::min(1.0, std::fabs(kv) / top)));
        line.width = 4.0f;
        st.legend_min = by_tau ? number_text(-top) : "0";
        st.legend_max = number_text(top);
        st.legend_map = by_tau ? "coolwarm" : "turbo";
    }
    if (item.option("shadow")) {
        Line3 sh;
        sh.color = with_alpha(c.env.theme.text, 0.35);
        sh.width = 1.6f;
        sh.dashed = true;
        for (const Vec3& p : pts) sh.pts.push_back(finite(p) ? Vec3{ p.x, p.y, c.zmin } : p);
        c.scene.s3.lines.push_back(std::move(sh));
    }
    if (item.option("arrows")) direction_arrows3(c, pts, col);
    c.scene.s3.lines.push_back(std::move(line));
    double t0 = 0.0;
    if (!parse_number(item.field("at"), c.env, t0, err)) return;
    {
        double lo = 0.0, hi = 1.0;
        slider_span(range, t0, lo, hi);
        offer_param(st, "t\xE2\x82\x80", lo, hi, t0);
    }
    const Vec3 p{ fx.at({ t0 }), fy.at({ t0 }), fz.at({ t0 }) };
    if (!finite(p)) {
        st.note = "the curve isn't defined at t\xE2\x82\x80";
        return;
    }
    c.scene.s3.dots.push_back(Dot3{ p, col, 6.0f, "" });
    const std::vector<std::string> tv = { "t" };
    st.readouts.push_back(sym_vreadout("r(t\xE2\x82\x80)", { X, Y, Z }, tv, { t0 }, c.env));
    if (!X1 || !Y1 || !Z1) {
        st.note = "Arbor can't differentiate this curve exactly.";
        return;
    }
    st.readouts.push_back(sym_vreadout("r\xE2\x80\xB2(t\xE2\x82\x80)", { X1, Y1, Z1 }, tv, { t0 }, c.env));
    st.readouts.push_back(sym_readout("speed |r\xE2\x80\xB2|", simplify(call("sqrt", { add(add(pow(X1, number(2.0)), pow(Y1, number(2.0))), pow(Z1, number(2.0))) })), tv, { t0 }, c.env));
    if (kappa) st.readouts.push_back(sym_readout("curvature \xCE\xBA", kappa, tv, { t0 }, c.env));
    if (tau) st.readouts.push_back(sym_readout("torsion \xCF\x84 (twist)", tau, tv, { t0 }, c.env));
    const ExprPtr speed_e = simplify(call("sqrt", { add(add(pow(X1, number(2.0)), pow(Y1, number(2.0))), pow(Z1, number(2.0))) }));
    std::vector<ExprPtr> T_e, N_e, B_e, C_e;
    ExprPtr R_e;
    if (X2 && Y2 && Z2 && symbols_wanted()) {
        const ExprPtr cx = simplify(sub(mul(Y1, Z2), mul(Z1, Y2)));
        const ExprPtr cy = simplify(sub(mul(Z1, X2), mul(X1, Z2)));
        const ExprPtr cz = simplify(sub(mul(X1, Y2), mul(Y1, X2)));
        const ExprPtr cross2 = simplify(add(add(pow(cx, number(2.0)), pow(cy, number(2.0))), pow(cz, number(2.0))));
        const ExprPtr cmag = simplify(call("sqrt", { cross2 }));
        const ExprPtr speed2 = simplify(add(add(pow(X1, number(2.0)), pow(Y1, number(2.0))), pow(Z1, number(2.0))));
        T_e = { simplify(div(X1, speed_e)), simplify(div(Y1, speed_e)), simplify(div(Z1, speed_e)) };
        B_e = { simplify(div(cx, cmag)), simplify(div(cy, cmag)), simplify(div(cz, cmag)) };
        const ExprPtr nx = simplify(sub(mul(cy, Z1), mul(cz, Y1)));
        const ExprPtr ny = simplify(sub(mul(cz, X1), mul(cx, Z1)));
        const ExprPtr nz = simplify(sub(mul(cx, Y1), mul(cy, X1)));
        const ExprPtr nden = simplify(mul(cmag, speed_e));
        N_e = { simplify(div(nx, nden)), simplify(div(ny, nden)), simplify(div(nz, nden)) };
        R_e = simplify(div(pow(speed2, number(1.5)), cmag));
        C_e = { simplify(add(X, div(mul(nx, speed2), cross2))), simplify(add(Y, div(mul(ny, speed2), cross2))), simplify(add(Z, div(mul(nz, speed2), cross2))) };
    }
    {
        const ExprPtr speed_x = simplify(call("sqrt", { add(add(pow(X1, number(2.0)), pow(Y1, number(2.0))), pow(Z1, number(2.0))) }));
        Func speed(speed_x, tv, false, NumberType::Hardware);
        const double from = range.a;
        if (from != t0) {
            const Integral L = integrate_open([&](double tt) { return speed.at({ tt }); }, from, t0, 1000);
            const std::string start = std::isinf(from) ? std::string(from < 0 ? "-\xE2\x88\x9E" : "\xE2\x88\x9E") : format_double(from, 4);
            std::string sym;
            const std::string ss = symbol_text(speed_x);
            if (!ss.empty()) sym = "\\int\\_{" + (std::isinf(from) ? std::string(from < 0 ? "-\\infty\\" : "\\infty\\") : format_double(from, 6)) + "}^{t_{0}} " + ss + " dt";
            if (L.ok) st.readouts.push_back({ "arc length from t = " + start, L.finite ? number_text(std::fabs(L.value)) : std::string("\xE2\x88\x9E"), sym });
        }
    }
    if (!X2 || !Y2 || !Z2) return;
    Func dx1(X1, tv, false, NumberType::Hardware), dy1(Y1, tv, false, NumberType::Hardware), dz1(Z1, tv, false, NumberType::Hardware);
    Func dx2(X2, tv, false, NumberType::Hardware), dy2(Y2, tv, false, NumberType::Hardware), dz2(Z2, tv, false, NumberType::Hardware);
    const Vec3 vel{ dx1.at({ t0 }), dy1.at({ t0 }), dz1.at({ t0 }) };
    const Vec3 acc{ dx2.at({ t0 }), dy2.at({ t0 }), dz2.at({ t0 }) };
    const Vec3 T = normalized(vel);
    const Vec3 b = cross(vel, acc);
    if (!finite(T) || length(b) < 1e-14) {
        st.note = "the curve is straight at t\xE2\x82\x80, so N and B aren't defined";
        if (item.option("frame") && finite(T)) c.scene.s3.arrows.push_back(Arrow3{ p, p + T * (c.range * 0.4), rgba(229, 83, 75), 2.6f, "T" });
        return;
    }
    const Vec3 B = normalized(b);
    const Vec3 N = cross(B, T);
    const double k = length(b) / std::pow(length(vel), 3.0);
    const double L = c.range * 0.4;
    st.readouts.push_back({ "T = r\xE2\x80\xB2/|r\xE2\x80\xB2|", vec_text({ T.x, T.y, T.z }), T_e.empty() ? std::string() : symbol_vec(T_e) });
    st.readouts.push_back({ "N = B \xC3\x97 T", vec_text({ N.x, N.y, N.z }), N_e.empty() ? std::string() : symbol_vec(N_e) });
    st.readouts.push_back({ "B = (r\xE2\x80\xB2 \xC3\x97 r\xE2\x80\xB3)/|r\xE2\x80\xB2 \xC3\x97 r\xE2\x80\xB3|", vec_text({ B.x, B.y, B.z }), B_e.empty() ? std::string() : symbol_vec(B_e) });
    {
        const Vec3 center = p + N * (1.0 / k);
        st.readouts.push_back({ "radius 1/\xCE\xBA", number_text(1.0 / k), R_e ? symbol_text(R_e) : std::string() });
        st.readouts.push_back({ "osculating circle center r + N/\xCE\xBA", vec_text({ center.x, center.y, center.z }), C_e.empty() ? std::string() : symbol_vec(C_e) });
        const std::string plane = format_double(B.x, 5) + "(x - " + format_double(p.x, 5) + ") + " + format_double(B.y, 5) + "(y - " + format_double(p.y, 5) + ") + " + format_double(B.z, 5) + "(z - " + format_double(p.z, 5) + ") = 0";
        std::string plane_sym;
        if (!B_e.empty()) {
            const std::string bx = symbol_text(B_e[0]), by = symbol_text(B_e[1]), bz = symbol_text(B_e[2]);
            const std::string rx = symbol_text(X), ry = symbol_text(Y), rz = symbol_text(Z);
            if (!bx.empty() && !by.empty() && !bz.empty() && !rx.empty() && !ry.empty() && !rz.empty())
                plane_sym = "(" + bx + ")(x - (" + rx + ")) + (" + by + ")(y - (" + ry + ")) + (" + bz + ")(z - (" + rz + ")) = 0";
            if (plane_sym.size() > 1400) plane_sym.clear();
        }
        st.readouts.push_back({ "osculating plane B \xC2\xB7 (P - r) = 0", plane, plane_sym });
    }
    if (item.option("frame")) {
        c.scene.s3.arrows.push_back(Arrow3{ p, p + T * L, rgba(229, 83, 75), 2.6f, "T" });
        c.scene.s3.arrows.push_back(Arrow3{ p, p + N * L, rgba(63, 166, 90), 2.6f, "N" });
        c.scene.s3.arrows.push_back(Arrow3{ p, p + B * L, rgba(79, 143, 230), 2.6f, "B" });
    }
    if (item.option("circle") && k > 1e-12) {
        const double R = 1.0 / k;
        const Vec3 center = p + N * R;
        Line3 circ;
        circ.color = with_alpha(c.env.theme.text, 0.7);
        circ.width = 1.6f;
        circ.dashed = true;
        for (int i = 0; i <= 180; ++i) {
            const double a = 6.283185307179586 * i / 180.0;
            circ.pts.push_back(center + (T * std::sin(a) - N * std::cos(a)) * R);
        }
        c.scene.s3.lines.push_back(std::move(circ));
        c.scene.s3.dots.push_back(Dot3{ center, with_alpha(c.env.theme.text, 0.7), 4.0f, "" });
    }
    if (item.option("plane")) add_quad(c, p, T * (L * 1.3), N * (L * 1.3), rgba(63, 166, 90), 0.22);
}

void build_param_surface(const Item& item, Ctx& c, ItemStatus& st) {
    ParseResult r = parse_field(item.field("eq"), c.env, { "u", "v" });
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    ExprPtr v = r.statement.relation == Relation::None ? r.statement.lhs : r.statement.rhs;
    if (component_count(v) != 3) {
        st.error = "write the surface as \xE2\x9F\xA8x(u, v), y(u, v), z(u, v)\xE2\x9F\xA9";
        return;
    }
    std::string err;
    ParamSurface ps;
    ps.u = "u";
    ps.v = "v";
    ps.x = prepare(component(v, 0), c.env, { "u", "v" }, err);
    ps.y = ps.x ? prepare(component(v, 1), c.env, { "u", "v" }, err) : nullptr;
    ps.z = ps.y ? prepare(component(v, 2), c.env, { "u", "v" }, err) : nullptr;
    if (!ps.x || !ps.y || !ps.z) {
        st.error = err;
        return;
    }
    st.mathtext = "\xE2\x9F\xA8" + to_mathtext(ps.x) + ", " + to_mathtext(ps.y) + ", " + to_mathtext(ps.z) + "\xE2\x9F\xA9";
    Range ru;
    Range rv;
    if (!parse_range(item, "umin", "umax", c.env, ru, err) || !parse_range(item, "vmin", "vmax", c.env, rv, err)) {
        st.error = err;
        return;
    }
    ps.u0 = ru.a;
    ps.u1 = ru.b;
    ps.v0 = rv.a;
    ps.v1 = rv.b;
    build_param(ps, item, c, st);
}

void build_revolution(const Item& item, Ctx& c, ItemStatus& st) {
    std::string err;
    PlaneCurve pc;
    if (!plane_curve(item.field("eq"), c.env, pc, err)) {
        st.error = err;
        return;
    }
    Vec2 P;
    Vec2 D;
    if (!plane_line(item.field("axis"), c.env, P, D, err)) {
        st.error = "axis: " + err;
        return;
    }
    Range tr;
    if (!parse_open_range(item, "min", "max", c.env, tr, err)) {
        st.error = err;
        return;
    }
    double turn = 6.283185307179586;
    if (!item.field("angle").empty() && !parse_number(item.field("angle"), c.env, turn, err)) {
        st.error = "angle: " + err;
        return;
    }
    if (c.env.degrees) turn *= 3.141592653589793 / 180.0;
    Func fx(pc.x, { "t" }, false, NumberType::Hardware);
    Func fy(pc.y, { "t" }, false, NumberType::Hardware);
    double ua = tr.a;
    double ub = tr.b;
    if (range_unbounded(tr)) {
        OpenMap om;
        open_map(tr.a, tr.b, om, 1e-9);
        const double reach = 2.5 * std::max(c.range, 0.5 * (c.zmax - c.zmin));
        const double cx = c.view3.target.x;
        const double cz = 0.5 * (c.zmin + c.zmax);
        double lo = INFINITY;
        double hi = -INFINITY;
        const int N = 6000;
        for (int i = 0; i <= N; ++i) {
            const double t = om(om.u0 + (om.u1 - om.u0) * i / N);
            const double X = fx.at({ t });
            const double Y = fy.at({ t });
            if (!std::isfinite(X) || !std::isfinite(Y) || std::fabs(X - cx) > reach || std::fabs(Y - cz) > reach) continue;
            lo = std::min(lo, t);
            hi = std::max(hi, t);
        }
        if (!(lo < hi)) {
            st.note = "the curve is out of view";
            return;
        }
        const bool up = tr.a < tr.b;
        if (std::isinf(tr.a)) ua = up ? lo : hi;
        if (std::isinf(tr.b)) ub = up ? hi : lo;
    }
    const ExprPtr t = variable("t");
    const ExprPtr th = variable("theta");
    const ExprPtr rx = sub(pc.x, number(P.x));
    const ExprPtr ry = sub(pc.y, number(P.y));
    const ExprPtr along = tidy(simplify(add(mul(rx, number(D.x)), mul(ry, number(D.y)))));
    const ExprPtr rho = tidy(simplify(add(mul(rx, number(-D.y)), mul(ry, number(D.x)))));
    const ExprPtr cs = call("cos", { th });
    const ExprPtr sn = call("sin", { th });
    ParamSurface ps;
    ps.u = "t";
    ps.v = "theta";
    ps.x = tidy(simplify(add(add(number(P.x), mul(along, number(D.x))), mul(mul(rho, number(-D.y)), cs))));
    ps.y = tidy(simplify(mul(rho, sn)));
    ps.z = tidy(simplify(add(add(number(P.y), mul(along, number(D.y))), mul(mul(rho, number(D.x)), cs))));
    ps.u0 = ua;
    ps.u1 = ub;
    ps.v0 = 0.0;
    ps.v1 = turn;
    build_param(ps, item, c, st);
    if (!st.error.empty()) return;
    const Rgba col = item_color(item);
    const int n = 600;
    std::vector<Vec3> curve;
    for (int i = 0; i <= n; ++i) {
        const double tt = ua + (ub - ua) * i / n;
        const double X = fx.at({ tt });
        const double Y = fy.at({ tt });
        curve.push_back(std::isfinite(X) && std::isfinite(Y) ? Vec3{ X, 0.0, Y } : Vec3{ k_nan, k_nan, k_nan });
    }
    if (item.option("curve")) {
        Line3 l;
        l.color = c.env.theme.text;
        l.width = 3.0f;
        l.on_top = true;
        l.pts = curve;
        c.scene.s3.lines.push_back(std::move(l));
        if (item.option("arrows")) direction_arrows3(c, curve, c.env.theme.text);
    }
    const double ends[2] = { tr.a, tr.b };
    const char* end_fields[2] = { "min", "max" };
    Func frho(rho, { "t" }, false, NumberType::Hardware);
    Func falong(along, { "t" }, false, NumberType::Hardware);
    for (int k = 0; k < 2; ++k) {
        if (!std::isfinite(ends[k]) || !limit_style_chosen(item, end_fields[k])) continue;
        const bool closed = limit_closed(item, end_fields[k]);
        const double X = fx.at({ ends[k] });
        const double Y = fy.at({ ends[k] });
        const double r = frho.at({ ends[k] });
        const double a = falong.at({ ends[k] });
        if (!std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(r) || !std::isfinite(a)) continue;
        Line3 rim;
        rim.color = with_alpha(c.env.theme.text, closed ? 0.9 : 0.6);
        rim.width = closed ? 2.4f : 1.8f;
        rim.dashed = !closed;
        rim.on_top = true;
        for (int i = 0; i <= 120; ++i) {
            const double v = turn * i / 120.0;
            rim.pts.push_back(Vec3{ P.x + a * D.x - r * D.y * std::cos(v), r * std::sin(v), P.y + a * D.y + r * D.x * std::cos(v) });
        }
        c.scene.s3.lines.push_back(std::move(rim));
        c.scene.s3.dots.push_back(Dot3{ Vec3{ X, 0.0, Y }, closed ? c.env.theme.text : with_alpha(c.env.theme.text, 0.45), closed ? 5.0f : 4.0f, "" });
    }
    if (item.option("axis_line")) {
        Line3 l;
        l.color = c.env.theme.accent;
        l.width = 2.0f;
        l.dashed = true;
        const double reach = 4.0 * std::max(c.range, c.zmax - c.zmin);
        l.pts.push_back(Vec3{ P.x - D.x * reach, 0.0, P.y - D.y * reach });
        l.pts.push_back(Vec3{ P.x + D.x * reach, 0.0, P.y + D.y * reach });
        c.scene.s3.lines.push_back(std::move(l));
    }
    const ExprPtr dalong = d(along, "t");
    const ExprPtr dx = d(pc.x, "t");
    const ExprPtr dy = d(pc.y, "t");
    if (dalong && dx && dy) {
        Func fa(dalong, { "t" }, false, NumberType::Hardware);
        Func fdx(dx, { "t" }, false, NumberType::Hardware);
        Func fdy(dy, { "t" }, false, NumberType::Hardware);
        bool crosses = false;
        bool flips = false;
        double first_sign = 0.0;
        double along_sign = 0.0;
        for (int i = 0; i <= 4000; ++i) {
            const double tt = ua + (ub - ua) * (i + 0.5) / 4001.0;
            const double r = frho.at({ tt });
            const double a = fa.at({ tt });
            if (std::isfinite(r) && r != 0.0) {
                if (first_sign == 0.0) first_sign = r > 0 ? 1.0 : -1.0;
                else if ((r > 0) != (first_sign > 0)) crosses = true;
            }
            if (std::isfinite(a) && std::fabs(a) > 1e-12) {
                if (along_sign == 0.0) along_sign = a > 0 ? 1.0 : -1.0;
                else if ((a > 0) != (along_sign > 0)) flips = true;
            }
        }
        const double part = turn / 6.283185307179586;
        const Integral vol = integrate_open([&](double tt) {
            const double r = frho.at({ tt });
            return 3.141592653589793 * part * r * r * std::fabs(fa.at({ tt }));
        }, tr.a, tr.b);
        const Integral area = integrate_open([&](double tt) {
            return 6.283185307179586 * part * std::fabs(frho.at({ tt })) * std::hypot(fdx.at({ tt }), fdy.at({ tt }));
        }, tr.a, tr.b);
        const std::string improper = range_unbounded(tr) ? " (improper integral)" : "";
        auto shown = [](const Integral& I) {
            if (!I.ok) return std::string("undefined somewhere in the range");
            if (!I.finite) return std::string("\xE2\x88\x9E (the integral diverges)");
            return number_text(std::fabs(I.value));
        };
        std::string vsym, asym;
        if (symbols_wanted()) {
            const std::string lo = std::isinf(tr.a) ? (tr.a < 0 ? "-\\infty\\" : "\\infty\\") : format_double(tr.a, 6);
            const std::string hi = std::isinf(tr.b) ? (tr.b < 0 ? "-\\infty\\" : "\\infty\\") : format_double(tr.b, 6);
            const std::string rs = symbol_text(rho);
            const std::string as = symbol_text(dalong);
            const std::string ss = symbol_text(simplify(call("sqrt", { add(pow(dx, number(2.0)), pow(dy, number(2.0))) })));
            const std::string factor = part == 1.0 ? "" : format_double(part, 6) + " \\cdot\\ ";
            if (!rs.empty() && !as.empty()) vsym = factor + "\\pi\\ \\int\\_{" + lo + "}^{" + hi + "} (" + rs + ")^{2} abs(" + as + ") dt";
            if (!rs.empty() && !ss.empty()) asym = factor + "2\\pi\\ \\int\\_{" + lo + "}^{" + hi + "} abs(" + rs + ") " + ss + " dt";
        }
        if (!flips) st.readouts.push_back({ "volume" + improper, shown(vol), vsym });
        st.readouts.push_back({ "surface area" + improper, shown(area), asym });
        if (crosses) st.note = "The curve crosses the axis, so the solid overlaps itself there.";
        else if (flips) st.note = "The curve turns back along the axis, so it doesn't bound one solid; only its surface area is given.";
    }
    st.mathtext = "\xE2\x9F\xA8" + to_mathtext(ps.x) + ", " + to_mathtext(ps.y) + ", " + to_mathtext(ps.z) + "\xE2\x9F\xA9";
    st.readouts.insert(st.readouts.begin(), formula_vreadout("r(t, \xCE\xB8)", { ps.x, ps.y, ps.z }));
    st.readouts.insert(st.readouts.begin() + 1, formula_readout("distance from the axis", rho));
    std::string tail = "The 2D graph's plane is stood upright: its x is x, its y is z.";
    if (range_unbounded(tr)) tail += " The range has no end, so the surface is drawn as far as the view reaches.";
    st.note = st.note.empty() ? tail : st.note + " " + tail;
}

void build_spherical(const Item& item, Ctx& c, ItemStatus& st) {
    ParseResult r = parse_field(item.field("eq"), c.env, { "theta", "phi", "rho" });
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    ExprPtr raw;
    Statement sph = r.statement;
    if (sph.lhs) sph.lhs = substitute(sph.lhs, { { "r", variable("rho") } });
    if (sph.rhs) sph.rhs = substitute(sph.rhs, { { "r", variable("rho") } });
    if (sph.relation == Relation::None && !depends_on(sph.lhs, "rho")) raw = sph.lhs;
    else if (sph.relation == Relation::Equal && is_var(sph.lhs, "rho") && !depends_on(sph.rhs, "rho")) raw = sph.rhs;
    else if (sph.relation == Relation::Equal && is_var(sph.rhs, "rho") && !depends_on(sph.lhs, "rho")) raw = sph.lhs;
    else {
        build_spherical_implicit(item, c, st, sph);
        return;
    }
    std::string err;
    ExprPtr R = prepare(raw, c.env, { "theta", "phi" }, err);
    if (!R) {
        st.error = err;
        return;
    }
    st.mathtext = "\\rho\\ = " + to_mathtext(R);
    ParamSurface ps;
    ps.u = "theta";
    ps.v = "phi";
    const ExprPtr th = variable("theta");
    const ExprPtr ph = variable("phi");
    ps.x = simplify(mul(mul(R, call("sin", { ph })), call("cos", { th })));
    ps.y = simplify(mul(mul(R, call("sin", { ph })), call("sin", { th })));
    ps.z = simplify(mul(R, call("cos", { ph })));
    ps.u0 = 0.0;
    ps.u1 = 6.283185307179586;
    ps.v0 = 0.0;
    ps.v1 = 3.141592653589793;
    build_param(ps, item, c, st);
}

void build_vector_field3d(const Item& item, Ctx& c, ItemStatus& st) {
    ParseResult r = parse_field(item.field("eq"), c.env, { "x", "y", "z" });
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    ExprPtr v = r.statement.relation == Relation::None ? r.statement.lhs : r.statement.rhs;
    if (component_count(v) != 3) {
        st.error = "write the field as \xE2\x9F\xA8P, Q, R\xE2\x9F\xA9";
        return;
    }
    std::string err;
    std::vector<ExprPtr> comps;
    for (int i = 0; i < 3; ++i) {
        ExprPtr e = prepare(component(v, static_cast<std::size_t>(i)), c.env, { "x", "y", "z" }, err);
        if (!e) {
            st.error = err;
            return;
        }
        comps.push_back(e);
    }
    st.mathtext = "F = \xE2\x9F\xA8" + to_mathtext(comps[0]) + ", " + to_mathtext(comps[1]) + ", " + to_mathtext(comps[2]) + "\xE2\x9F\xA9";
    double density = 7.0;
    parse_number(item.field("density"), c.env, density, err);
    const int n = static_cast<int>(std::clamp(density, 2.0, 16.0));
    Func fp(comps[0], { "x", "y", "z" }, false, NumberType::Hardware);
    Func fq(comps[1], { "x", "y", "z" }, false, NumberType::Hardware);
    Func fr(comps[2], { "x", "y", "z" }, false, NumberType::Hardware);
    std::vector<double> xs, ys, zs;
    for (int k = 0; k < n; ++k) {
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                xs.push_back(xlo(c) + (xhi(c) - xlo(c)) * (i + 0.5) / n);
                ys.push_back(ylo(c) + (yhi(c) - ylo(c)) * (j + 0.5) / n);
                zs.push_back(c.zmin + (c.zmax - c.zmin) * (k + 0.5) / n);
            }
        }
    }
    const std::vector<double> P = fp.map3(xs, ys, zs);
    const std::vector<double> Q = fq.map3(xs, ys, zs);
    const std::vector<double> R = fr.map3(xs, ys, zs);
    std::vector<double> mag(xs.size());
    for (std::size_t i = 0; i < xs.size(); ++i) mag[i] = std::sqrt(P[i] * P[i] + Q[i] * Q[i] + R[i] * R[i]);
    const double top = std::max(1e-12, robust_max(mag));
    const double spacing = 2.0 * c.range / n;
    const double tiny = std::max(std::numeric_limits<double>::min(), 1e-9 * robust_max(mag));
    for (std::size_t i = 0; i < xs.size(); ++i) {
        if (!std::isfinite(mag[i]) || mag[i] <= tiny) continue;
        const Vec3 dir{ P[i] / mag[i], Q[i] / mag[i], R[i] / mag[i] };
        const double len = item.option("normalize") ? 0.7 * spacing : 0.95 * spacing * std::min(1.0, mag[i] / top);
        const Vec3 from = Vec3{ xs[i], ys[i], zs[i] } - dir * (len * 0.5);
        c.scene.s3.arrows.push_back(Arrow3{ from, from + dir * len, colormap("viridis", 0.15 + 0.85 * std::min(1.0, mag[i] / top)), 1.8f, "" });
    }
    st.legend_min = "0";
    st.legend_max = number_text(top);
    st.legend_map = "viridis";
    const ExprPtr px = d(comps[0], "x"), qy = d(comps[1], "y"), rz = d(comps[2], "z");
    if (px && qy && rz) st.readouts.push_back(formula_readout("div F", simplify(add(add(px, qy), rz))));
    const ExprPtr ry = d(comps[2], "y"), qz = d(comps[1], "z"), pz = d(comps[0], "z"), rx = d(comps[2], "x"), qx = d(comps[1], "x"), py = d(comps[0], "y");
    if (ry && qz && pz && rx && qx && py) st.readouts.push_back(formula_vreadout("curl F", { simplify(sub(ry, qz)), simplify(sub(pz, rx)), simplify(sub(qx, py)) }));
    if (item.option("streamlines")) {
        const double h = spacing * 0.15;
        auto field = [&](Vec3 p) { return Vec3{ fp.at({ p.x, p.y, p.z }), fq.at({ p.x, p.y, p.z }), fr.at({ p.x, p.y, p.z }) }; };
        const int seeds = std::max(2, n / 2);
        for (int sk = 0; sk < seeds; ++sk) {
            for (int sj = 0; sj < seeds; ++sj) {
                for (int si = 0; si < seeds; ++si) {
                    Vec3 p{ xlo(c) + (xhi(c) - xlo(c)) * (si + 0.5) / seeds, ylo(c) + (yhi(c) - ylo(c)) * (sj + 0.5) / seeds, c.zmin + (c.zmax - c.zmin) * (sk + 0.5) / seeds };
                    Line3 l;
                    l.color = with_alpha(item_color(item), 0.8);
                    l.width = 1.4f;
                    l.pts.push_back(p);
                    for (int step = 0; step < 160; ++step) {
                        auto unit = [&](Vec3 q) { const Vec3 f = field(q); const double m = length(f); return m > 0.0 && std::isfinite(m) ? f * (1.0 / m) : Vec3{ k_nan, k_nan, k_nan }; };
                        const Vec3 k1 = unit(p);
                        const Vec3 k2 = unit(p + k1 * (h * 0.5));
                        const Vec3 k3 = unit(p + k2 * (h * 0.5));
                        const Vec3 k4 = unit(p + k3 * h);
                        const Vec3 np = p + (k1 + k2 * 2.0 + k3 * 2.0 + k4) * (h / 6.0);
                        if (!finite(np) || np.x < xlo(c) || np.x > xhi(c) || np.y < ylo(c) || np.y > yhi(c) || np.z < c.zmin || np.z > c.zmax) break;
                        l.pts.push_back(np);
                        p = np;
                    }
                    c.scene.s3.lines.push_back(std::move(l));
                }
            }
        }
    }
}

void build_points3d(const Item& item, Ctx& c, ItemStatus& st) {
    const std::vector<std::string> parts = split_top_level(item.field("eq"), ',');
    std::vector<Vec3> pts;
    for (const std::string& part : parts) {
        ParseResult r = parse_field(part, c.env, {});
        if (!r.ok) {
            st.error = r.error;
            return;
        }
        if (component_count(r.statement.lhs) != 3) {
            st.error = "each point needs three numbers, like (1, 2, 3)";
            return;
        }
        Vec3 p;
        double* slots[3] = { &p.x, &p.y, &p.z };
        for (int i = 0; i < 3; ++i) {
            std::string err;
            ExprPtr e = prepare(component(r.statement.lhs, static_cast<std::size_t>(i)), c.env, {}, err);
            if (!e) {
                st.error = err;
                return;
            }
            Func f(e, {}, false, NumberType::Hardware);
            *slots[i] = f.at({});
        }
        pts.push_back(p);
    }
    const Rgba col = item_color(item);
    if (item.option("join") && pts.size() > 1) {
        Line3 l;
        l.pts = pts;
        l.color = col;
        l.width = 2.4f;
        c.scene.s3.lines.push_back(std::move(l));
    }
    for (const Vec3& p : pts) c.scene.s3.dots.push_back(Dot3{ p, col, 6.0f, "" });
    st.readouts.push_back({ "points", std::to_string(pts.size()) });
}

} // namespace build
} // namespace arbor

namespace arbor {
namespace build {

void build_cylindrical_implicit_item(const Item& item, Ctx& c, ItemStatus& st) {
    const char axis = cylinder_axis(item);
    const std::string along(1, axis);
    ParseResult r = parse_field(item.field("eq"), c.env, { "theta", along, "r", "rho", "phi" });
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    build_cylindrical_implicit(item, c, st, r.statement);
}

void build_cylindrical(const Item& item, Ctx& c, ItemStatus& st) {
    const char axis = cylinder_axis(item);
    const std::string along(1, axis);
    ParseResult r = parse_field(item.field("eq"), c.env, { "theta", along, "r", "rho", "phi" });
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    if (depends_on(r.statement.lhs, "rho") || depends_on(r.statement.lhs, "phi") || (r.statement.rhs && (depends_on(r.statement.rhs, "rho") || depends_on(r.statement.rhs, "phi")))) {
        build_cylindrical_implicit(item, c, st, r.statement);
        return;
    }
    ExprPtr raw;
    if (r.statement.relation == Relation::None && !depends_on(r.statement.lhs, "r")) raw = r.statement.lhs;
    else if (r.statement.relation == Relation::Equal && is_var(r.statement.lhs, "r") && !depends_on(r.statement.rhs, "r")) raw = r.statement.rhs;
    else if (r.statement.relation == Relation::Equal && is_var(r.statement.rhs, "r") && !depends_on(r.statement.lhs, "r")) raw = r.statement.lhs;
    else {
        build_cylindrical_implicit(item, c, st, r.statement);
        return;
    }
    std::string err;
    ExprPtr R = prepare(raw, c.env, { "theta", along }, err);
    if (!R) {
        st.error = err;
        return;
    }
    Range zr;
    if (!parse_range(item, "zmin", "zmax", c.env, zr, err)) {
        st.error = err;
        return;
    }
    st.mathtext = "r = " + to_mathtext(R);
    const ExprPtr th = variable("theta");
    const ExprPtr h = variable("cylz");
    const ExprPtr Rh = substitute(R, { { along, h } });
    Func fx(simplify(mul(Rh, call("cos", { th }))), { "theta", "cylz" }, false, c.env.geo_type);
    Func fy(simplify(mul(Rh, call("sin", { th }))), { "theta", "cylz" }, false, c.env.geo_type);
    if (axis != 'z') st.note = std::string("r is the distance from the ") + axis + " axis; \xCE\xB8 turns around it" + (axis == 'x' ? " from y toward z." : " from z toward x.");
    if (!fx.ok() || !fy.ok()) {
        st.error = !fx.ok() ? fx.error() : fy.error();
        return;
    }
    const int nu = c.env.quality <= 1 ? 48 : c.env.quality >= 3 ? 144 : 90;
    const int nv = nu / 2;
    Mesh3 mesh;
    std::vector<Vec3> grid;
    surface_mesh(c, [&](const std::vector<double>& us, const std::vector<double>& vs, std::vector<Vec3>& out) {
        const std::vector<double> X = fx.map2(us, vs);
        const std::vector<double> Y = fy.map2(us, vs);
        out.resize(us.size());
        for (std::size_t i = 0; i < us.size(); ++i) {
            if (axis == 'x') out[i] = Vec3{ vs[i], X[i], Y[i] };
            else if (axis == 'y') out[i] = Vec3{ Y[i], vs[i], X[i] };
            else out[i] = Vec3{ X[i], Y[i], vs[i] };
        }
    }, 0.0, 6.283185307179586, zr.a, zr.b, nu, nv, mesh, grid);
    const Rgba base = item_color(item);
    mesh.col.resize(mesh.pos.size());
    for (std::size_t i = 0; i < mesh.pos.size(); ++i) {
        const double t = c.zmax > c.zmin ? (mesh.pos[i].z - c.zmin) / (c.zmax - c.zmin) : 0.5;
        mesh.col[i] = item.option("height_color") && finite(mesh.pos[i]) ? colormap("viridis", 0.08 + 0.9 * t) : base;
    }
    c.scene.s3.meshes.push_back(std::move(mesh));
    if (item.option("mesh")) mesh_lines(c, grid, nu, nv, std::max(1, nu / 24), with_alpha(rgba(0, 0, 0), 0.3));
}

void build_curve_value3d(const Item& item, Ctx& c, ItemStatus& st) {
    ParseResult rf = parse_field(item.field("eq"), c.env, { "x", "y", "z" });
    if (!rf.ok) {
        st.error = "f: " + rf.error;
        return;
    }
    ParseResult rp = parse_field(item.field("path"), c.env, { "t" });
    if (!rp.ok) {
        st.error = "path: " + rp.error;
        return;
    }
    ExprPtr fraw = rf.statement.relation == Relation::None ? rf.statement.lhs : rf.statement.rhs;
    ExprPtr path = rp.statement.relation == Relation::None ? rp.statement.lhs : rp.statement.rhs;
    if (component_count(path) != 3) {
        st.error = "write the path as \xE2\x9F\xA8x(t), y(t), z(t)\xE2\x9F\xA9";
        return;
    }
    std::string err;
    const std::vector<std::string> xyz = { "x", "y", "z" };
    ExprPtr F = prepare(fraw, c.env, xyz, err);
    ExprPtr X = F ? prepare(component(path, 0), c.env, { "t" }, err) : nullptr;
    ExprPtr Y = X ? prepare(component(path, 1), c.env, { "t" }, err) : nullptr;
    ExprPtr Z = Y ? prepare(component(path, 2), c.env, { "t" }, err) : nullptr;
    if (!F || !X || !Y || !Z) {
        st.error = err;
        return;
    }
    Range range;
    if (!parse_range(item, "min", "max", c.env, range, err)) {
        st.error = err;
        return;
    }
    const ExprPtr G = simplify(substitute(F, { { "x", X }, { "y", Y }, { "z", Z } }));
    st.mathtext = "f(r(t)) = " + to_mathtext(G);
    Func fx(X, { "t" }, false, c.env.geo_type), fy(Y, { "t" }, false, c.env.geo_type), fz(Z, { "t" }, false, c.env.geo_type), fg(G, { "t" }, false, c.env.geo_type);
    if (!fx.ok() || !fy.ok() || !fz.ok() || !fg.ok()) {
        st.error = !fg.ok() ? fg.error() : "the path doesn't evaluate";
        return;
    }
    CurveSampler3 s;
    s.scale = 2.0 * c.range;
    s.initial = 600;
    s.eval = [&](const std::vector<double>& t, std::vector<Vec3>& out) {
        const std::vector<double> a = fx.map1(t), b = fy.map1(t), z = fz.map1(t);
        out.resize(t.size());
        for (std::size_t i = 0; i < t.size(); ++i) out[i] = Vec3{ a[i], b[i], z[i] };
    };
    std::vector<double> ts;
    std::vector<Vec3> pts;
    sample_curve3(s, range.a, range.b, ts, pts);
    const std::vector<double> gv = fg.map1(ts);
    const double lo = robust_min(gv);
    const double hi = robust_max(gv);
    Line3 line;
    line.pts = pts;
    line.width = 4.0f;
    line.color = item_color(item);
    for (double v : gv) line.colors.push_back(colormap("viridis", hi > lo ? (v - lo) / (hi - lo) : 0.5));
    c.scene.s3.lines.push_back(std::move(line));
    st.legend_min = number_text(lo);
    st.legend_max = number_text(hi);
    st.legend_map = "viridis";
    double t0 = 0.0;
    const bool has_t0 = parse_number(item.field("at"), c.env, t0, err);
    if (has_t0) offer_param(st, "t\xE2\x82\x80", range.a, range.b, t0);
    if (item.option("inset")) {
        Inset2 in;
        in.title = "f(x(t), y(t), z(t))";
        in.xlabel = "t";
        const std::vector<double> it = linspace(range.a, range.b, 400);
        const std::vector<double> iv = fg.map1(it);
        for (std::size_t i = 0; i < it.size(); ++i) {
            in.pts.push_back(Vec2{ it[i], iv[i] });
            in.colors.push_back(colormap("viridis", hi > lo ? (iv[i] - lo) / (hi - lo) : 0.5));
        }
        in.color = item_color(item);
        if (has_t0) {
            in.marker = t0;
            in.has_marker = true;
        }
        c.scene.s2.insets.push_back(std::move(in));
    }
    if (!has_t0) return;
    const Vec3 p{ fx.at({ t0 }), fy.at({ t0 }), fz.at({ t0 }) };
    if (finite(p)) c.scene.s3.dots.push_back(Dot3{ p, item_color(item), 6.0f, "" });
    st.readouts.push_back(sym_readout("f(r(t\xE2\x82\x80))", G, { "t" }, { t0 }, c.env));
    if (ExprPtr dg = d(G, "t")) st.readouts.push_back(sym_readout("d/dt f(r(t))", dg, { "t" }, { t0 }, c.env));
    std::vector<ExprPtr> grad;
    std::vector<ExprPtr> vel = { d(X, "t"), d(Y, "t"), d(Z, "t") };
    for (const std::string& v : xyz) {
        ExprPtr g = d(F, v);
        grad.push_back(g ? simplify(substitute(g, { { "x", X }, { "y", Y }, { "z", Z } })) : nullptr);
    }
    if (grad[0] && grad[1] && grad[2] && vel[0] && vel[1] && vel[2]) {
        std::string gt = "(", vt = "(";
        for (int i = 0; i < 3; ++i) {
            gt += (i ? ", " : "") + value_text(grad[static_cast<std::size_t>(i)], { "t" }, { t0 }, c.env);
            vt += (i ? ", " : "") + value_text(vel[static_cast<std::size_t>(i)], { "t" }, { t0 }, c.env);
        }
        st.readouts.push_back({ "\xE2\x88\x87" "f", gt + ")" });
        st.readouts.push_back({ "r\xE2\x80\xB2(t\xE2\x82\x80)", vt + ")" });
        const ExprPtr dot3 = tidy(add(add(mul(grad[0], vel[0]), mul(grad[1], vel[1])), mul(grad[2], vel[2])));
        st.readouts.push_back(sym_readout("\xE2\x88\x87" "f \xC2\xB7 r\xE2\x80\xB2 (chain rule)", dot3, { "t" }, { t0 }, c.env));
        if (item.option("gradient") && finite(p)) {
            Func g0(grad[0], { "t" }, false, NumberType::Hardware), g1(grad[1], { "t" }, false, NumberType::Hardware), g2(grad[2], { "t" }, false, NumberType::Hardware);
            const Vec3 gvec{ g0.at({ t0 }), g1.at({ t0 }), g2.at({ t0 }) };
            if (finite(gvec) && length(gvec) > 0) c.scene.s3.arrows.push_back(Arrow3{ p, p + normalized(gvec) * (c.range * 0.4), c.env.theme.text, 2.4f, "\xE2\x88\x87" "f" });
        }
    }
}

} // namespace build
} // namespace arbor
