#include "build_common.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace arbor {
namespace build {

namespace {

const double k_nan = std::nan("");

bool is_var(const ExprPtr& e, const char* name) {
    return e && e->op == Op::Variable && e->text == name;
}

Relation flip(Relation r) {
    switch (r) {
        case Relation::Less: return Relation::Greater;
        case Relation::LessEqual: return Relation::GreaterEqual;
        case Relation::Greater: return Relation::Less;
        case Relation::GreaterEqual: return Relation::LessEqual;
        default: return r;
    }
}

bool strict(Relation r) {
    return r == Relation::Less || r == Relation::Greater;
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

void sample_function(Ctx& c, Func& f, bool in_y, std::vector<Vec2>& pts, std::vector<double>& ts) {
    CurveSampler2 s;
    s.box = c.box;
    s.width = c.width;
    s.height = c.height;
    s.initial = std::max(200, c.width / (c.env.quality >= 3 ? 1 : 2));
    s.rounds = c.env.quality >= 2 ? 7 : 5;
    s.eval = [&](const std::vector<double>& t, std::vector<Vec2>& out) {
        const std::vector<double> v = f.map1(t);
        out.resize(t.size());
        for (std::size_t i = 0; i < t.size(); ++i) out[i] = in_y ? Vec2{ v[i], t[i] } : Vec2{ t[i], v[i] };
    };
    const double a = in_y ? c.box.ymin : c.box.xmin;
    const double b = in_y ? c.box.ymax : c.box.xmax;
    const double pad = (b - a) * 0.02;
    sample_curve2(s, a - pad, b + pad, ts, pts);
}

void shade_between(Ctx& c, const std::vector<Vec2>& pts, bool in_y, double edge, Rgba color, double from, double to) {
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const Vec2 a = pts[i];
        const Vec2 b = pts[i + 1];
        if (!finite(a) || !finite(b)) continue;
        const double ta = in_y ? a.y : a.x;
        const double tb = in_y ? b.y : b.x;
        if (std::max(ta, tb) < from || std::min(ta, tb) > to) continue;
        auto clampv = [&](double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); };
        Vec2 a2 = a;
        Vec2 b2 = b;
        if (!in_y) {
            a2.y = clampv(a.y, c.box.ymin - 1e3 * (c.box.ymax - c.box.ymin), c.box.ymax + 1e3 * (c.box.ymax - c.box.ymin));
            b2.y = clampv(b.y, c.box.ymin - 1e3 * (c.box.ymax - c.box.ymin), c.box.ymax + 1e3 * (c.box.ymax - c.box.ymin));
            const Vec2 ea{ a2.x, edge };
            const Vec2 eb{ b2.x, edge };
            c.scene.s2.tris.push_back(Tri2{ a2, b2, eb, color });
            c.scene.s2.tris.push_back(Tri2{ a2, eb, ea, color });
        } else {
            const Vec2 ea{ edge, a2.y };
            const Vec2 eb{ edge, b2.y };
            c.scene.s2.tris.push_back(Tri2{ a2, b2, eb, color });
            c.scene.s2.tris.push_back(Tri2{ a2, eb, ea, color });
        }
    }
}

double simpson(Func& f, double a, double b, int n) {
    if (n % 2) ++n;
    const std::vector<double> xs = linspace(a, b, static_cast<std::size_t>(n) + 1);
    const std::vector<double> ys = f.map1(xs);
    const double h = (b - a) / n;
    double s = 0.0;
    for (int i = 0; i <= n; ++i) {
        if (!std::isfinite(ys[static_cast<std::size_t>(i)])) return k_nan;
        const double w = (i == 0 || i == n) ? 1.0 : (i % 2 ? 4.0 : 2.0);
        s += w * ys[static_cast<std::size_t>(i)];
    }
    return s * h / 3.0;
}

struct Curve2Analysis {
    ExprPtr x;
    ExprPtr y;
    std::string var;
};

void analyze_curve2(const Curve2Analysis& a, double t0, const Item& item, Ctx& c, ItemStatus& st, bool tangent, bool normal, bool velocity, bool acceleration, bool circle) {
    const Rgba col = item_color(item);
    ExprPtr dx = d(a.x, a.var);
    ExprPtr dy = d(a.y, a.var);
    if (!dx || !dy) {
        st.note = "Arbor can't differentiate this curve exactly, so tangent and curvature are off.";
        return;
    }
    ExprPtr ddx = d(dx, a.var);
    ExprPtr ddy = d(dy, a.var);
    const NumberType gt = c.env.geo_type;
    Func fx(a.x, { a.var }, false, gt);
    Func fy(a.y, { a.var }, false, gt);
    Func fdx(dx, { a.var }, false, gt);
    Func fdy(dy, { a.var }, false, gt);
    const Vec2 p{ fx.at({ t0 }), fy.at({ t0 }) };
    const Vec2 v{ fdx.at({ t0 }), fdy.at({ t0 }) };
    if (!finite(p)) {
        st.note = "the curve isn't defined at " + a.var + "\xE2\x82\x80";
        return;
    }
    c.scene.s2.dots.push_back(Dot2{ p, col, 5.5f, false, "" });
    st.readouts.push_back(sym_vreadout("point", { a.x, a.y }, { a.var }, { t0 }, c.env));
    st.readouts.push_back(sym_vreadout("velocity", { dx, dy }, { a.var }, { t0 }, c.env));
    ExprPtr speed = call("sqrt", { add(pow(dx, number(2.0)), pow(dy, number(2.0))) });
    st.readouts.push_back(sym_readout("speed", simplify(speed), { a.var }, { t0 }, c.env));
    if (tangent && finite(v)) c.scene.s2.strokes.push_back(line_through(p, v, c.box, faded(c, col, 0.15), 1.8f, false));
    if (normal && finite(v)) c.scene.s2.strokes.push_back(line_through(p, Vec2{ -v.y, v.x }, c.box, faded(c, col, 0.35), 1.6f, true));
    if (velocity && finite(v)) add_arrow2(c.scene.s2, p, p + v, c.env.theme.text, 2.2f);
    if (!ddx || !ddy) return;
    Func fddx(ddx, { a.var }, false, gt);
    Func fddy(ddy, { a.var }, false, gt);
    const Vec2 acc{ fddx.at({ t0 }), fddy.at({ t0 }) };
    if (acceleration && finite(acc)) add_arrow2(c.scene.s2, p, p + acc, rgba(240, 140, 46), 2.2f);
    ExprPtr kappa = simplify(div(sub(mul(dx, ddy), mul(dy, ddx)), pow(add(pow(dx, number(2.0)), pow(dy, number(2.0))), number(1.5))));
    st.readouts.push_back(sym_readout("curvature \xCE\xBA", kappa, { a.var }, { t0 }, c.env));
    const double cross_z = v.x * acc.y - v.y * acc.x;
    const double sp = length(v);
    if (sp > 0.0 && std::fabs(cross_z) > 1e-14) {
        const double k = cross_z / (sp * sp * sp);
        const Vec2 nrm = Vec2{ -v.y, v.x } * (1.0 / sp);
        const Vec2 center = p + nrm * (1.0 / k);
        std::string rsym, csym;
        if (symbols_wanted()) {
            const ExprPtr sp2 = add(pow(dx, number(2.0)), pow(dy, number(2.0)));
            const ExprPtr cr = sub(mul(dx, ddy), mul(dy, ddx));
            rsym = symbol_text(div(pow(sp2, number(1.5)), call("abs", { cr })));
            csym = symbol_vec({ sub(a.x, div(mul(dy, sp2), cr)), add(a.y, div(mul(dx, sp2), cr)) });
        }
        st.readouts.push_back({ "radius 1/|\xCE\xBA|", number_text(1.0 / std::fabs(k)), rsym });
        st.readouts.push_back({ "center of curvature", vec_text({ center.x, center.y }), csym });
        if (circle) {
            add_stroke(c, circle_points(center, 1.0 / std::fabs(k), 160), faded(c, col, 0.3), 1.6f, true);
            c.scene.s2.dots.push_back(Dot2{ center, faded(c, col, 0.3), 4.0f, true, "" });
        }
    }
}

} // namespace

std::string nice_number(double v) {
    if (!std::isfinite(v)) return format_double(v, 8);
    if (std::fabs(v) < 1e-12) return "0";
    const double r = std::round(v);
    if (std::fabs(v - r) < 1e-9 * std::max(1.0, std::fabs(v))) return format_double(r, 15);
    const double pi = 3.14159265358979323846;
    for (int q = 1; q <= 12; ++q) {
        const double p = std::round(v * q / pi);
        if (p != 0.0 && std::fabs(v - p * pi / q) < 1e-9 * std::max(1.0, std::fabs(v))) {
            std::string num = p == 1.0 ? "\xCF\x80" : p == -1.0 ? "-\xCF\x80" : format_double(p, 15) + "\xCF\x80";
            return q == 1 ? num : num + "/" + std::to_string(q);
        }
    }
    for (int q = 2; q <= 12; ++q) {
        const double p = std::round(v * q);
        if (std::fabs(v - p / q) < 1e-9 * std::max(1.0, std::fabs(v))) return format_double(p, 15) + "/" + std::to_string(q);
    }
    return format_double(v, 8);
}

