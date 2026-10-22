#ifndef ARBOR_BUILD_COMMON_HPP
#define ARBOR_BUILD_COMMON_HPP

#include "arbor/expr.hpp"
#include "arbor/program.hpp"
#include "arbor/scene.hpp"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace arbor {
namespace build {

struct Env {
    FunctionTable functions;
    std::map<std::string, ExprPtr> numbers;
    std::set<std::string> defined;
    std::set<std::string> wanted;
    NumberType geo_type = NumberType::Hardware;
    NumberType value_type = NumberType::Hardware;
    int quality = 2;
    const BuildControl* control = nullptr;
    Theme theme;
    bool degrees = false;

    bool cancelled() const { return control && control->cancelled(); }
};

struct Ctx {
    Env& env;
    Scene& scene;
    WorldBox2 box;
    int width = 800;
    int height = 600;
    View3D view3;
    double range = 5.0;
    double zmin = -5.0;
    double zmax = 5.0;
    double range_y = 5.0;
};

void set_thread_control(const BuildControl* control);
bool build_cancelled();
void set_thread_symbols(bool on);
bool symbols_wanted();
void set_item_deadline(double seconds);
bool item_timed_out();

ParseOptions parse_options(const Env& env, const std::vector<std::string>& vars);
ParseResult parse_field(const std::string& text, const Env& env, const std::vector<std::string>& vars);
ExprPtr prepare(const ExprPtr& e, Env& env, const std::vector<std::string>& vars, std::string& error);
bool parse_number(const std::string& text, Env& env, double& out, std::string& error);

class Func {
public:
    Func() = default;
    Func(const ExprPtr& e, const std::vector<std::string>& vars, bool complex, NumberType type);

    bool ok() const { return m_ok; }
    const std::string& error() const { return m_error; }
    const ExprPtr& expr() const { return m_expr; }

