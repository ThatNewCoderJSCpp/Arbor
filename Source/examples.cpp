#include "arbor/examples.hpp"

#include <functional>
#include <map>

namespace arbor {

namespace {

using Fields = std::map<std::string, std::string>;
using Options = std::map<std::string, bool>;

Item& put(Graph& g, const std::string& kind, const std::string& eq, const Fields& fields = {}, const Options& options = {}) {
    Item& it = g.add_item(kind);
    it.fields["eq"] = eq;
    for (const auto& [k, v] : fields) it.fields[k] = v;
    for (const auto& [k, v] : options) it.options[k] = v;
    return it;
}

void pde_view(Graph& g, double cx, double cy, double rx, double ry, double cz = 0.0, double rz = 0.0) {
    g.view3.target = Vec3{ cx, cy, cz };
    g.view3.range = rx;
    g.view3.range_y = ry > 0.0 ? ry : 0.0;
    if (rz > 0.0) {
        g.view3.auto_z = false;
        g.view3.zrange = rz;
    }
}

struct Recipe {
    Example info;
    std::string first_kind;
    std::function<void(Graph&)> fill;
};

const std::vector<Recipe>& recipes() {
    static const std::vector<Recipe> all = {
        { { "calculus", "2d", "Tangent and curvature", "A cubic with its derivative, tangent, normal and osculating circle at a slider point." }, "function", [](Graph& g) {
            g.items.clear();
            put(g, "define", "a = 1");
            put(g, "function", "y = x^3/6 - x", { { "at", "a" } }, { { "derivative", true }, { "tangent", true }, { "normal", true }, { "circle", true } });
            g.sliders.push_back(Slider{ "a", 1.0, -3.0, 3.0, 0.05 });
        } },
        { { "rose", "2d", "Polar rose", "r = cos(kθ) with a slider for k." }, "polar", [](Graph& g) {
            g.items.clear();
            put(g, "define", "k = 4");
            put(g, "polar", "r = 3cos(k\xCE\xB8)", { { "max", "2pi" }, { "at", "0.3" } }, { { "tangent", true } });
            g.view2.scale = 70.0;
        } },
        { { "lissajous", "2d", "Lissajous curve", "A parametric curve colored by curvature, with velocity and acceleration." }, "parametric", [](Graph& g) {
            g.items.clear();
            put(g, "parametric", "<3cos(3t), 3sin(2t)>", { { "at", "0.7" } }, { { "velocity", true }, { "acceleration", true }, { "curvature_color", true }, { "circle", true } });
            g.view2.scale = 60.0;
        } },
        { { "level", "2d", "Level curves", "An implicit curve, a region and a contour map of the same function." }, "contour", [](Graph& g) {
            g.items.clear();
            put(g, "contour", "sin(x)*cos(y) + x/5", { { "levels", "14" } }, { { "shade", true }, { "gradient", true } });
            put(g, "implicit", "sin(x)*cos(y) + x/5 = 0.5");
        } },
        { { "flow", "2d", "Vortex flow", "A vector field with flow lines and curl shading." }, "vector_field", [](Graph& g) {
            g.items.clear();
            put(g, "vector_field", "<-y + x/4, x - y/4>", { { "density", "19" } }, { { "streamlines", true }, { "curl", true } });
        } },
        { { "logistic", "2d", "Slope field", "dy/dx = y(1 - y/3) with the solution through a point." }, "slope_field", [](Graph& g) {
            g.items.clear();
            put(g, "slope_field", "y(1 - y/3)", { { "x0", "-4" }, { "y0", "0.2" } });
            g.view2.scale = 50.0;
        } },
        { { "pendulum", "2d", "Damped pendulum", "y\xE2\x80\xB3 = -sin(y) - 0.15y\xE2\x80\xB2, a second-order ODE, with y\xE2\x80\xB2 and its phase plane." }, "ode", [](Graph& g) {
            g.items.clear();
            put(g, "ode", "y'' = -sin(y) - 0.15y'", { { "x0", "0" }, { "init", "2.5, 0" }, { "min", "0" }, { "max", "30" }, { "at", "4" } }, { { "derivatives", true }, { "phase", true } });
            g.view2.cx = 14.0;
            g.view2.scale = 28.0;
        } },
        { { "collatz", "misc", "3k + 1", "The Collatz sequence from 27: halve even numbers, otherwise take 3a + 1, until it reaches 1." }, "sequence", [](Graph& g) {
            g.items.clear();
            put(g, "sequence", "a mod 2 = 0", { { "then", "a/2" }, { "else", "3a + 1" }, { "start", "27" }, { "steps", "300" }, { "stop", "a = 1" } });
        } },
        { { "mandelbrot", "misc", "Mandelbrot set", "z \xE2\x86\xA6 z\xC2\xB2 + c from z = 0, colored by how fast each c escapes." }, "escape", [](Graph& g) {
            g.items.clear();
            put(g, "escape", "z^2 + c", { { "while", "|z| <= 2" }, { "max", "150" } });
        } },
        { { "julia", "misc", "Julia set", "The same rule with c fixed at -0.8 + 0.156i, starting from every point." }, "escape", [](Graph& g) {
            g.items.clear();
            put(g, "escape", "z^2 + c", { { "while", "|z| <= 2" }, { "max", "200" }, { "c", "-0.8 + 0.156i" } }, { { "julia", true } });
            g.view2.cx = 0.0;
            g.view2.scale = 220.0;
        } },
        { { "langton", "misc", "Langton's ant", "11,000 steps: chaos for about 10,000 steps, then the endless highway." }, "turmite", [](Graph& g) {
            g.items.clear();
            put(g, "turmite", "cell = 0", { { "then", "R F M" }, { "else", "L F M" }, { "steps", "11000" }, { "cell", "0.1" } });
        } },
        { { "twopiece", "misc", "Piecewise", "A function built from two formulas: x\xC2\xB2 left of 0, \xE2\x88\x9Ax to the right." }, "piecewise", [](Graph& g) {
            g.items.clear();
            put(g, "piecewise", "x < 0", { { "then", "x^2" }, { "else", "sqrt(x)" } });
            g.view2.scale = 60.0;
        } },
        { { "heat-rod", "pde", "Heat in a rod", "u_t = \xC2\xBD" "u_xx with the ends held at 0: the wiggles die first, then the hump sinks." }, "pde", [](Graph& g) {
            g.items.clear();
            put(g, "pde", "u_t = 0.5u_xx", { { "init", "cos(x/2) + 0.6sin(2x)" }, { "tmax", "4" }, { "at", "0.5" } }, { { "surface", true }, { "profile", true }, { "height_color", true } });
            pde_view(g, 0.0, 2.0, 3.6, 2.2);
        } },
        { { "string", "pde", "Plucked string", "u_tt = u_xx from a bump at rest: it splits in two, bounces off the fixed ends upside down and comes back." }, "pde", [](Graph& g) {
            g.items.clear();
            put(g, "pde", "u_tt = u_xx", { { "init", "exp(-4x^2), 0" }, { "tmax", "12" }, { "at", "2" } }, { { "surface", true }, { "profile", true }, { "height_color", true } });
            pde_view(g, 0.0, 6.0, 3.6, 6.2);
        } },
        { { "burgers", "pde", "Burgers' equation", "u_t + uu_x = 0.02u_xx on a loop: the fast part of a sine wave catches the slow part and forms a shock." }, "pde", [](Graph& g) {
            g.items.clear();
            put(g, "pde", "u_t + uu_x = 0.02u_xx", { { "init", "sin(x)" }, { "edge_xmin", "periodic" }, { "edge_xmax", "periodic" }, { "tmax", "4" }, { "at", "2" } }, { { "surface", true }, { "profile", true }, { "height_color", true } });
            pde_view(g, 0.0, 2.0, 3.6, 2.2);
        } },
        { { "kdv", "pde", "Two solitons (KdV)", "u_t + 6uu_x + u_xxx = 0: a tall fast soliton overtakes a short slow one and both come out unchanged." }, "pde", [](Graph& g) {
            g.items.clear();
            put(g, "pde", "u_t + 6uu_x + u_xxx = 0", { { "init", "2sech(x + 6)^2 + 0.5sech((x + 1)/2)^2" }, { "edge_xmin", "periodic" }, { "edge_xmax", "periodic" }, { "xmin", "-12" }, { "xmax", "12" }, { "tmax", "5" }, { "at", "2.5" }, { "points", "321" }, { "steps", "1000" }, { "method", "Crank\xE2\x80\x93Nicolson" } }, { { "surface", true }, { "profile", true }, { "height_color", true } });
            pde_view(g, 0.0, 2.5, 12.5, 2.7, 1.0, 1.6);
            g.view3.pitch = 0.8;
            g.view3.yaw = -0.35;
        } },
        { { "fisher", "pde", "A spreading population", "Fisher's equation u_t = u_xx + u(1 - u): the settled region invades the empty one as a wave of steady speed 2." }, "pde", [](Graph& g) {
            g.items.clear();
            put(g, "pde", "u_t = u_xx + u(1 - u)", { { "init", "1/(1 + e^(4(x + 15)))" }, { "edge_xmin", "u = 1" }, { "edge_xmax", "u = 0" }, { "xmin", "-20" }, { "xmax", "20" }, { "tmax", "14" }, { "at", "7" }, { "points", "241" } }, { { "surface", true }, { "profile", true }, { "height_color", true } });
            pde_view(g, 0.0, 7.0, 21.0, 7.4, 0.5, 0.8);
        } },
        { { "beam", "pde", "Vibrating beam", "u_tt = -0.1u_xxxx with hinged ends (u = 0 and u_xx = 0): each mode rings at a frequency that grows like the square of its number." }, "pde", [](Graph& g) {
            g.items.clear();
            put(g, "pde", "u_tt = -0.1u_xxxx", { { "init", "sin(x) + 0.3sin(3x), 0" }, { "edge_xmin", "u = 0, u_xx = 0" }, { "edge_xmax", "u = 0, u_xx = 0" }, { "xmin", "0" }, { "xmax", "pi" }, { "tmax", "10" }, { "at", "1" } }, { { "surface", true }, { "profile", true }, { "height_color", true } });
            pde_view(g, 1.6, 5.0, 1.8, 5.2);
        } },
        { { "bbm", "pde", "BBM water wave", "u_t + u_x + uu_x - u_xxt = 0: the mixed derivative u_xxt means every step solves the whole grid at once." }, "pde", [](Graph& g) {
            g.items.clear();
            put(g, "pde", "u_t + u_x + uu_x - u_xxt = 0", { { "init", "3sech(x/2)^2" }, { "edge_xmin", "periodic" }, { "edge_xmax", "periodic" }, { "xmin", "-20" }, { "xmax", "20" }, { "tmax", "12" }, { "at", "6" }, { "points", "241" }, { "method", "Crank\xE2\x80\x93Nicolson" } }, { { "surface", true }, { "profile", true }, { "height_color", true } });
            pde_view(g, 0.0, 6.0, 21.0, 6.4, 1.5, 2.0);
            g.view3.pitch = 0.8;
            g.view3.yaw = -0.35;
        } },
        { { "schrodinger", "pde", "Quantum wave packet", "Schr\xC3\xB6" "dinger's equation for \xCF\x88 = u + iv as a pair, u_t = -\xC2\xBD" "v_xx and v_t = \xC2\xBD" "u_xx: the packet moves and spreads." }, "pde", [](Graph& g) {
            g.items.clear();
            put(g, "pde", "u_t = -0.5v_xx", { { "functions", "u, v" }, { "eq_v", "v_t = 0.5u_xx" }, { "init", "exp(-(x + 4)^2)cos(3x)" }, { "init_v", "exp(-(x + 4)^2)sin(3x)" }, { "edge_xmin", "periodic" }, { "edge_xmax", "periodic" }, { "xmin", "-10" }, { "xmax", "10" }, { "tmax", "2.5" }, { "at", "1" }, { "points", "321" }, { "method", "Crank\xE2\x80\x93Nicolson" } }, { { "surface", true }, { "profile", true }, { "height_color", true }, { "show_u", true }, { "show_v", false } });
            pde_view(g, 0.0, 1.25, 10.5, 1.4, 0.0, 1.2);
        } },
        { { "drum", "pde", "A struck drum", "The wave equation u_tt = u_xx + u_yy on a square drum with its edge held fixed; move t\xE2\x82\x80 to watch the ripples." }, "pde", [](Graph& g) {
            g.items.clear();
            put(g, "pde", "u_tt = u_xx + u_yy", { { "space", "x, y" }, { "init", "exp(-30(x^2 + y^2)), 0" }, { "xmin", "-1" }, { "xmax", "1" }, { "ymin", "-1" }, { "ymax", "1" }, { "tmax", "4" }, { "at", "0.5" }, { "points", "61" } }, { { "height_color", true } });
            pde_view(g, 0.0, 0.0, 1.2, 0.0);
        } },
        { { "allen-cahn", "pde", "Phase separation", "The Allen\xE2\x80\x93" "Cahn equation u_t = 0.002\xE2\x88\x87\xC2\xB2u + u - u\xC2\xB3: a mixture separates into regions of 1 and -1 whose borders straighten and shrink." }, "pde", [](Graph& g) {
            g.items.clear();
            put(g, "pde", "u_t = 0.002(u_xx + u_yy) + u - u^3", { { "space", "x, y" }, { "init", "0.3sin(5x)sin(4y) + 0.2cos(7x + 3y) + 0.1sin(11y - 2x)" }, { "edge_xmin", "periodic" }, { "edge_xmax", "periodic" }, { "edge_ymin", "periodic" }, { "edge_ymax", "periodic" }, { "xmin", "-pi/2" }, { "xmax", "pi/2" }, { "ymin", "-pi/2" }, { "ymax", "pi/2" }, { "tmax", "20" }, { "at", "8" } }, { { "height_color", true } });
            pde_view(g, 0.0, 0.0, 1.8, 0.0);
        } },
        { { "gray-scott", "pde", "Gray\xE2\x80\x93Scott maze", "Two chemicals u and v that feed and eat each other (u + 2v \xE2\x86\x92 3v), started from one spot: v grows into rings and a maze." }, "pde", [](Graph& g) {
            g.items.clear();
            put(g, "pde", "u_t = 0.0001(u_xx + u_yy) - uv^2 + 0.04(1 - u)", { { "space", "x, y" }, { "functions", "u, v" }, { "eq_v", "v_t = 0.00005(v_xx + v_yy) + uv^2 - 0.1v" }, { "init", "1 - 0.5exp(-40(x^2 + y^2))" }, { "init_v", "0.25exp(-40(x^2 + y^2))" }, { "edge_xmin", "periodic" }, { "edge_xmax", "periodic" }, { "edge_ymin", "periodic" }, { "edge_ymax", "periodic" }, { "xmin", "-1" }, { "xmax", "1" }, { "ymin", "-1" }, { "ymax", "1" }, { "tmax", "3000" }, { "at", "3000" }, { "steps", "500" }, { "points", "65" } }, { { "height_color", true }, { "show_u", false }, { "show_v", true } });
            pde_view(g, 0.0, 0.0, 1.2, 0.0);
        } },
        { { "pulses", "pde", "Splitting pulses", "Gray\xE2\x80\x93Scott in one dimension: a single pulse of v splits again and again until it fills the loop." }, "pde", [](Graph& g) {
            g.items.clear();
            put(g, "pde", "u_t = 0.0001u_xx - uv^2 + 0.04(1 - u)", { { "functions", "u, v" }, { "eq_v", "v_t = 0.00005v_xx + uv^2 - 0.1v" }, { "init", "1 - 0.5exp(-200x^2)" }, { "init_v", "0.25exp(-200x^2)" }, { "edge_xmin", "periodic" }, { "edge_xmax", "periodic" }, { "xmin", "-1" }, { "xmax", "1" }, { "tmax", "2000" }, { "at", "2000" }, { "steps", "1000" } }, { { "surface", true }, { "profile", true }, { "height_color", true }, { "show_u", false }, { "show_v", true } });
            pde_view(g, 0.0, 1000.0, 1.1, 1050.0);
        } },
        { { "heat-ball", "pde", "Heat in a box", "u_t = \xE2\x88\x87\xC2\xB2u in three dimensions from a hot spot, with insulated walls: a level surface and a colored slice." }, "pde", [](Graph& g) {
            g.items.clear();
            put(g, "pde", "u_t = u_xx + u_yy + u_zz", { { "space", "x, y, z" }, { "init", "exp(-2((x - 1)^2 + y^2 + z^2)) + exp(-2((x + 1)^2 + (y - 1)^2 + z^2))" }, { "edge_xmin", "insulated" }, { "edge_xmax", "insulated" }, { "edge_ymin", "insulated" }, { "edge_ymax", "insulated" }, { "edge_zmin", "insulated" }, { "edge_zmax", "insulated" }, { "tmax", "1" }, { "at", "0.05" }, { "level", "0.15, 0.4" }, { "slices", "z = -1.5" } }, { { "iso", true }, { "slice", true }, { "height_color", true } });
            pde_view(g, 0.0, 0.0, 3.4, 0.0, 0.0, 3.4);
        } },
        { { "laplace", "pde", "Laplace on a square", "u_xx + u_yy = 0 with the top edge held at sin \xCF\x80x and the others at 0: the steady temperature of a plate." }, "pde_steady", [](Graph& g) {
            g.items.clear();
            put(g, "pde_steady", "u_xx + u_yy = 0", {}, { { "height_color", true } });
            pde_view(g, 0.5, 0.5, 0.65, 0.0, 0.5, 0.65);
        } },
        { { "membrane", "pde", "Soap film over a wire", "The minimal surface equation (1 + u_y\xC2\xB2)u_xx - 2u_xu_yu_xy + (1 + u_x\xC2\xB2)u_yy = 0, solved by Newton's method, with a wavy wire frame." }, "pde_steady", [](Graph& g) {
            g.items.clear();
            put(g, "pde_steady", "(1 + u_y^2)u_xx - 2u_x u_y u_xy + (1 + u_x^2)u_yy = 0", { { "edge_xmin", "u = 0.5sin(2pi y)" }, { "edge_xmax", "u = -0.5sin(2pi y)" }, { "edge_ymin", "u = 0.5cos(pi x)" }, { "edge_ymax", "u = 0.5cos(pi x)" } }, { { "height_color", true }, { "mesh", true } });
            pde_view(g, 0.5, 0.5, 0.65, 0.0);
        } },
        { { "sideways-wave", "pde", "Marching a wave in x", "u_xx = u_tt solved along x from u and u_x given at x = 0: a pulse in t splits as x grows." }, "pde_march", [](Graph& g) {
            g.items.clear();
            put(g, "pde_march", "u_xx = u_tt", { { "at", "1.5" } }, { { "surface", true }, { "profile", true }, { "height_color", true } });
            pde_view(g, 2.0, 0.0, 2.2, 3.4);
        } },
        { { "sin-cos-region", "regions", "Between sin and cos", "The region between sin x and cos x from one crossing to the next, \xCF\x80/4 to 5\xCF\x80/4, with area 2\xE2\x88\x9A" "2 and its centroid." }, "between2d", [](Graph& g) {
            g.items.clear();
            put(g, "between2d", "cos(x)", { { "upper", "sin(x)" }, { "min", "pi/4" }, { "max", "5pi/4" }, { "var", "x" } }, { { "shade", true }, { "boundary", true }, { "slices", true }, { "centroid", true } });
            g.view2.cx = 2.4;
            g.view2.scale = 70.0;
        } },
        { { "cardioid-area", "regions", "Inside a cardioid", "r \xE2\x89\xA4 1 + cos \xCE\xB8 all the way around: area 3\xCF\x80/2." }, "polar_region", [](Graph& g) {
            g.items.clear();
            put(g, "polar_region", "0", { { "upper", "1 + cos(theta)" }, { "min", "0" }, { "max", "2pi" } }, { { "shade", true }, { "boundary", true } });
            g.view2.scale = 120.0;
        } },
        { { "ice-cream", "regions", "Ice cream cone", "\xCF\x81 < 2 and \xCF\x86 < \xCF\x80/4: the solid inside a sphere and a cone, with its volume." }, "region3d", [](Graph& g) {
            g.items.clear();
            put(g, "region3d", "rho < 2, phi < pi/4", { { "resolution", "70" } }, { { "shade", true }, { "boundary", true } });
            g.view3.range = 3.0;
        } },
        { { "turned-parabola", "2d", "A rotated parabola", "y = x\xC2\xB2 turned 45\xC2\xB0 about (1, 0), with its parametrization and new equation." }, "rotation", [](Graph& g) {
            g.items.clear();
            put(g, "rotation", "y = x^2", { { "angle", "pi/4" }, { "cx", "1" }, { "cy", "0" } }, { { "original", true } });
            g.view2.scale = 50.0;
        } },
        { { "saddle", "3d", "Saddle with traces", "z = x\xC2\xB2 - y\xC2\xB2 with traces, contours on the floor and a tangent plane." }, "surface", [](Graph& g) {
            g.items.clear();
            put(g, "surface", "z = (x^2 - y^2)/4", { { "x0", "1" }, { "y0", "0.5" }, { "cx", "1" }, { "cy", "-1" } }, { { "traces", true }, { "floor", true }, { "plane", true } });
            g.view3.range = 3.0;
        } },
        { { "helix", "3d", "Helix and its Frenet frame", "Curvature, torsion and the T, N, B frame of a conical helix." }, "curve3d", [](Graph& g) {
            g.items.clear();
            put(g, "curve3d", "<(1 + t/8)cos(t), (1 + t/8)sin(t), t/4>", { { "max", "6pi" }, { "at", "5" } }, { { "frame", true }, { "circle", true }, { "torsion_color", true }, { "shadow", true } });
            g.view3.range = 4.0;
            g.view3.auto_z = false;
            g.view3.target = Vec3{ 0.0, 0.0, 2.4 };
            g.view3.zrange = 2.6;
        } },
        { { "torus", "3d", "Torus curvature", "A torus colored by Gaussian curvature: positive outside, negative inside." }, "param_surface", [](Graph& g) {
            g.items.clear();
            put(g, "param_surface", "<(2 + cos(v))cos(u), (2 + cos(v))sin(u), sin(v)>", {}, { { "gauss", true } });
            g.view3.range = 3.2;
            g.view3.auto_z = false;
            g.view3.zrange = 3.2;
        } },
        { { "mobius", "3d", "M\xC3\xB6" "bius strip", "A one-sided manifold from a parametrization." }, "param_surface", [](Graph& g) {
            g.items.clear();
            put(g, "param_surface", "<(2 + v cos(u/2))cos(u), (2 + v cos(u/2))sin(u), v sin(u/2)>", { { "vmin", "-0.6" }, { "vmax", "0.6" } }, { { "height_color", true }, { "normals", true } });
            g.view3.range = 3.0;
            g.view3.auto_z = false;
            g.view3.zrange = 3.0;
        } },
        { { "helix-temperature", "3d", "Temperature along a helix", "f(x, y, z) = xy + z read along a helix, with the chain rule \xE2\x88\x87" "f \xC2\xB7 r\xE2\x80\xB2 at t\xE2\x82\x80." }, "curve_value3d", [](Graph& g) {
            g.items.clear();
            put(g, "curve_value3d", "x*y + z", { { "path", "<2cos(t), 2sin(t), t/3>" }, { "max", "4pi" }, { "at", "2" } });
            g.view3.range = 2.6;
            g.view3.auto_z = false;
            g.view3.target = Vec3{ 0.0, 0.0, 2.1 };
            g.view3.zrange = 2.3;
        } },
        { { "vase", "3d", "Cylindrical vase", "r = f(\xCE\xB8, z): a fluted surface of revolution." }, "cylindrical", [](Graph& g) {
            g.items.clear();
            put(g, "cylindrical", "r = 1.2 + 0.4sin(1.5z) + 0.08cos(8theta)", { { "zmin", "-2.5" }, { "zmax", "2.5" } });
            g.view3.range = 2.6;
            g.view3.auto_z = false;
            g.view3.zrange = 2.6;
        } },
        { { "hyperboloid", "3d", "Hyperboloid", "The implicit surface x\xC2\xB2 + y\xC2\xB2 - z\xC2\xB2 = 1." }, "implicit3d", [](Graph& g) {
            g.items.clear();
            put(g, "implicit3d", "x^2 + y^2 - z^2 = 1", { { "resolution", "44" } });
            g.view3.range = 2.5;
            g.view3.auto_z = false;
            g.view3.zrange = 2.5;
        } },
        { { "horn", "3d", "Solid of revolution", "y = \xE2\x88\x9Ax from 0 to 4 turned about the x-axis: volume 8\xCF\x80, with the curve and the axis." }, "revolution", [](Graph& g) {
            g.items.clear();
            put(g, "revolution", "y = sqrt(x)", { { "axis", "x-axis" }, { "min", "0" }, { "max", "4" }, { "angle", "2pi" } }, { { "curve", true }, { "axis_line", true }, { "mesh", true } });
        } },
        { { "poles", "complex", "Zeros and poles", "Domain coloring of (z\xC2\xB2 - 1)/(z\xC2\xB2 + 1): zeros at \xC2\xB1" "1, poles at \xC2\xB1i." }, "domain", [](Graph& g) {
            g.items.clear();
            put(g, "domain", "(z^2 - 1)/(z^2 + 1)", {}, { { "modulus", true }, { "phase", true } });
            g.view2.scale = 110.0;
        } },
        { { "exp-map", "complex", "The exponential map", "Where w = e^z sends a grid of lines." }, "conformal", [](Graph& g) {
            g.items.clear();
            put(g, "conformal", "exp(z)", { { "remin", "-1" }, { "remax", "1" }, { "immin", "-pi" }, { "immax", "pi" }, { "lines", "20" } });
            g.view2.scale = 70.0;
        } },
        { { "sqrt-sheet", "complex", "Riemann surface of \xE2\x88\x9Az", "z = w\xC2\xB2 drawn over the w-disk: two sheets glued along a cut." }, "riemann", [](Graph& g) {
            g.items.clear();
            put(g, "riemann", "w^2", { { "rmax", "1.4" } });
        } },
        { { "gamma", "complex", "Modulus of \xCE\x93(z)", "|\xCE\x93(z)| over the plane, with poles at 0, -1, -2, \xE2\x80\xA6" }, "complex_surface", [](Graph& g) {
            g.items.clear();
            put(g, "complex_surface", "gamma(z)", { { "height", "modulus" } });
            g.view3.range = 3.0;
            g.view3.target = Vec3{ -0.5, 0.0, 0.0 };
            g.view3.auto_z = false;
            g.view3.target.z = 2.5;
            g.view3.zrange = 2.5;
        } },
        { { "spiral", "complex", "Complex spiral", "f(t) = e^{it}(1 + t/6): one real input, a complex output." }, "complex_curve", [](Graph& g) {
            g.items.clear();
            put(g, "complex_curve", "exp(i t)(1 + t/6)", { { "max", "4pi" } }, { { "frame", true } });
        } },
        { { "spiral-up", "complex", "Complex spiral, t upward", "The same e^{it}(1 + t/6) with t running up and the output plane across it." }, "complex_curve", [](Graph& g) {
            g.items.clear();
            put(g, "complex_curve", "exp(i t)(1 + t/6)", { { "max", "4pi" }, { "axis", "up" }, { "at", "5" } }, { { "shadows", false } });
            g.view3.range = 3.2;
            g.view3.target = Vec3{ 0.0, 0.0, 2.0 * 3.14159265358979 };
            g.view3.auto_z = false;
            g.view3.zrange = 2.0 * 3.14159265358979;
        } },
        { { "polar-jacobian", "transforms", "Polar coordinates", "(u, v) \xE2\x86\xA6 (u cos v, u sin v), shaded by det J = u." }, "transform2d", [](Graph& g) {
            g.items.clear();
            put(g, "transform2d", "<u*cos(v), u*sin(v)>");
        } },
        { { "shear", "transforms", "A nonlinear map", "(u, v) \xE2\x86\xA6 (u + sin v / 2, v + u\xC2\xB2/4) with its Jacobian cell." }, "transform2d", [](Graph& g) {
            g.items.clear();
            put(g, "transform2d", "<u + sin(v)/2, v + u^2/4>", { { "umin", "-2" }, { "umax", "2" }, { "vmin", "-2" }, { "vmax", "2" }, { "u0", "0.8" }, { "v0", "0.5" } }, { { "source", true } });
        } },
        { { "spherical-grid", "transforms", "Spherical coordinates", "u, v and w grid lines carried into space, colored by |J| = u\xC2\xB2 sin v." }, "transform3d", [](Graph& g) {
            g.items.clear();
            put(g, "transform3d", "<u*sin(v)*cos(w), u*sin(v)*sin(w), u*cos(v)>");
            g.view3.range = 2.2;
            g.view3.auto_z = false;
            g.view3.zrange = 2.2;
        } },
    };
    return all;
}

} // namespace

const std::vector<Example>& examples() {
    static const std::vector<Example> all = [] {
        std::vector<Example> out;
        for (const Recipe& r : recipes()) out.push_back(r.info);
        return out;
    }();
    return all;
}

bool make_example(const std::string& id, Graph& out) {
    for (const Recipe& r : recipes()) {
        if (r.info.id != id) continue;
        out = new_graph(r.first_kind);
        out.title = r.info.title;
        out.next_item = 1;
        r.fill(out);
        return true;
    }
    return false;
}

} // namespace arbor