bool grows_toward(Func& f, double c, double side, double width) {
    double prev = -1.0;
    double first = 0.0;
    const double steps[3] = { 1e-4, 1e-7, 1e-10 };
    for (int k = 0; k < 3; ++k) {
        const double d = width * steps[k];
        const double v = f.at({ c + side * d });
        if (!std::isfinite(v)) return k > 0;
        const double m = std::fabs(v);
        if (k == 0) first = m;
        else if (m <= 1.3 * prev) return false;
        prev = m;
    }
    return prev > 2.0 * first + 1e-9;
}

double toward_sign(Func& f, double c, double side, double width) {
    const double v = f.at({ c + side * width * 1e-10 });
    return std::isfinite(v) ? (v > 0 ? 1.0 : -1.0) : 0.0;
}

struct Vertical {
    double at;
    bool up;
    bool down;
};

std::vector<Vertical> vertical_asymptotes(Func& f, double a, double b, double view) {
    std::vector<Vertical> out;
    if (!(b > a)) return out;
    const double width = b - a;
    const std::size_t n = 1600;
    const std::vector<double> ts = linspace(a, b, n + 1);
    const std::vector<double> vs = f.map1(ts);
    auto record = [&](double c) {
        for (const Vertical& v : out) {
            if (std::fabs(v.at - c) < 1e-6 * width) return;
        }
        const bool left = grows_toward(f, c, -1.0, width);
        const bool right = grows_toward(f, c, 1.0, width);
        if (!left && !right) return;
        Vertical v{ c, false, false };
        for (double side : { -1.0, 1.0 }) {
            if (!(side < 0 ? left : right)) continue;
            const double sg = toward_sign(f, c, side, width);
            if (sg > 0) v.up = true;
            if (sg < 0) v.down = true;
        }
        out.push_back(v);
    };
    for (std::size_t i = 0; i + 1 < ts.size() && out.size() < 200; ++i) {
        double ta = ts[i];
        double tb = ts[i + 1];
        double fa = vs[i];
        double fb = vs[i + 1];
        const bool A = std::isfinite(fa);
        const bool B = std::isfinite(fb);
        if (!A && !B) continue;
        if (A != B) {
            for (int k = 0; k < 70; ++k) {
                const double tm = 0.5 * (ta + tb);
                if (tm <= ta || tm >= tb) break;
                const double fm = f.at({ tm });
                if (std::isfinite(fm) == A) ta = tm;
                else tb = tm;
            }
            record(A ? tb : ta);
            continue;
        }
        if (std::fabs(fb - fa) < 0.5 * view) continue;
        double jump = std::fabs(fb - fa);
        int stuck = 0;
        bool broke = false;
        for (int k = 0; k < 90; ++k) {
            const double tm = 0.5 * (ta + tb);
            if (tm <= ta || tm >= tb) {
                broke = stuck > 0;
                break;
            }
            const double fm = f.at({ tm });
            if (!std::isfinite(fm)) {
                record(tm);
                break;
            }
            const double left = std::fabs(fm - fa);
            const double right = std::fabs(fb - fm);
            if (left >= right) {
                tb = tm;
                fb = fm;
            } else {
                ta = tm;
                fa = fm;
            }
            const double next = std::max(left, right);
            if (next < 1e-6 * view) break;
            stuck = next > 0.85 * jump ? stuck + 1 : 0;
            jump = next;
            if (stuck >= 8 && tb - ta < 1e-9 * std::max(1.0, std::fabs(ta))) {
                broke = true;
                break;
            }
        }
        if (broke) record(0.5 * (ta + tb));
    }
    std::sort(out.begin(), out.end(), [](const Vertical& x, const Vertical& y) { return x.at < y.at; });
    return out;
}

struct Tail {
    bool found = false;
    double slope = 0.0;
    double intercept = 0.0;
};

Tail tail_line(Func& f, double sign) {
    Tail t;
    const double xs[6] = { 1.0731e2 + 0.3183, 1.0731e4 + 0.3183, 1.0731e6 + 0.3183, 1.0731e8 + 0.3183, 1.0731e10 + 0.3183, 1.0731e12 + 0.3183 };
    double v[6];
    for (int k = 0; k < 6; ++k) v[k] = f.at({ sign * xs[k] });
    auto close = [](double p, double q, double tol) { return std::isfinite(p) && std::isfinite(q) && std::fabs(p - q) <= tol * std::max(1.0, std::fabs(q)); };
    if (close(v[3], v[5], 1e-5) && close(v[4], v[5], 1e-7)) {
        t.found = true;
        t.intercept = v[5];
        if (std::fabs(t.intercept - std::round(t.intercept)) < 1e-7) t.intercept = std::round(t.intercept);
        return t;
    }
    const double x1 = sign * (1.0731e6 + 0.3183);
    const double x2 = sign * (1.0731e7 + 0.3183);
    const double x3 = sign * (1.0731e8 + 0.3183);
    const double f1 = f.at({ x1 });
    const double f2 = f.at({ x2 });
    const double f3 = f.at({ x3 });
    if (!std::isfinite(f1) || !std::isfinite(f2) || !std::isfinite(f3)) return t;
    const double m1 = (f2 - f1) / (x2 - x1);
    const double m2 = (f3 - f2) / (x3 - x2);
    if (!std::isfinite(m1) || !std::isfinite(m2) || std::fabs(m2) < 1e-6 || std::fabs(m1 - m2) > 1e-6 * std::fabs(m2)) return t;
    const double x0 = sign * (1.0731e4 + 0.3183);
    const double b0 = f.at({ x0 }) - m2 * x0;
    const double b1 = f1 - m2 * x1;
    const double b2 = f3 - m2 * x3;
    if (!close(b1, b2, 1e-4) || !close(b0, b2, 1e-3)) return t;
    t.found = true;
    t.slope = std::fabs(m2 - std::round(m2)) < 1e-8 ? std::round(m2) : m2;
    t.intercept = std::fabs(b2 - std::round(b2)) < 1e-5 ? std::round(b2) : b2;
    return t;
}

std::string line_text(const std::string& other, const std::string& var, const Tail& t) {
    if (t.slope == 0.0) return other + " = " + nice_number(t.intercept);
    std::string s = other + " = " + (t.slope == 1.0 ? std::string() : t.slope == -1.0 ? std::string("-") : nice_number(t.slope)) + var;
    if (t.intercept != 0.0) s += (t.intercept < 0 ? " - " : " + ") + nice_number(std::fabs(t.intercept));
    return s;
}

double golden(Func& f, double a, double b, bool maximum) {
    const double g = 0.6180339887498949;
    double c = b - g * (b - a);
    double d = a + g * (b - a);
    double fc = f.at({ c });
    double fd = f.at({ d });
    for (int k = 0; k < 90 && b - a > 1e-14 * std::max(1.0, std::fabs(a)); ++k) {
        const bool left = maximum ? fc > fd : fc < fd;
        if (left) {
            b = d;
            d = c;
            fd = fc;
            c = b - g * (b - a);
            fc = f.at({ c });
        } else {
            a = c;
            c = d;
            fc = fd;
            d = a + g * (b - a);
            fd = f.at({ d });
        }
    }
    return 0.5 * (a + b);
}

std::string point_list(const std::vector<Vec2>& ps, bool in_y) {
    std::string s;
    const std::size_t shown = std::min<std::size_t>(ps.size(), 6);
    for (std::size_t i = 0; i < shown; ++i) {
        const Vec2 p = in_y ? Vec2{ ps[i].y, ps[i].x } : ps[i];
        s += (i ? ", (" : "(") + nice_number(p.x) + ", " + nice_number(p.y) + ")";
    }
    if (ps.size() > shown) s += " and " + std::to_string(ps.size() - shown) + " more";
    return s;
}