    void eval(std::size_t n, const std::vector<const double*>& in, double* out);
    void eval_complex(std::size_t n, const std::vector<const double*>& re, const std::vector<const double*>& im, double* out_re, double* out_im);
    double at(std::initializer_list<double> in);
    std::vector<double> map1(const std::vector<double>& xs);
    std::vector<double> map2(const std::vector<double>& xs, const std::vector<double>& ys);
    std::vector<double> map3(const std::vector<double>& xs, const std::vector<double>& ys, const std::vector<double>& zs);

private:
    ExprPtr m_expr;
    std::vector<std::string> m_vars;
    Program m_program;
    std::shared_ptr<Evaluator> m_eval;
    bool m_ok = false;
    std::string m_error;
};

std::string value_text(const ExprPtr& e, const std::vector<std::string>& vars, const std::vector<double>& at, Env& env);
std::string complex_value_text(const ExprPtr& e, const std::vector<std::string>& vars, const std::vector<double>& re, const std::vector<double>& im, Env& env);
std::string number_text(double v);
std::string vec_text(const std::vector<double>& v);
std::string symbol_text(const ExprPtr& e);
std::string symbol_vec(const std::vector<ExprPtr>& es);
Readout sym_readout(const std::string& label, const ExprPtr& e, const std::vector<std::string>& vars, const std::vector<double>& at, Env& env);
Readout sym_vreadout(const std::string& label, const std::vector<ExprPtr>& es, const std::vector<std::string>& vars, const std::vector<double>& at, Env& env);
Readout formula_readout(const std::string& label, const ExprPtr& e);
Readout formula_readout(const std::string& label, const std::string& symbol);
Readout formula_vreadout(const std::string& label, const std::vector<ExprPtr>& es);

ExprPtr d(const ExprPtr& e, const std::string& var);
ExprPtr expr_or_numeric(const ExprPtr& e);

Rgba item_color(const Item& item);
Rgba soft(Rgba c, double alpha);

std::vector<double> linspace(double a, double b, std::size_t n);

struct CurveSampler2 {
    std::function<void(const std::vector<double>&, std::vector<Vec2>&)> eval;
    WorldBox2 box;
    int width = 800;
    int height = 600;
    int initial = 600;
    int rounds = 7;
};

void sample_curve2(const CurveSampler2& s, double t0, double t1, std::vector<double>& ts, std::vector<Vec2>& pts);

struct CurveSampler3 {
    std::function<void(const std::vector<double>&, std::vector<Vec3>&)> eval;
    double scale = 10.0;
    int initial = 500;
    int rounds = 6;
};

void sample_curve3(const CurveSampler3& s, double t0, double t1, std::vector<double>& ts, std::vector<Vec3>& pts);

struct Grid2 {
    int nx = 0;
    int ny = 0;
    double x0 = 0.0;
    double x1 = 1.0;
    double y0 = 0.0;
    double y1 = 1.0;
    std::vector<double> v;
    double x(int i) const { return x0 + (x1 - x0) * i / (nx - 1); }
    double y(int j) const { return y0 + (y1 - y0) * j / (ny - 1); }
    double at(int i, int j) const { return v[static_cast<std::size_t>(j) * nx + i]; }
};

Grid2 sample_grid(Func& f, double x0, double x1, double y0, double y1, int nx, int ny);
std::vector<Vec2> marching_squares(const Grid2& g, double level, Func* refine);

struct Grid3 {
    int n = 0;
    double lo[3] = { 0.0, 0.0, 0.0 };
    double hi[3] = { 1.0, 1.0, 1.0 };
    std::vector<double> v;
    Vec3 pos(int i, int j, int k) const;
    double at(int i, int j, int k) const { return v[(static_cast<std::size_t>(k) * n + j) * n + i]; }
};

void marching_tetrahedra(const Grid3& g, std::vector<Vec3>& tris);

void add_arrow2(Scene2& s, Vec2 from, Vec2 to, Rgba color, float width = 2.0f);
void add_text2(Scene2& s, Vec2 at, const std::string& text, Rgba color);
Stroke2 line_through(Vec2 p, Vec2 dir, const WorldBox2& box, Rgba color, float width, bool dashed);
std::vector<Vec2> circle_points(Vec2 c, double r, int n);

struct Range {
    double a = 0.0;
    double b = 1.0;
};

bool parse_range(const Item& item, const char* lo, const char* hi, Env& env, Range& out, std::string& error);
bool parse_open_range(const Item& item, const char* lo, const char* hi, Env& env, Range& out, std::string& error);
bool range_unbounded(const Range& r);

struct OpenMap {
    int mode = 0;
    double a = 0.0;
    double b = 0.0;
    double sign = 1.0;
    double u0 = 0.0;
    double u1 = 1.0;
    double operator()(double u) const;
    double dt(double u) const;
};

bool open_map(double t0, double t1, OpenMap& m, double eps = 1e-7);

struct Integral {
    double value = 0.0;
    bool ok = true;
    bool finite = true;
};

Integral integrate_open(const std::function<double(double)>& f, double a, double b, int panels = 2000);
void slider_span(const Range& r, double t0, double& lo, double& hi);
void direction_arrows2(Ctx& c, const std::vector<Vec2>& pts, Rgba color);
void direction_arrows3(Ctx& c, const std::vector<Vec3>& pts, Rgba color);
void offer_param(ItemStatus& st, const std::string& label, double lo, double hi, double value);

struct Limit {
    bool set = false;
    double value = 0.0;
    bool closed = true;
};

struct Limits {
    Limit lo;
    Limit hi;
};

bool read_limits(const Item& item, const std::string& lo_field, const std::string& hi_field, Env& env, Limits& out, std::string& error);
bool limit_closed(const Item& item, const std::string& field);
bool limit_style_chosen(const Item& item, const std::string& field);
ExprPtr restrict_to_limits(const ExprPtr& f, const Item& item, const std::vector<std::string>& vars, Env& env, std::string& error);
double robust_min(std::vector<double> v);
double robust_max(std::vector<double> v);

void build_function(const Item& item, Ctx& c, ItemStatus& st);
void build_implicit(const Item& item, Ctx& c, ItemStatus& st);
void build_polar(const Item& item, Ctx& c, ItemStatus& st);
void build_parametric(const Item& item, Ctx& c, ItemStatus& st);
void build_curve_value(const Item& item, Ctx& c, ItemStatus& st);
void build_vector_field(const Item& item, Ctx& c, ItemStatus& st);
void build_contour(const Item& item, Ctx& c, ItemStatus& st);
void build_slope_field(const Item& item, Ctx& c, ItemStatus& st);
void build_points(const Item& item, Ctx& c, ItemStatus& st);

void build_surface(const Item& item, Ctx& c, ItemStatus& st);
void build_implicit3d(const Item& item, Ctx& c, ItemStatus& st);
void build_curve3d(const Item& item, Ctx& c, ItemStatus& st);
void build_param_surface(const Item& item, Ctx& c, ItemStatus& st);
void build_spherical(const Item& item, Ctx& c, ItemStatus& st);
void build_vector_field3d(const Item& item, Ctx& c, ItemStatus& st);
void build_points3d(const Item& item, Ctx& c, ItemStatus& st);
void build_cylindrical(const Item& item, Ctx& c, ItemStatus& st);
void build_cylindrical_implicit_item(const Item& item, Ctx& c, ItemStatus& st);
void region_caps(Ctx& c, Func& f, Relation rel, Rgba color);
void build_region2d(const Item& item, Ctx& c, ItemStatus& st);
void build_between2d(const Item& item, Ctx& c, ItemStatus& st);
void build_polar_region(const Item& item, Ctx& c, ItemStatus& st);
void build_region3d(const Item& item, Ctx& c, ItemStatus& st);
void build_between3d(const Item& item, Ctx& c, ItemStatus& st);
void build_curve_value3d(const Item& item, Ctx& c, ItemStatus& st);

void build_domain(const Item& item, Ctx& c, ItemStatus& st);
void build_conformal(const Item& item, Ctx& c, ItemStatus& st);
void build_complex_surface(const Item& item, Ctx& c, ItemStatus& st);
void build_complex_curve(const Item& item, Ctx& c, ItemStatus& st);
void build_riemann(const Item& item, Ctx& c, ItemStatus& st);
void build_transform2d(const Item& item, Ctx& c, ItemStatus& st);
void build_transform3d(const Item& item, Ctx& c, ItemStatus& st);

struct PlaneCurve {
    enum class Kind { OfX, OfY, Parametric };
    Kind kind = Kind::OfX;
    ExprPtr x;
    ExprPtr y;
    Statement original;
};

bool plane_curve(const std::string& text, Env& env, PlaneCurve& out, std::string& error);
bool plane_line(const std::string& text, Env& env, Vec2& point, Vec2& dir, std::string& error);

void build_revolution(const Item& item, Ctx& c, ItemStatus& st);
void build_rotation(const Item& item, Ctx& c, ItemStatus& st);
void build_ode(const Item& item, Ctx& c, ItemStatus& st);
void build_piecewise(const Item& item, Ctx& c, ItemStatus& st);
void build_sequence(const Item& item, Ctx& c, ItemStatus& st);
void build_escape(const Item& item, Ctx& c, ItemStatus& st);
void build_turmite(const Item& item, Ctx& c, ItemStatus& st);
void build_brachistochrone(const Item& item, Ctx& c, ItemStatus& st);
void build_pde(const Item& item, Ctx& c, ItemStatus& st);
void build_pde_steady(const Item& item, Ctx& c, ItemStatus& st);
void build_pde_march(const Item& item, Ctx& c, ItemStatus& st);

void surface_mesh(Ctx& c, const std::function<void(const std::vector<double>&, const std::vector<double>&, std::vector<Vec3>&)>& pos, double u0, double u1, double v0, double v1, int nu, int nv, Mesh3& mesh, std::vector<Vec3>& grid);
void mesh_lines(Ctx& c, const std::vector<Vec3>& grid, int nu, int nv, int every, Rgba color);
void finish_normals(Mesh3& mesh);

} // namespace build
} // namespace arbor

#endif // ARBOR_BUILD_COMMON_HPP
