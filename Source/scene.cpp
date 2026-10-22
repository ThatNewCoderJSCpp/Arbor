#include "build_common.hpp"

#include "arbor/kinds.hpp"

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <map>

namespace arbor {

using namespace build;

namespace {

using Builder = void (*)(const Item&, Ctx&, ItemStatus&);

const std::map<std::string, Builder>& builders() {
    static const std::map<std::string, Builder> all = {
        { "function", build_function },
        { "implicit", build_implicit },
        { "polar", build_polar },
        { "parametric", build_parametric },
        { "curve_value", build_curve_value },
        { "vector_field", build_vector_field },
        { "contour", build_contour },
        { "slope_field", build_slope_field },
        { "points", build_points },
        { "surface", build_surface },
        { "implicit3d", build_implicit3d },
        { "curve3d", build_curve3d },
        { "param_surface", build_param_surface },
        { "spherical", build_spherical },
        { "vector_field3d", build_vector_field3d },
        { "points3d", build_points3d },
        { "cylindrical", build_cylindrical },
        { "implicit_cylindrical", build_cylindrical_implicit_item },
        { "mixed3d", build_cylindrical_implicit_item },
        { "region2d", build_region2d },
        { "brachistochrone", build_brachistochrone },
        { "pde", build_pde },
        { "pde_steady", build_pde_steady },
        { "pde_march", build_pde_march },
        { "between2d", build_between2d },
        { "polar_region", build_polar_region },
        { "region3d", build_region3d },
        { "between3d", build_between3d },
        { "curve_value3d", build_curve_value3d },
        { "domain", build_domain },
        { "conformal", build_conformal },
        { "complex_surface", build_complex_surface },
        { "complex_curve", build_complex_curve },
        { "riemann", build_riemann },
        { "transform2d", build_transform2d },
        { "transform3d", build_transform3d },
        { "ode", build_ode },
        { "revolution", build_revolution },
        { "rotation", build_rotation },
        { "piecewise", build_piecewise },
        { "sequence", build_sequence },
        { "escape", build_escape },
        { "turmite", build_turmite },
    };
    return all;
}

const std::set<std::string>& reserved_names() {
    static const std::set<std::string> r = { "x", "y", "z", "t", "u", "v", "w", "r", "i", "e", "pi", "tau" };
    return r;
}

void define_all(const Graph& g, Env& env, std::vector<ItemStatus>& status) {
    for (int pass = 0; pass < 2; ++pass) {
        for (const Item& item : g.items) {
            if (item.kind != "define") continue;
            auto st = std::find_if(status.begin(), status.end(), [&](const ItemStatus& s) { return s.id == item.id; });
            ParseResult r = parse_field(item.field("eq"), env, {});
            if (!r.ok) {
                if (pass == 1 && st != status.end()) st->error = r.error;
                continue;
            }
            const Statement& s = r.statement;
            if (s.is_function_definition) {
                env.functions[s.defines] = UserFunction{ s.params, s.rhs };
                env.defined.insert(s.defines);
                if (pass == 1 && st != status.end()) st->mathtext = to_mathtext(s.lhs) + " = " + to_mathtext(s.rhs);
                continue;
            }
            if (s.is_value_definition) {
                if (reserved_names().count(s.defines)) {
                    if (pass == 1 && st != status.end()) st->error = "'" + s.defines + "' is a variable name; pick another name";
                    continue;
                }
                env.defined.insert(s.defines);
                if (is_constant(s.rhs)) {
                    std::string err;
                    Env scratch = env;
                    ExprPtr e = prepare(s.rhs, scratch, {}, err);
                    double v = std::nan("");
                    if (e) {
                        Program p = compile(e, {}, false, err);
                        if (p.valid) v = make_hardware_evaluator(p)->real1({});
                    }
                    if (std::isfinite(v)) {
                        env.numbers[s.defines] = s.rhs->op == Op::Number ? s.rhs : number(v);
                        env.functions.erase(s.defines);
                        if (pass == 1 && st != status.end()) st->note = "slider";
                    }
                } else {
                    env.functions[s.defines] = UserFunction{ {}, s.rhs };
                    env.numbers.erase(s.defines);
                    if (pass == 1 && st != status.end()) {
                        std::string err;
                        Env scratch = env;
                        ExprPtr e = prepare(s.rhs, scratch, {}, err);
                        if (e && is_constant(e)) st->readouts.push_back({ s.defines, value_text(e, {}, {}, scratch) });
                    }
                }
                if (pass == 1 && st != status.end()) st->mathtext = s.defines + " = " + to_mathtext(s.rhs);
                continue;
            }
            if (pass == 1 && st != status.end()) st->error = "write a definition like a = 2 or f(x) = x^2";
        }
    }
}

bool surface_z_range(const Graph& g, Env& env, double range, Vec3 target, double& lo, double& hi) {
    std::vector<double> zs;
    for (const Item& item : g.items) {
        if (!item.visible) continue;
        if (item.kind != "surface" && item.kind != "complex_surface") continue;
        const bool complex = item.kind == "complex_surface";
        ParseResult r = parse_field(item.field("eq"), env, complex ? std::vector<std::string>{ "z" } : std::vector<std::string>{ "x", "y" });
        if (!r.ok) continue;
        ExprPtr e = r.statement.lhs;
        if (r.statement.relation == Relation::Equal) e = (r.statement.lhs->op == Op::Variable) ? r.statement.rhs : r.statement.lhs;
        std::string err;
        Env scratch = env;
        ExprPtr F = prepare(e, scratch, complex ? std::vector<std::string>{ "z" } : std::vector<std::string>{ "x", "y" }, err);
        if (!F) continue;
        Func f(F, complex ? std::vector<std::string>{ "z" } : std::vector<std::string>{ "x", "y" }, complex, NumberType::Hardware);
        if (!f.ok()) continue;
        const int n = 41;
        std::vector<double> xs;
        std::vector<double> ys;
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                xs.push_back(target.x - range + 2.0 * range * i / (n - 1));
                ys.push_back(target.y - range + 2.0 * range * j / (n - 1));
            }
        }
        std::vector<double> out(xs.size());
        if (complex) {
            std::vector<double> oi(xs.size());
            f.eval_complex(xs.size(), { xs.data() }, { ys.data() }, out.data(), oi.data());
            std::string mode = item.field("height");
            for (std::size_t k = 0; k < out.size(); ++k) {
                if (mode == "real") continue;
                if (mode == "imag") out[k] = oi[k];
                else if (mode == "log") out[k] = std::log(std::hypot(out[k], oi[k]));
                else out[k] = std::hypot(out[k], oi[k]);
            }
        } else {
            f.eval(xs.size(), { xs.data(), ys.data() }, out.data());
        }
        for (double z : out) {
            if (std::isfinite(z)) zs.push_back(z);
        }
    }
    if (zs.size() < 8) return false;
    lo = robust_min(zs);
    hi = robust_max(zs);
    if (!(hi > lo)) {
        lo -= 1.0;
        hi += 1.0;
    }
    const double pad = 0.08 * (hi - lo);
    lo -= pad;
    hi += pad;
    return true;
}

} // namespace