void analyze_function(const Item& item, Ctx& c, ItemStatus& st, Func& fn, bool in_y, const std::string& var, Rgba col) {
    const bool want_extrema = item.option("extrema");
    const bool draw_asymptotes = item.option("asymptotes");
    const double a = in_y ? c.box.ymin : c.box.xmin;
    const double b = in_y ? c.box.ymax : c.box.xmax;
    const double view = in_y ? c.box.xmax - c.box.xmin : c.box.ymax - c.box.ymin;
    const std::string other = in_y ? "x" : "y";
    Func hw(fn.expr(), { var }, false, NumberType::Hardware);
    if (!hw.ok()) return;
    const ExprPtr slope_expr = want_extrema ? d(fn.expr(), var) : nullptr;
    Func slope;
    if (slope_expr) slope = Func(slope_expr, { var }, false, NumberType::Hardware);
    const std::vector<Vertical> verts = vertical_asymptotes(hw, a, b, view);
    const Tail right = tail_line(hw, 1.0);
    const Tail left = tail_line(hw, -1.0);
    const Rgba line_col = faded(c, col, 0.45);
    if (!verts.empty()) {
        std::string s;
        const std::size_t shown = std::min<std::size_t>(verts.size(), 6);
        for (std::size_t i = 0; i < shown; ++i) s += (i ? ", " : "") + var + " = " + nice_number(verts[i].at);
        if (verts.size() > shown) s += " and " + std::to_string(verts.size() - shown) + " more in view";
        st.readouts.push_back({ verts.size() == 1 ? "vertical asymptote" : "vertical asymptotes", s, std::string() });
        if (draw_asymptotes) {
            for (const Vertical& v : verts) {
                const Vec2 p = in_y ? Vec2{ 0.0, v.at } : Vec2{ v.at, 0.0 };
                const Vec2 dir = in_y ? Vec2{ 1.0, 0.0 } : Vec2{ 0.0, 1.0 };
                c.scene.s2.strokes.push_back(line_through(p, dir, c.box, line_col, 1.4f, true));
            }
        }
    }
    auto same = [](const Tail& p, const Tail& q) { return p.found && q.found && p.slope == q.slope && std::fabs(p.intercept - q.intercept) <= 1e-9 * std::max(1.0, std::fabs(p.intercept)); };
    std::vector<Tail> lines;
    if (right.found || left.found) {
        const bool flat_r = right.found && right.slope == 0.0;
        const bool flat_l = left.found && left.slope == 0.0;
        std::string s;
        if (same(right, left)) {
            s = line_text(other, var, right) + " as " + var + " \xE2\x86\x92 \xC2\xB1\xE2\x88\x9E";
            lines.push_back(right);
        } else {
            if (right.found) {
                s = line_text(other, var, right) + " as " + var + " \xE2\x86\x92 \xE2\x88\x9E";
                lines.push_back(right);
            }
            if (left.found) {
                s += (s.empty() ? "" : "; ") + line_text(other, var, left) + " as " + var + " \xE2\x86\x92 -\xE2\x88\x9E";
                lines.push_back(left);
            }
        }
        const bool slant = (right.found && !flat_r) || (left.found && !flat_l);
        const bool flat = flat_r || flat_l;
        st.readouts.push_back({ slant && flat ? "asymptotes at the ends" : slant ? "slant asymptote" : "horizontal asymptote", s, std::string() });
        if (draw_asymptotes) {
            for (const Tail& t : lines) {
                const Vec2 p = in_y ? Vec2{ t.intercept, 0.0 } : Vec2{ 0.0, t.intercept };
                const Vec2 dir = in_y ? Vec2{ t.slope, 1.0 } : Vec2{ 1.0, t.slope };
                c.scene.s2.strokes.push_back(line_through(p, dir, c.box, line_col, 1.4f, true));
            }
        }
    }
    if (!want_extrema || !(b > a)) return;
    const std::size_t n = 1600;
    const std::vector<double> ts = linspace(a, b, n + 1);
    const std::vector<double> vs = hw.map1(ts);
    std::vector<Vec2> maxima;
    std::vector<Vec2> minima;
    auto near_pole = [&](double lo, double hi) {
        for (const Vertical& v : verts) {
            if (v.at >= lo && v.at <= hi) return true;
        }
        return false;
    };
    for (std::size_t i = 1; i + 1 < ts.size(); ++i) {
        const double l = vs[i - 1];
        const double m = vs[i];
        const double r = vs[i + 1];
        if (!std::isfinite(l) || !std::isfinite(m) || !std::isfinite(r)) continue;
        const bool is_max = (m > l && m >= r) || (m >= l && m > r);
        const bool is_min = (m < l && m <= r) || (m <= l && m < r);
        if (!is_max && !is_min) continue;
        if (near_pole(ts[i - 1], ts[i + 1])) continue;
        double t = golden(hw, ts[i - 1], ts[i + 1], is_max);
        if (slope.ok()) {
            double lo = ts[i - 1];
            double hi = ts[i + 1];
            double slo = slope.at({ lo });
            const double shi = slope.at({ hi });
            if (std::isfinite(slo) && std::isfinite(shi) && (slo > 0) != (shi > 0)) {
                for (int k = 0; k < 100; ++k) {
                    const double mid = 0.5 * (lo + hi);
                    if (mid <= lo || mid >= hi) break;
                    const double sm = slope.at({ mid });
                    if (!std::isfinite(sm)) break;
                    if ((sm > 0) == (slo > 0)) {
                        lo = mid;
                        slo = sm;
                    } else {
                        hi = mid;
                    }
                }
                const double root = 0.5 * (lo + hi);
                const double fr = hw.at({ root });
                const double fg = hw.at({ t });
                if (std::isfinite(fr) && (is_max ? fr >= fg - 1e-12 * std::max(1.0, std::fabs(fg)) : fr <= fg + 1e-12 * std::max(1.0, std::fabs(fg)))) t = root;
            }
        }
        const double v = fn.at({ t });
        if (!std::isfinite(v)) continue;
        std::vector<Vec2>& into = is_max ? maxima : minima;
        if (!into.empty() && std::fabs(into.back().x - t) < 1e-7 * (b - a)) continue;
        into.push_back(Vec2{ t, v });
    }
    double best_hi = -INFINITY;
    double best_lo = INFINITY;
    double at_hi = std::nan("");
    double at_lo = std::nan("");
    auto consider = [&](double t, double v) {
        if (!std::isfinite(v)) return;
        if (v > best_hi) {
            best_hi = v;
            at_hi = t;
        }
        if (v < best_lo) {
            best_lo = v;
            at_lo = t;
        }
    };
    for (std::size_t i = 0; i < ts.size(); ++i) consider(ts[i], vs[i]);
    for (const Vec2& p : maxima) consider(p.x, p.y);
    for (const Vec2& p : minima) consider(p.x, p.y);
    bool up = false;
    bool down = false;
    for (const Vertical& v : verts) {
        up = up || v.up;
        down = down || v.down;
    }
    const bool dots = maxima.size() + minima.size() <= 60;
    for (const Vec2& p : maxima) {
        if (dots) c.scene.s2.dots.push_back(Dot2{ in_y ? Vec2{ p.y, p.x } : p, col, 4.0f, true, "" });
    }
    for (const Vec2& p : minima) {
        if (dots) c.scene.s2.dots.push_back(Dot2{ in_y ? Vec2{ p.y, p.x } : p, col, 4.0f, true, "" });
    }
    st.readouts.push_back({ "local maxima", maxima.empty() ? std::string("none in view") : point_list(maxima, in_y), std::string() });
    st.readouts.push_back({ "local minima", minima.empty() ? std::string("none in view") : point_list(minima, in_y), std::string() });
    const bool limited = item.field(var + "min").find_first_not_of(" \t") != std::string::npos || item.field(var + "max").find_first_not_of(" \t") != std::string::npos;
    const std::string span = limited ? "[" + nice_number(a) + ", " + nice_number(b) + "]" : "[" + format_double(a, 4) + ", " + format_double(b, 4) + "]";
    const std::string where = limited ? " on " + span : " in view " + span;
    auto point = [&](double t, double v) {
        const Vec2 p = in_y ? Vec2{ v, t } : Vec2{ t, v };
        return "(" + nice_number(p.x) + ", " + nice_number(p.y) + ")";
    };
    if (up) st.readouts.push_back({ "absolute max" + where, "none: it grows without bound near the asymptote", std::string() });
    else if (std::isfinite(best_hi)) {
        st.readouts.push_back({ "absolute max" + where, other + " = " + nice_number(best_hi) + " at " + point(at_hi, best_hi), std::string() });
        c.scene.s2.dots.push_back(Dot2{ in_y ? Vec2{ best_hi, at_hi } : Vec2{ at_hi, best_hi }, col, 6.0f, false, "max" });
    }
    if (down) st.readouts.push_back({ "absolute min" + where, "none: it falls without bound near the asymptote", std::string() });
    else if (std::isfinite(best_lo)) {
        st.readouts.push_back({ "absolute min" + where, other + " = " + nice_number(best_lo) + " at " + point(at_lo, best_lo), std::string() });
        c.scene.s2.dots.push_back(Dot2{ in_y ? Vec2{ best_lo, at_lo } : Vec2{ at_lo, best_lo }, col, 6.0f, false, "min" });
    }
}

void range_end_dots(Ctx& c, const Item& item, CurveSampler2& s, const Range& range, Rgba col) {
    const double ends[2] = { range.a, range.b };
    const char* fields[2] = { "min", "max" };
    for (int k = 0; k < 2; ++k) {
        if (!limit_style_chosen(item, fields[k]) || !std::isfinite(ends[k])) continue;
        std::vector<Vec2> p;
        s.eval({ ends[k] }, p);
        if (p.empty() || !finite(p[0])) continue;
        c.scene.s2.dots.push_back(Dot2{ p[0], col, 4.5f, !limit_closed(item, fields[k]), "" });
    }
}

void build_function(const Item& item, Ctx& c, ItemStatus& st) {
    ParseResult r = parse_field(item.field("eq"), c.env, { "x", "y" });
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    const Statement& s = r.statement;
    ExprPtr raw;
    bool in_y = false;
    Relation rel = s.relation;
    if (rel == Relation::None) {
        raw = s.lhs;
    } else if (is_var(s.lhs, "y") && !depends_on(s.rhs, "y")) {
        raw = s.rhs;
    } else if (is_var(s.rhs, "y") && !depends_on(s.lhs, "y")) {
        raw = s.lhs;
        rel = flip(rel);
    } else if (is_var(s.lhs, "x") && !depends_on(s.rhs, "x")) {
        raw = s.rhs;
        in_y = true;
    } else if (is_var(s.rhs, "x") && !depends_on(s.lhs, "x")) {
        raw = s.lhs;
        in_y = true;
        rel = flip(rel);
    } else {
        build_implicit(item, c, st);
        return;
    }
    const std::string var = in_y ? "y" : "x";
    if (depends_on(raw, in_y ? "x" : "y")) {
        build_implicit(item, c, st);
        return;
    }
    std::string err;
    ExprPtr f = prepare(raw, c.env, { var }, err);
    if (!f) {
        st.error = err;
        return;
    }
    st.mathtext = std::string(in_y ? "x" : "y") + " " + (rel == Relation::None ? "=" : relation_text(rel)) + " " + to_mathtext(f);
    Func fn(f, { var }, false, c.env.geo_type);
    if (!fn.ok()) {
        st.error = fn.error();
        return;
    }
    const Rgba col = item_color(item);
    Limits lim;
    {
        std::string le;
        if (!read_limits(item, var + "min", var + "max", c.env, lim, le)) {
            st.error = le;
            return;
        }
    }
    Ctx full = c;
    if (lim.lo.set) (in_y ? c.box.ymin : c.box.xmin) = std::max(in_y ? c.box.ymin : c.box.xmin, lim.lo.value);
    if (lim.hi.set) (in_y ? c.box.ymax : c.box.xmax) = std::min(in_y ? c.box.ymax : c.box.xmax, lim.hi.value);
    struct RestoreBox {
        Ctx& c;
        WorldBox2 b;
        ~RestoreBox() { c.box = b; }
    } restore{ c, full.box };
    if ((in_y ? c.box.ymin >= c.box.ymax : c.box.xmin >= c.box.xmax)) {
        st.note = "the domain is out of view";
        return;
    }
    std::vector<Vec2> pts;
    std::vector<double> ts;
    sample_function(c, fn, in_y, pts, ts);
    for (const Limit* l : { &lim.lo, &lim.hi }) {
        if (!l->set) continue;
        const double v = fn.at({ l->value });
        if (!std::isfinite(v)) continue;
        const Vec2 p = in_y ? Vec2{ v, l->value } : Vec2{ l->value, v };
        c.scene.s2.dots.push_back(Dot2{ p, col, 4.5f, !l->closed, "" });
    }
    if (rel != Relation::None && rel != Relation::Equal) {
        const bool below = rel == Relation::Less || rel == Relation::LessEqual;
        double edge;
        if (!in_y) edge = below ? c.box.ymin - (c.box.ymax - c.box.ymin) : c.box.ymax + (c.box.ymax - c.box.ymin);
        else edge = below ? c.box.xmin - (c.box.xmax - c.box.xmin) : c.box.xmax + (c.box.xmax - c.box.xmin);
        shade_between(c, pts, in_y, edge, soft(col, 0.22), -1e300, 1e300);
    }
    add_stroke(c, pts, col, 2.6f, strict(rel));
    {
        auto exact = std::make_shared<Func>(f, std::vector<std::string>{ var }, false, NumberType::Hardware);
        if (exact->ok()) {
            c.scene.s2.strokes.back().exact = [exact](double t) { return exact->at({ t }); };
            c.scene.s2.strokes.back().exact_in_y = in_y;
        }
    }
    analyze_function(item, c, st, fn, in_y, var, col);

    const ExprPtr f1 = d(f, var);
    const ExprPtr f2 = f1 ? d(f1, var) : nullptr;
    if (item.option("derivative")) {
        if (!f1) st.note = "Arbor can't differentiate this exactly.";
        else {
            Func g(f1, { var }, false, c.env.geo_type);
            std::vector<Vec2> dp;
            std::vector<double> dt;
            sample_function(c, g, in_y, dp, dt);
            add_stroke(c, dp, faded(c, col, 0.35), 2.0f, true);
            st.readouts.push_back(formula_readout("f\xE2\x80\xB2", f1));
        }
    }
    if (item.option("second") && f2) {
        Func g(f2, { var }, false, c.env.geo_type);
        std::vector<Vec2> dp;
        std::vector<double> dt;
        sample_function(c, g, in_y, dp, dt);
        add_stroke(c, dp, faded(c, col, 0.55), 1.8f, true);
        st.readouts.push_back(formula_readout("f\xE2\x80\xB3", f2));
    }
    double t0 = 0.0;
    std::string e2;
    const bool want_point = item.option("tangent") || item.option("normal") || item.option("circle") || item.option("area");
    if (!parse_number(item.field("at"), c.env, t0, e2)) {
        if (want_point) st.note = "point: " + e2;
        return;
    }
    if (want_point) offer_param(st, "x\xE2\x82\x80", c.box.xmin, c.box.xmax, t0);
    const double v0 = fn.at({ t0 });
    if (std::isfinite(v0)) {
        const Vec2 p = in_y ? Vec2{ v0, t0 } : Vec2{ t0, v0 };
        if (want_point) c.scene.s2.dots.push_back(Dot2{ p, col, 5.5f, false, "" });
        st.readouts.push_back(sym_readout(std::string("f(") + format_double(t0, 6) + ")", f, { var }, { t0 }, c.env));
        if (f1) st.readouts.push_back(sym_readout(std::string("f\xE2\x80\xB2(") + format_double(t0, 6) + ")", f1, { var }, { t0 }, c.env));
        if (f2) st.readouts.push_back(sym_readout(std::string("f\xE2\x80\xB3(") + format_double(t0, 6) + ")", f2, { var }, { t0 }, c.env));
        if (f1 && f2) {
            Func g1(f1, { var }, false, NumberType::Hardware);
            Func g2(f2, { var }, false, NumberType::Hardware);
            const double s1 = g1.at({ t0 });
            const double s2 = g2.at({ t0 });
            const Vec2 dir = in_y ? Vec2{ s1, 1.0 } : Vec2{ 1.0, s1 };
            ExprPtr kappa = simplify(div(call("abs", { f2 }), pow(add(number(1.0), pow(f1, number(2.0))), number(1.5))));
            st.readouts.push_back(sym_readout("curvature \xCE\xBA", kappa, { var }, { t0 }, c.env));
            const std::string other = in_y ? "x" : "y";
            if (std::isfinite(s1)) {
                const std::string a0 = format_double(t0, 6);
                std::string tan_v = other + " = " + format_double(v0, 6) + (s1 < 0 ? " - " : " + ") + format_double(std::fabs(s1), 6) + "(" + var + " - " + a0 + ")";
                std::string tan_s;
                if (symbols_wanted()) tan_s = other + " = f(" + var + "_{0}) + f\xE2\x80\xB2(" + var + "_{0})(" + var + " - " + var + "_{0})";
                st.readouts.push_back({ "tangent line", tan_v, tan_s });
                if (std::fabs(s1) > 1e-14) st.readouts.push_back({ "normal line", other + " = " + format_double(v0, 6) + (s1 > 0 ? " - " : " + ") + format_double(std::fabs(1.0 / s1), 6) + "(" + var + " - " + a0 + ")", std::string() });
            }
            if (std::isfinite(s2) && std::fabs(s2) > 1e-14) {
                const double q = 1.0 + s1 * s1;
                const Vec2 center = in_y ? Vec2{ v0 + q / s2, t0 - s1 * q / s2 } : Vec2{ t0 - s1 * q / s2, v0 + q / s2 };
                std::string rsym, csym;
                if (symbols_wanted()) {
                    const ExprPtr qe = add(number(1.0), pow(f1, number(2.0)));
                    rsym = symbol_text(div(pow(qe, number(1.5)), call("abs", { f2 })));
                    const ExprPtr along = sub(variable(var), div(mul(f1, qe), f2));
                    const ExprPtr across = add(f, div(qe, f2));
                    csym = in_y ? symbol_vec({ across, along }) : symbol_vec({ along, across });
                }
                st.readouts.push_back({ "radius 1/\xCE\xBA", number_text(std::pow(q, 1.5) / std::fabs(s2)), rsym });
                st.readouts.push_back({ "center of curvature", vec_text({ center.x, center.y }), csym });
            }
            if (item.option("tangent") && std::isfinite(s1)) c.scene.s2.strokes.push_back(line_through(p, dir, c.box, faded(c, col, 0.15), 1.8f, false));
            if (item.option("normal") && std::isfinite(s1)) c.scene.s2.strokes.push_back(line_through(p, Vec2{ -dir.y, dir.x }, c.box, faded(c, col, 0.35), 1.6f, true));
            if (item.option("circle") && std::isfinite(s2) && std::fabs(s2) > 1e-14) {
                const double q = 1.0 + s1 * s1;
                const double rad = std::pow(q, 1.5) / std::fabs(s2);
                Vec2 center = in_y ? Vec2{ v0 + q / s2, t0 - s1 * q / s2 } : Vec2{ t0 - s1 * q / s2, v0 + q / s2 };
                add_stroke(c, circle_points(center, rad, 180), faded(c, col, 0.3), 1.6f, true);
                c.scene.s2.dots.push_back(Dot2{ center, faded(c, col, 0.3), 4.0f, true, "" });
            }
        }
        if (item.option("area")) {
            const double a = std::min(0.0, t0);
            const double b = std::max(0.0, t0);
            std::vector<Vec2> part;
            for (const Vec2& q : pts) {
                const double t = in_y ? q.y : q.x;
                if (t >= a && t <= b) part.push_back(q);
            }
            shade_between(c, part, in_y, 0.0, soft(col, 0.25), a, b);
            const double area = simpson(fn, 0.0, t0, 2000);
            st.readouts.push_back({ std::string("\xE2\x88\xAB from 0 to ") + format_double(t0, 6), number_text(area) });
        }
    }
}