WorldBox2 world_box(const View2D& v, int width, int height) {
    WorldBox2 b;
    const double sx = std::max(1e-12, v.scale);
    const double sy = v.equal_axes ? sx : std::max(1e-12, v.scale_y);
    b.xmin = v.cx - 0.5 * width / sx;
    b.xmax = v.cx + 0.5 * width / sx;
    b.ymin = v.cy - 0.5 * height / sy;
    b.ymax = v.cy + 0.5 * height / sy;
    return b;
}

std::vector<std::string> split_top_level(const std::string& text, char sep) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char ch : text) {
        if (ch == '(' || ch == '[' || ch == '{' || ch == '<') ++depth;
        else if (ch == ')' || ch == ']' || ch == '}' || ch == '>') depth = std::max(0, depth - 1);
        if (ch == sep && depth == 0) {
            out.push_back(cur);
            cur.clear();
            continue;
        }
        cur += ch;
    }
    if (cur.find_first_not_of(" \t") != std::string::npos) out.push_back(cur);
    for (std::string& s : out) {
        const std::size_t a = s.find_first_not_of(" \t");
        const std::size_t b = s.find_last_not_of(" \t");
        s = a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    }
    return out;
}

Scene build_scene(const BuildRequest& request, const BuildControl& control, NumberType geometry_type) {
    const auto started = std::chrono::steady_clock::now();
    const Graph& g = request.graph;
    Scene scene;
    scene.space = g.space;
    scene.revision = request.revision;
    scene.built_view = g.view2;
    scene.built_width = request.width;
    scene.built_height = request.height;
    scene.plotted_with = geometry_type;
    scene.precise_pass = geometry_type != NumberType::Hardware;

    set_thread_control(&control);
    set_thread_symbols(request.symbols);
    struct Reset {
        ~Reset() {
            set_thread_control(nullptr);
            set_thread_symbols(true);
        }
    } reset;
    Env env;
    env.geo_type = geometry_type;
    env.value_type = request.value_type;
    env.quality = request.quality;
    env.control = &control;
    env.theme = request.theme;
    env.degrees = g.degrees;

    for (const Item& item : g.items) {
        ItemStatus st;
        st.id = item.id;
        parse_color(item.color, st.swatch);
        scene.status.push_back(st);
    }
    define_all(g, env, scene.status);

    Ctx c{ env, scene, WorldBox2{}, 800, 600, View3D{}, 5.0, -5.0, 5.0 };
    c.width = std::max(1, request.width);
    c.height = std::max(1, request.height);
    c.box = world_box(g.view2, c.width, c.height);
    c.view3 = g.view3;
    if (g.space == Space::Plane) {
        auto bound = [&](const std::string& text, double& out) {
            if (text.empty()) return false;
            std::string e;
            double v = 0.0;
            if (!parse_number(text, env, v, e) || !std::isfinite(v)) return false;
            out = v;
            return true;
        };
        bool any = false;
        any = bound(g.view2.bound_xmin, scene.s2.clip_xmin) || any;
        any = bound(g.view2.bound_xmax, scene.s2.clip_xmax) || any;
        any = bound(g.view2.bound_ymin, scene.s2.clip_ymin) || any;
        any = bound(g.view2.bound_ymax, scene.s2.clip_ymax) || any;
        scene.s2.clipped = any;
    }
    c.range = std::max(1e-9, g.view3.range);
    c.range_y = g.view3.range_y > 0.0 ? std::max(1e-9, g.view3.range_y) : c.range;
    c.zmin = g.view3.target.z - g.view3.zrange;
    c.zmax = g.view3.target.z + g.view3.zrange;
    if (g.space == Space::Solid && g.view3.auto_z) {
        double lo = 0.0;
        double hi = 0.0;
        if (surface_z_range(g, env, c.range, g.view3.target, lo, hi)) {
            c.zmin = std::max(lo, g.view3.target.z - 50.0 * c.range);
            c.zmax = std::min(hi, g.view3.target.z + 50.0 * c.range);
            if (c.zmax - c.zmin < 1e-6) {
                c.zmin -= 1.0;
                c.zmax += 1.0;
            }
        } else {
            c.zmin = g.view3.target.z - c.range;
            c.zmax = g.view3.target.z + c.range;
        }
    }
    scene.s3.zmin = c.zmin;
    scene.s3.zmax = c.zmax;
    scene.s3.has_z = true;

    std::size_t done = 0;
    for (std::size_t k = 0; k < g.items.size(); ++k) {
        if (control.cancelled()) break;
        const Item& item = g.items[k];
        ItemStatus& st = scene.status[k];
        if (item.kind == "define") continue;
        const KindInfo* info = find_kind(item.kind);
        auto it = builders().find(item.kind);
        if (!info || it == builders().end()) {
            st.error = "unknown kind '" + item.kind + "'";
            continue;
        }
        if (info->space != g.space) {
            st.error = std::string("this belongs in a ") + (info->space == Space::Solid ? "3D" : "2D") + " graph";
            continue;
        }
        if (!item.visible) continue;
        const std::size_t before_wanted = env.wanted.size();
        (void)before_wanted;
        int branch = 0;
        {
            const std::string text = item.field("branch");
            char* end = nullptr;
            const long v = std::strtol(text.c_str(), &end, 10);
            if (end != text.c_str()) branch = static_cast<int>(std::clamp(v, -1000L, 1000L));
        }
        CompileBranch branch_scope(branch);
        set_item_deadline(scene.precise_pass ? 0.0 : 20.0);
        try {
            it->second(item, c, st);
        } catch (const std::exception& e) {
            st.error = std::string("couldn't draw this: ") + e.what();
        } catch (...) {
            st.error = "couldn't draw this: something went wrong inside Arbor";
        }
        if (item_timed_out() && !control.cancelled()) {
            st.error = "this took longer than 20 seconds and was stopped; try a smaller range or less detail";
        }
        set_item_deadline(0.0);
        ++done;
        if (control.progress) control.progress(static_cast<double>(done) / std::max<std::size_t>(1, g.items.size()));
    }

    for (const std::string& n : env.wanted) {
        if (reserved_names().count(n) || env.defined.count(n) || env.numbers.count(n)) continue;
        if (find_function(n) || is_builtin_constant(n)) continue;
        scene.wanted_sliders.push_back(n);
    }
    scene.build_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return scene;
}

} // namespace arbor