void build_implicit(const Item& item, Ctx& c, ItemStatus& st) {
    ParseResult r = parse_field(item.field("eq"), c.env, { "x", "y" });
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    const Statement& s = r.statement;
    ExprPtr raw = s.relation == Relation::None ? s.lhs : sub(s.lhs, s.rhs);
    std::string err;
    ExprPtr F = prepare(raw, c.env, { "x", "y" }, err);
    if (!F) {
        st.error = err;
        return;
    }
    st.mathtext = to_mathtext(s.lhs) + " " + (s.relation == Relation::None ? "=" : relation_text(s.relation)) + " " + (s.rhs ? to_mathtext(s.rhs) : std::string("0"));
    const ExprPtr Fd = restrict_to_limits(F, item, { "x", "y" }, c.env, err);
    if (!Fd) {
        st.error = err;
        return;
    }
    Func fn(Fd, { "x", "y" }, false, c.env.geo_type);
    if (!fn.ok()) {
        st.error = fn.error();
        return;
    }
    const Rgba col = item_color(item);
    const bool inequality = s.relation != Relation::None && s.relation != Relation::Equal;
    if (inequality || item.option("fill")) {
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
        Func hw(Fd, { "x", "y" }, false, NumberType::Hardware);
        const std::vector<double> v = hw.map2(xs, ys);
        ras.px.resize(v.size());
        const Relation rel = inequality ? s.relation : Relation::Less;
        const Rgba fill = soft(col, 0.24);
        for (std::size_t i = 0; i < v.size(); ++i) {
            bool in = false;
            if (std::isfinite(v[i])) {
                switch (rel) {
                    case Relation::Less: in = v[i] < 0.0; break;
                    case Relation::LessEqual: in = v[i] <= 0.0; break;
                    case Relation::Greater: in = v[i] > 0.0; break;
                    case Relation::GreaterEqual: in = v[i] >= 0.0; break;
                    default: break;
                }
            }
            ras.px[i] = in ? fill : Rgba{ 0, 0, 0, 0 };
        }
        c.scene.s2.rasters.push_back(std::move(ras));
    }
    const int cell = c.env.quality >= 3 ? 2 : 3;
    const double padx = (c.box.xmax - c.box.xmin) * 0.01;
    const double pady = (c.box.ymax - c.box.ymin) * 0.01;
    Grid2 g = sample_grid(fn, c.box.xmin - padx, c.box.xmax + padx, c.box.ymin - pady, c.box.ymax + pady, c.width / cell + 2, c.height / cell + 2);
    std::vector<Vec2> segs = marching_squares(g, 0.0, &fn);
    if (segs.empty() && !inequality) st.note = "no part of this curve is in view";
    add_stroke(c, std::move(segs), col, 2.6f, inequality && strict(s.relation));
    if (item.option("gradient")) {
        ExprPtr gx = d(F, "x");
        ExprPtr gy = d(F, "y");
        if (gx && gy) {
            Func fgx(gx, { "x", "y" }, false, NumberType::Hardware);
            Func fgy(gy, { "x", "y" }, false, NumberType::Hardware);
            const double spacing = (c.box.xmax - c.box.xmin) / 18.0;
            for (double y = std::ceil(c.box.ymin / spacing) * spacing; y <= c.box.ymax; y += spacing) {
                for (double x = std::ceil(c.box.xmin / spacing) * spacing; x <= c.box.xmax; x += spacing) {
                    const Vec2 gv{ fgx.at({ x, y }), fgy.at({ x, y }) };
                    if (!finite(gv) || length(gv) == 0.0) continue;
                    add_arrow2(c.scene.s2, Vec2{ x, y }, Vec2{ x, y } + normalized(gv) * (0.6 * spacing), faded(c, col, 0.45), 1.5f);
                }
            }
            st.readouts.push_back(formula_vreadout("\xE2\x88\x87" "F", { gx, gy }));
        }
    }
}

void build_polar_implicit(const Item& item, Ctx& c, ItemStatus& st, const Statement& s) {
    std::string err;
    ExprPtr lhs = s.lhs;
    ExprPtr rhs = s.rhs ? s.rhs : number(0.0);
    auto fix_t = [](ExprPtr e) { return depends_on(e, "t") && !depends_on(e, "theta") ? substitute(e, { { "t", variable("theta") } }) : e; };
    lhs = fix_t(lhs);
    rhs = fix_t(rhs);
    ExprPtr F = prepare(sub(lhs, rhs), c.env, { "theta", "r" }, err);
    if (!F) {
        st.error = err;
        return;
    }
    st.mathtext = to_mathtext(lhs) + " " + (s.relation == Relation::None ? "=" : relation_text(s.relation)) + " " + to_mathtext(rhs);
    Range range;
    if (!parse_range(item, "min", "max", c.env, range, err)) {
        st.error = err;
        return;
    }
    const double tau = 6.283185307179586;
    if (range.b - range.a > 8.0 * tau) range.b = range.a + 8.0 * tau;
    double rmax = 0.0;
    for (double x : { c.box.xmin, c.box.xmax }) {
        for (double y : { c.box.ymin, c.box.ymax }) rmax = std::max(rmax, std::hypot(x, y));
    }
    Func fn(F, { "theta", "r" }, false, c.env.geo_type);
    if (!fn.ok()) {
        st.error = fn.error();
        return;
    }
    const Rgba col = item_color(item);
    const bool inequality = s.relation != Relation::None && s.relation != Relation::Equal;
    if (inequality) {
        Func hw(F, { "theta", "r" }, false, NumberType::Hardware);
        const int step = c.env.quality >= 3 ? 1 : 2;
        Raster2 ras;
        ras.xmin = c.box.xmin;
        ras.xmax = c.box.xmax;
        ras.ymin = c.box.ymin;
        ras.ymax = c.box.ymax;
        ras.w = std::max(1, c.width / step);
        ras.h = std::max(1, c.height / step);
        const std::size_t count = static_cast<std::size_t>(ras.w) * ras.h;
        std::vector<char> inside(count, 0);
        std::vector<double> rs(count);
        std::vector<double> base(count);
        for (int j = 0; j < ras.h; ++j) {
            for (int i = 0; i < ras.w; ++i) {
                const double x = ras.xmin + (ras.xmax - ras.xmin) * (i + 0.5) / ras.w;
                const double y = ras.ymax - (ras.ymax - ras.ymin) * (j + 0.5) / ras.h;
                const std::size_t k = static_cast<std::size_t>(j) * ras.w + i;
                rs[k] = std::hypot(x, y);
                double th = std::atan2(y, x);
                th = range.a + std::fmod(std::fmod(th - range.a, tau) + tau, tau);
                base[k] = th;
            }
        }
        for (int turn = 0; turn < 9; ++turn) {
            std::vector<double> ths(count);
            bool any = false;
            for (std::size_t k = 0; k < count; ++k) {
                ths[k] = base[k] + tau * turn;
                any = any || ths[k] <= range.b;
            }
            if (!any) break;
            const std::vector<double> v = hw.map2(ths, rs);
            for (std::size_t k = 0; k < count; ++k) {
                if (inside[k] || ths[k] > range.b || !std::isfinite(v[k])) continue;
                bool in = false;
                switch (s.relation) {
                    case Relation::Less: in = v[k] < 0.0; break;
                    case Relation::LessEqual: in = v[k] <= 0.0; break;
                    case Relation::Greater: in = v[k] > 0.0; break;
                    case Relation::GreaterEqual: in = v[k] >= 0.0; break;
                    default: break;
                }
                inside[k] = in ? 1 : 0;
            }
        }
        ras.px.resize(count);
        const Rgba fill = soft(col, 0.24);
        for (std::size_t k = 0; k < count; ++k) ras.px[k] = inside[k] ? fill : Rgba{ 0, 0, 0, 0 };
        c.scene.s2.rasters.push_back(std::move(ras));
    }
    const int turns = std::max(1, static_cast<int>(std::ceil((range.b - range.a) / tau)));
    const int nt = std::min(3000, (c.env.quality >= 3 ? 480 : 300) * turns);
    const int nr = c.env.quality >= 3 ? 360 : 220;
    Grid2 g = sample_grid(fn, range.a, range.b, 0.0, rmax * 1.02, nt, nr);
    std::vector<Vec2> segs = marching_squares(g, 0.0, &fn);
    for (Vec2& p : segs) {
        if (finite(p)) p = Vec2{ p.y * std::cos(p.x), p.y * std::sin(p.x) };
    }
    if (segs.empty() && !inequality) st.note = "no part of this curve is in view";
    add_stroke(c, std::move(segs), col, 2.6f, inequality && strict(s.relation));
}

void build_polar(const Item& item, Ctx& c, ItemStatus& st) {
    ParseResult r = parse_field(item.field("eq"), c.env, { "theta", "r", "t" });
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    ExprPtr raw;
    const bool inequality = r.statement.relation != Relation::None && r.statement.relation != Relation::Equal;
    if (r.statement.relation == Relation::None) raw = r.statement.lhs;
    else if (!inequality && is_var(r.statement.lhs, "r") && !depends_on(r.statement.rhs, "r")) raw = r.statement.rhs;
    else if (!inequality && is_var(r.statement.rhs, "r") && !depends_on(r.statement.lhs, "r")) raw = r.statement.lhs;
    else {
        build_polar_implicit(item, c, st, r.statement);
        return;
    }
    if (depends_on(raw, "t") && !depends_on(raw, "theta")) raw = substitute(raw, { { "t", variable("theta") } });
    std::string err;
    ExprPtr R = prepare(raw, c.env, { "theta" }, err);
    if (!R) {
        st.error = err;
        return;
    }
    st.mathtext = "r = " + to_mathtext(R);
    Range range;
    if (!parse_range(item, "min", "max", c.env, range, err)) {
        st.error = err;
        return;
    }
    const ExprPtr X = simplify(mul(R, call("cos", { variable("theta") })));
    const ExprPtr Y = simplify(mul(R, call("sin", { variable("theta") })));
    Func fx(X, { "theta" }, false, c.env.geo_type);
    Func fy(Y, { "theta" }, false, c.env.geo_type);
    if (!fx.ok()) {
        st.error = fx.error();
        return;
    }
    CurveSampler2 s;
    s.box = c.box;
    s.width = c.width;
    s.height = c.height;
    s.initial = 800;
    s.eval = [&](const std::vector<double>& t, std::vector<Vec2>& out) {
        const std::vector<double> a = fx.map1(t);
        const std::vector<double> b = fy.map1(t);
        out.resize(t.size());
        for (std::size_t i = 0; i < t.size(); ++i) out[i] = Vec2{ a[i], b[i] };
    };
    std::vector<double> ts;
    std::vector<Vec2> pts;
    sample_curve2(s, range.a, range.b, ts, pts);
    const Rgba col = item_color(item);
    add_stroke(c, pts, col, 2.6f, false);
    range_end_dots(c, item, s, range, col);
    double t0 = 0.0;
    if (parse_number(item.field("at"), c.env, t0, err)) {
        offer_param(st, "\xCE\xB8\xE2\x82\x80", range.a, range.b, t0);
        const bool any = item.option("tangent") || item.option("normal") || item.option("circle") || item.option("rays");
        if (any) {
            analyze_curve2(Curve2Analysis{ X, Y, "theta" }, t0, item, c, st, item.option("tangent"), item.option("normal"), false, false, item.option("circle"));
            if (item.option("rays")) {
                const Vec2 p{ fx.at({ t0 }), fy.at({ t0 }) };
                add_stroke(c, { Vec2{ 0.0, 0.0 }, p }, faded(c, col, 0.4), 1.6f, true);
            }
        }
        st.readouts.insert(st.readouts.begin(), sym_readout("r(\xCE\xB8\xE2\x82\x80)", R, { "theta" }, { t0 }, c.env));
    }
}

void build_parametric(const Item& item, Ctx& c, ItemStatus& st) {
    ParseResult r = parse_field(item.field("eq"), c.env, { "t" });
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    ExprPtr v = r.statement.relation == Relation::None ? r.statement.lhs : r.statement.rhs;
    if (component_count(v) != 2) {
        st.error = "write the curve as \xE2\x9F\xA8x(t), y(t)\xE2\x9F\xA9";
        return;
    }
    std::string err;
    ExprPtr X = prepare(component(v, 0), c.env, { "t" }, err);
    ExprPtr Y = X ? prepare(component(v, 1), c.env, { "t" }, err) : nullptr;
    if (!X || !Y) {
        st.error = err;
        return;
    }
    st.mathtext = "\xE2\x9F\xA8" + to_mathtext(X) + ", " + to_mathtext(Y) + "\xE2\x9F\xA9";
    Range range;
    if (!parse_open_range(item, "min", "max", c.env, range, err)) {
        st.error = err;
        return;
    }
    Func fx(X, { "t" }, false, c.env.geo_type);
    Func fy(Y, { "t" }, false, c.env.geo_type);
    if (!fx.ok() || !fy.ok()) {
        st.error = fx.ok() ? fy.error() : fx.error();
        return;
    }
    CurveSampler2 s;
    s.box = c.box;
    s.width = c.width;
    s.height = c.height;
    s.initial = 800;
    s.eval = [&](const std::vector<double>& t, std::vector<Vec2>& out) {
        const std::vector<double> a = fx.map1(t);
        const std::vector<double> b = fy.map1(t);
        out.resize(t.size());
        for (std::size_t i = 0; i < t.size(); ++i) out[i] = Vec2{ a[i], b[i] };
    };
    std::vector<double> ts;
    std::vector<Vec2> pts;
    sample_curve2(s, range.a, range.b, ts, pts);
    const Rgba col = item_color(item);
    Stroke2 stroke;
    stroke.pts = pts;
    stroke.color = col;
    stroke.width = 2.6f;
    if (item.option("curvature_color")) {
        ExprPtr dx = d(X, "t");
        ExprPtr dy = d(Y, "t");
        ExprPtr ddx = dx ? d(dx, "t") : nullptr;
        ExprPtr ddy = dy ? d(dy, "t") : nullptr;
        if (ddx && ddy) {
            ExprPtr kappa = simplify(call("abs", { div(sub(mul(dx, ddy), mul(dy, ddx)), pow(add(pow(dx, number(2.0)), pow(dy, number(2.0))), number(1.5))) }));
            Func fk(kappa, { "t" }, false, NumberType::Hardware);
            const std::vector<double> k = fk.map1(ts);
            const double hi = robust_max(k);
            for (double kv : k) stroke.colors.push_back(colormap("turbo", hi > 0.0 ? kv / hi : 0.0));
            stroke.width = 3.2f;
            st.legend_min = "0";
            st.legend_max = number_text(hi);
            st.legend_map = "turbo";
        }
    }
    c.scene.s2.strokes.push_back(std::move(stroke));
    if (item.option("arrows")) direction_arrows2(c, pts, col);
    range_end_dots(c, item, s, range, col);
    double t0 = 0.0;
    if (parse_number(item.field("at"), c.env, t0, err)) {
        double lo = 0.0, hi = 1.0;
        slider_span(range, t0, lo, hi);
        offer_param(st, "t\xE2\x82\x80", lo, hi, t0);
        analyze_curve2(Curve2Analysis{ X, Y, "t" }, t0, item, c, st, item.option("tangent"), item.option("normal"), item.option("velocity"), item.option("acceleration"), item.option("circle"));
    }
}

void build_curve_value(const Item& item, Ctx& c, ItemStatus& st) {
    ParseResult rf = parse_field(item.field("eq"), c.env, { "x", "y" });
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
    if (component_count(path) != 2) {
        st.error = "write the path as \xE2\x9F\xA8x(t), y(t)\xE2\x9F\xA9";
        return;
    }
    std::string err;
    ExprPtr F = prepare(fraw, c.env, { "x", "y" }, err);
    ExprPtr X = F ? prepare(component(path, 0), c.env, { "t" }, err) : nullptr;
    ExprPtr Y = X ? prepare(component(path, 1), c.env, { "t" }, err) : nullptr;
    if (!F || !X || !Y) {
        st.error = err;
        return;
    }
    Range range;
    if (!parse_range(item, "min", "max", c.env, range, err)) {
        st.error = err;
        return;
    }
    const ExprPtr G = simplify(substitute(F, { { "x", X }, { "y", Y } }));
    st.mathtext = "f(x(t), y(t)) = " + to_mathtext(G);
    Func fx(X, { "t" }, false, c.env.geo_type);
    Func fy(Y, { "t" }, false, c.env.geo_type);
    Func fg(G, { "t" }, false, c.env.geo_type);
    if (!fx.ok() || !fy.ok() || !fg.ok()) {
        st.error = !fx.ok() ? fx.error() : !fy.ok() ? fy.error() : fg.error();
        return;
    }
    if (item.option("contours")) {
        Func ff(F, { "x", "y" }, false, NumberType::Hardware);
        Grid2 g = sample_grid(ff, c.box.xmin, c.box.xmax, c.box.ymin, c.box.ymax, c.width / 5 + 2, c.height / 5 + 2);
        const double lo = robust_min(g.v);
        const double hi = robust_max(g.v);
        for (int k = 1; k < 12; ++k) {
            const double level = lo + (hi - lo) * k / 12.0;
            add_stroke(c, marching_squares(g, level, &ff), soft(c.env.theme.text, 0.25), 1.0f, false);
        }
    }
    CurveSampler2 s;
    s.box = c.box;
    s.width = c.width;
    s.height = c.height;
    s.initial = 800;
    s.eval = [&](const std::vector<double>& t, std::vector<Vec2>& out) {
        const std::vector<double> a = fx.map1(t);
        const std::vector<double> b = fy.map1(t);
        out.resize(t.size());
        for (std::size_t i = 0; i < t.size(); ++i) out[i] = Vec2{ a[i], b[i] };
    };
    std::vector<double> ts;
    std::vector<Vec2> pts;
    sample_curve2(s, range.a, range.b, ts, pts);
    const std::vector<double> gv = fg.map1(ts);
    const double lo = robust_min(gv);
    const double hi = robust_max(gv);
    Stroke2 stroke;
    stroke.pts = pts;
    stroke.width = 4.0f;
    stroke.color = item_color(item);
    for (double v : gv) stroke.colors.push_back(colormap("viridis", hi > lo ? (v - lo) / (hi - lo) : 0.5));
    c.scene.s2.strokes.push_back(std::move(stroke));
    st.legend_min = number_text(lo);
    st.legend_max = number_text(hi);
    st.legend_map = "viridis";
    double t0 = 0.0;
    const bool has_t0 = parse_number(item.field("at"), c.env, t0, err);
    if (has_t0) offer_param(st, "t\xE2\x82\x80", range.a, range.b, t0);
    if (item.option("inset")) {
        Inset2 in;
        in.title = "f(x(t), y(t))";
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
    if (has_t0) {
        const Vec2 p{ fx.at({ t0 }), fy.at({ t0 }) };
        if (finite(p)) c.scene.s2.dots.push_back(Dot2{ p, item_color(item), 5.5f, false, "" });
        st.readouts.push_back(sym_readout("f(x(t\xE2\x82\x80), y(t\xE2\x82\x80))", G, { "t" }, { t0 }, c.env));
        if (ExprPtr dg = d(G, "t")) st.readouts.push_back(sym_readout("d/dt", dg, { "t" }, { t0 }, c.env));
        ExprPtr fxp = d(F, "x");
        ExprPtr fyp = d(F, "y");
        ExprPtr xp = d(X, "t");
        ExprPtr yp = d(Y, "t");
        if (fxp && fyp && xp && yp) {
            const ExprPtr gx = simplify(substitute(fxp, { { "x", X }, { "y", Y } }));
            const ExprPtr gy = simplify(substitute(fyp, { { "x", X }, { "y", Y } }));
            st.readouts.push_back(sym_vreadout("\xE2\x88\x87" "f", { gx, gy }, { "t" }, { t0 }, c.env));
            st.readouts.push_back(sym_vreadout("r\xE2\x80\xB2(t\xE2\x82\x80)", { xp, yp }, { "t" }, { t0 }, c.env));
            st.readouts.push_back(sym_readout("\xE2\x88\x87" "f \xC2\xB7 r\xE2\x80\xB2", simplify(add(mul(gx, xp), mul(gy, yp))), { "t" }, { t0 }, c.env));
            if (item.option("gradient") && finite(p)) {
                Func fgx(gx, { "t" }, false, NumberType::Hardware);
                Func fgy(gy, { "t" }, false, NumberType::Hardware);
                const Vec2 gvec{ fgx.at({ t0 }), fgy.at({ t0 }) };
                if (finite(gvec) && length(gvec) > 0.0) add_arrow2(c.scene.s2, p, p + normalized(gvec) * ((c.box.xmax - c.box.xmin) * 0.12), c.env.theme.text, 2.2f);
            }
        }
    }
}

void build_vector_field(const Item& item, Ctx& c, ItemStatus& st) {
    ParseResult r = parse_field(item.field("eq"), c.env, { "x", "y" });
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    ExprPtr v = r.statement.relation == Relation::None ? r.statement.lhs : r.statement.rhs;
    if (component_count(v) != 2) {
        st.error = "write the field as \xE2\x9F\xA8P(x, y), Q(x, y)\xE2\x9F\xA9";
        return;
    }
    std::string err;
    ExprPtr P = prepare(component(v, 0), c.env, { "x", "y" }, err);
    ExprPtr Q = P ? prepare(component(v, 1), c.env, { "x", "y" }, err) : nullptr;
    if (!P || !Q) {
        st.error = err;
        return;
    }
    st.mathtext = "F = \xE2\x9F\xA8" + to_mathtext(P) + ", " + to_mathtext(Q) + "\xE2\x9F\xA9";
    double density = 21.0;
    parse_number(item.field("density"), c.env, density, err);
    density = std::clamp(density, 3.0, 80.0);
    Func fp(P, { "x", "y" }, false, NumberType::Hardware);
    Func fq(Q, { "x", "y" }, false, NumberType::Hardware);
    if (!fp.ok() || !fq.ok()) {
        st.error = !fp.ok() ? fp.error() : fq.error();
        return;
    }
    const ExprPtr px = d(P, "x");
    const ExprPtr qy = d(Q, "y");
    const ExprPtr qx = d(Q, "x");
    const ExprPtr py = d(P, "y");
    const ExprPtr div_e = px && qy ? simplify(add(px, qy)) : nullptr;
    const ExprPtr curl_e = qx && py ? simplify(sub(qx, py)) : nullptr;
    auto shade = [&](const ExprPtr& e) {
        Func f(e, { "x", "y" }, false, NumberType::Hardware);
        Raster2 ras;
        ras.xmin = c.box.xmin;
        ras.xmax = c.box.xmax;
        ras.ymin = c.box.ymin;
        ras.ymax = c.box.ymax;
        ras.w = std::max(1, c.width / 3);
        ras.h = std::max(1, c.height / 3);
        std::vector<double> xs(static_cast<std::size_t>(ras.w) * ras.h);
        std::vector<double> ys(xs.size());
        for (int j = 0; j < ras.h; ++j) {
            for (int i = 0; i < ras.w; ++i) {
                xs[static_cast<std::size_t>(j) * ras.w + i] = ras.xmin + (ras.xmax - ras.xmin) * (i + 0.5) / ras.w;
                ys[static_cast<std::size_t>(j) * ras.w + i] = ras.ymax - (ras.ymax - ras.ymin) * (j + 0.5) / ras.h;
            }
        }
        const std::vector<double> vals = f.map2(xs, ys);
        std::vector<double> mags;
        for (double q : vals) mags.push_back(std::fabs(q));
        const double top = std::max(1e-12, robust_max(mags));
        for (double q : vals) ras.px.push_back(std::isfinite(q) ? with_alpha(colormap("coolwarm", 0.5 + 0.5 * std::clamp(q / top, -1.0, 1.0)), 0.45) : Rgba{ 0, 0, 0, 0 });
        c.scene.s2.rasters.push_back(std::move(ras));
        st.legend_min = number_text(-top);
        st.legend_max = number_text(top);
        st.legend_map = "coolwarm";
    };
    if (item.option("divergence") && div_e) shade(div_e);
    else if (item.option("curl") && curl_e) shade(curl_e);
    if (div_e) st.readouts.push_back(formula_readout("div F", div_e));
    if (curl_e) st.readouts.push_back(formula_readout("curl F", curl_e));
    const double spacing = (c.box.xmax - c.box.xmin) / density;
    std::vector<double> xs;
    std::vector<double> ys;
    const long long jy0 = static_cast<long long>(std::floor(c.box.ymin / spacing));
    const long long jy1 = static_cast<long long>(std::ceil(c.box.ymax / spacing)) + 1;
    const long long ix0 = static_cast<long long>(std::floor(c.box.xmin / spacing));
    const long long ix1 = static_cast<long long>(std::ceil(c.box.xmax / spacing)) + 1;
    for (long long j = jy0; j <= jy1; ++j) {
        for (long long i = ix0; i <= ix1; ++i) {
            xs.push_back(static_cast<double>(i) * spacing);
            ys.push_back(static_cast<double>(j) * spacing);
        }
    }
    const std::vector<double> pv = fp.map2(xs, ys);
    const std::vector<double> qv = fq.map2(xs, ys);
    std::vector<double> mag(xs.size());
    for (std::size_t i = 0; i < xs.size(); ++i) mag[i] = std::hypot(pv[i], qv[i]);
    const double top = std::max(1e-12, robust_max(mag));
    const bool norm = item.option("normalize");
    const double tiny = std::max(std::numeric_limits<double>::min(), 1e-9 * robust_max(mag));
    for (std::size_t i = 0; i < xs.size(); ++i) {
        if (!std::isfinite(mag[i]) || mag[i] <= tiny) continue;
        const Vec2 dir{ pv[i] / mag[i], qv[i] / mag[i] };
        const double len = norm ? 0.75 * spacing : 0.95 * spacing * std::min(1.0, mag[i] / top);
        const Vec2 from{ xs[i] - dir.x * len * 0.5, ys[i] - dir.y * len * 0.5 };
        add_arrow2(c.scene.s2, from, from + dir * len, colormap("viridis", 0.15 + 0.85 * std::min(1.0, mag[i] / top)), 1.6f);
    }
    if (!st.legend_map.size()) {
        st.legend_min = "0";
        st.legend_max = number_text(top);
        st.legend_map = "viridis";
    }
    if (item.option("streamlines")) {
        const double h = spacing * 0.2;
        const Rgba sc = soft(item_color(item), 0.8);
        auto field = [&](Vec2 p) { return Vec2{ fp.at({ p.x, p.y }), fq.at({ p.x, p.y }) }; };
        const int seeds = std::max(3, static_cast<int>(density / 3));
        for (int sj = 0; sj < seeds; ++sj) {
            for (int si = 0; si < seeds; ++si) {
                const Vec2 seed{ c.box.xmin + (c.box.xmax - c.box.xmin) * (si + 0.5) / seeds, c.box.ymin + (c.box.ymax - c.box.ymin) * (sj + 0.5) / seeds };
                std::vector<Vec2> line;
                for (int dir = -1; dir <= 1; dir += 2) {
                    std::vector<Vec2> half;
                    Vec2 p = seed;
                    for (int step = 0; step < 200; ++step) {
                        auto unit = [&](Vec2 q) { const Vec2 f = field(q); const double m = length(f); return m > 0.0 && std::isfinite(m) ? f * (dir / m) : Vec2{ k_nan, k_nan }; };
                        const Vec2 k1 = unit(p);
                        const Vec2 k2 = unit(p + k1 * (h * 0.5));
                        const Vec2 k3 = unit(p + k2 * (h * 0.5));
                        const Vec2 k4 = unit(p + k3 * h);
                        const Vec2 np = p + (k1 + k2 * 2.0 + k3 * 2.0 + k4) * (h / 6.0);
                        if (!finite(np) || np.x < c.box.xmin || np.x > c.box.xmax || np.y < c.box.ymin || np.y > c.box.ymax) break;
                        half.push_back(np);
                        p = np;
                    }
                    if (dir < 0) {
                        std::reverse(half.begin(), half.end());
                        line.insert(line.end(), half.begin(), half.end());
                        line.push_back(seed);
                    } else {
                        line.insert(line.end(), half.begin(), half.end());
                    }
                }
                add_stroke(c, line, sc, 1.4f, false);
            }
        }
    }
}

void build_contour(const Item& item, Ctx& c, ItemStatus& st) {
    ParseResult r = parse_field(item.field("eq"), c.env, { "x", "y" });
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    ExprPtr raw = r.statement.relation == Relation::None ? r.statement.lhs : r.statement.rhs;
    std::string err;
    ExprPtr F = prepare(raw, c.env, { "x", "y" }, err);
    if (!F) {
        st.error = err;
        return;
    }
    st.mathtext = "f(x, y) = " + to_mathtext(F);
    double levels = 12.0;
    parse_number(item.field("levels"), c.env, levels, err);
    levels = std::clamp(levels, 1.0, 80.0);
    Func fn(F, { "x", "y" }, false, c.env.geo_type);
    if (!fn.ok()) {
        st.error = fn.error();
        return;
    }
    Grid2 g = sample_grid(fn, c.box.xmin, c.box.xmax, c.box.ymin, c.box.ymax, c.width / 3 + 2, c.height / 3 + 2);
    const double lo = robust_min(g.v);
    const double hi = robust_max(g.v);
    if (item.option("shade")) {
        Raster2 ras;
        ras.xmin = c.box.xmin;
        ras.xmax = c.box.xmax;
        ras.ymin = c.box.ymin;
        ras.ymax = c.box.ymax;
        ras.w = g.nx;
        ras.h = g.ny;
        ras.px.resize(g.v.size());
        for (int j = 0; j < g.ny; ++j) {
            for (int i = 0; i < g.nx; ++i) {
                const double v = g.at(i, g.ny - 1 - j);
                ras.px[static_cast<std::size_t>(j) * g.nx + i] = std::isfinite(v) ? with_alpha(colormap("viridis", hi > lo ? (v - lo) / (hi - lo) : 0.5), 0.62) : Rgba{ 0, 0, 0, 0 };
            }
        }
        c.scene.s2.rasters.push_back(std::move(ras));
        st.legend_min = number_text(lo);
        st.legend_max = number_text(hi);
        st.legend_map = "viridis";
    }
    const Rgba col = item_color(item);
    const int n = static_cast<int>(levels);
    for (int k = 1; k <= n; ++k) {
        const double level = lo + (hi - lo) * k / (n + 1.0);
        std::vector<Vec2> seg = marching_squares(g, level, &fn);
        if (item.option("labels") && seg.size() >= 2) {
            const Vec2 at = seg[(seg.size() / 3 / 3) * 3];
            if (finite(at)) add_text2(c.scene.s2, at, format_double(level, 3), c.env.theme.text);
        }
        add_stroke(c, std::move(seg), mix(col, c.env.theme.text, 0.15), 1.5f, false);
    }
    if (item.option("gradient")) {
        ExprPtr gx = d(F, "x");
        ExprPtr gy = d(F, "y");
        if (gx && gy) {
            Func fgx(gx, { "x", "y" }, false, NumberType::Hardware);
            Func fgy(gy, { "x", "y" }, false, NumberType::Hardware);
            const double spacing = (c.box.xmax - c.box.xmin) / 16.0;
            for (double y = std::ceil(c.box.ymin / spacing) * spacing; y <= c.box.ymax; y += spacing) {
                for (double x = std::ceil(c.box.xmin / spacing) * spacing; x <= c.box.xmax; x += spacing) {
                    const Vec2 gv{ fgx.at({ x, y }), fgy.at({ x, y }) };
                    if (!finite(gv) || length(gv) == 0.0) continue;
                    add_arrow2(c.scene.s2, Vec2{ x, y }, Vec2{ x, y } + normalized(gv) * (0.6 * spacing), c.env.theme.text, 1.5f);
                }
            }
            st.readouts.push_back(formula_vreadout("\xE2\x88\x87" "f", { gx, gy }));
        }
    }
}

void build_slope_field(const Item& item, Ctx& c, ItemStatus& st) {
    ParseResult r = parse_field(item.field("eq"), c.env, { "x", "y" });
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    ExprPtr raw = r.statement.relation == Relation::None ? r.statement.lhs : r.statement.rhs;
    std::string err;
    ExprPtr F = prepare(raw, c.env, { "x", "y" }, err);
    if (!F) {
        st.error = err;
        return;
    }
    st.mathtext = "dy/dx = " + to_mathtext(F);
    double density = 25.0;
    parse_number(item.field("density"), c.env, density, err);
    density = std::clamp(density, 5.0, 80.0);
    Func fn(F, { "x", "y" }, false, NumberType::Hardware);
    if (!fn.ok()) {
        st.error = fn.error();
        return;
    }
    const Rgba col = item_color(item);
    const double sx = (c.box.xmax - c.box.xmin) / density;
    std::vector<Vec2> segs;
    const double ratio = (c.box.ymax - c.box.ymin) / c.height / ((c.box.xmax - c.box.xmin) / c.width);
    const double sy = sx * ratio;
    for (double y = std::floor(c.box.ymin / sy) * sy; y <= c.box.ymax + sy; y += sy) {
        for (double x = std::floor(c.box.xmin / sx) * sx; x <= c.box.xmax + sx; x += sx) {
            const double m = fn.at({ x, y });
            if (!std::isfinite(m)) continue;
            Vec2 dir = normalized(Vec2{ 1.0, m / ratio });
            dir.y *= ratio;
            const Vec2 half = dir * (0.32 * sx);
            segs.push_back(Vec2{ x, y } - half);
            segs.push_back(Vec2{ x, y } + half);
            segs.push_back(Vec2{ k_nan, k_nan });
        }
    }
    add_stroke(c, std::move(segs), faded(c, col, 0.35), 1.4f, false);
    if (item.option("solution")) {
        double x0 = 0.0;
        double y0 = 0.0;
        if (parse_number(item.field("x0"), c.env, x0, err) && parse_number(item.field("y0"), c.env, y0, err)) {
            Func fs(F, { "x", "y" }, false, c.env.geo_type);
            const double h = (c.box.xmax - c.box.xmin) / 800.0;
            std::vector<Vec2> line;
            for (int dir = -1; dir <= 1; dir += 2) {
                std::vector<Vec2> half;
                double x = x0;
                double y = y0;
                const double step = h * dir;
                for (int i = 0; i < 4000; ++i) {
                    const double k1 = fs.at({ x, y });
                    const double k2 = fs.at({ x + step / 2, y + step * k1 / 2 });
                    const double k3 = fs.at({ x + step / 2, y + step * k2 / 2 });
                    const double k4 = fs.at({ x + step, y + step * k3 });
                    y += step * (k1 + 2 * k2 + 2 * k3 + k4) / 6.0;
                    x += step;
                    if (!std::isfinite(y) || x < c.box.xmin || x > c.box.xmax || std::fabs(y) > 1e6) break;
                    half.push_back(Vec2{ x, y });
                }
                if (dir < 0) {
                    std::reverse(half.begin(), half.end());
                    line.insert(line.end(), half.begin(), half.end());
                    line.push_back(Vec2{ x0, y0 });
                } else {
                    line.insert(line.end(), half.begin(), half.end());
                }
            }
            add_stroke(c, line, col, 2.6f, false);
            c.scene.s2.dots.push_back(Dot2{ Vec2{ x0, y0 }, col, 5.5f, false, "" });
            st.readouts.push_back(sym_readout("slope at (x\xE2\x82\x80, y\xE2\x82\x80)", F, { "x", "y" }, { x0, y0 }, c.env));
        }
    }
}

void build_points(const Item& item, Ctx& c, ItemStatus& st) {
    const std::vector<std::string> parts = split_top_level(item.field("eq"), ',');
    std::vector<Vec2> pts;
    std::string acc;
    for (const std::string& part : parts) {
        ParseResult r = parse_field(part, c.env, {});
        if (!r.ok) {
            st.error = r.error;
            return;
        }
        ExprPtr v = r.statement.lhs;
        if (component_count(v) != 2) {
            st.error = "each point needs two numbers, like (1, 2)";
            return;
        }
        std::string err;
        ExprPtr ex = prepare(component(v, 0), c.env, {}, err);
        ExprPtr ey = ex ? prepare(component(v, 1), c.env, {}, err) : nullptr;
        if (!ex || !ey) {
            st.error = err;
            return;
        }
        Func fx(ex, {}, false, NumberType::Hardware);
        Func fy(ey, {}, false, NumberType::Hardware);
        pts.push_back(Vec2{ fx.at({}), fy.at({}) });
    }
    const Rgba col = item_color(item);
    if (item.option("join") && pts.size() > 1) add_stroke(c, pts, col, 2.2f, false);
    for (const Vec2& p : pts) c.scene.s2.dots.push_back(Dot2{ p, col, 5.5f, false, item.option("labels") ? "(" + format_double(p.x, 4) + ", " + format_double(p.y, 4) + ")" : std::string() });
    st.readouts.push_back({ "points", std::to_string(pts.size()) });
}

} // namespace build
} // namespace arbor
