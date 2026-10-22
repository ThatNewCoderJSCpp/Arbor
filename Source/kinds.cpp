#include "arbor/kinds.hpp"

namespace arbor {

const char* space_id(Space s) {
    return s == Space::Solid ? "3d" : "2d";
}

Space space_from_id(const std::string& id) {
    return id == "3d" ? Space::Solid : Space::Plane;
}

const std::vector<SectionInfo>& sections() {
    static const std::vector<SectionInfo> all = {
        { "2d", "2D", "Functions, curves and fields in the plane" },
        { "3d", "3D", "Surfaces, space curves, solids and fields in space" },
        { "complex", "Complex", "Color maps, conformal maps, modulus surfaces and Riemann surfaces" },
        { "transforms", "Transforms", "Changes of variables, mapped grids and Jacobians" },
        { "regions", "Regions", "Areas and solids from any functions: conditions, regions between curves and surfaces, polar regions, with their areas and volumes" },
        { "pde", "PDEs", "Partial differential equations: heat, waves, transport, reaction\xE2\x80\x93" "diffusion and steady states in one, two or three space dimensions, with up to three unknown functions" },
        { "misc", "Misc", "Patterns and rules: piecewise functions, sequences like 3k + 1, escape-time sets, Langton's ant and the brachistochrone" }
    };
    return all;
}

namespace {

FieldSpec eq(const std::string& value, const std::string& hint) {
    return FieldSpec{ "eq", "", value, hint, true };
}

FieldSpec field(const std::string& id, const std::string& label, const std::string& value, const std::string& hint = std::string()) {
    return FieldSpec{ id, label, value, hint, false };
}

FieldSpec wide(const std::string& id, const std::string& label, const std::string& value, const std::string& hint = std::string()) {
    return FieldSpec{ id, label, value, hint, true };
}

OptionSpec option(const std::string& id, const std::string& label, bool on = false, const std::string& hint = std::string()) {
    return OptionSpec{ id, label, on, hint };
}

FieldSpec choice(const std::string& id, const std::string& label, const std::vector<std::string>& choices, const std::string& value, const std::string& hint = std::string()) {
    FieldSpec f{ id, label, value, hint, false };
    f.choices = choices;
    return f;
}

FieldSpec when(FieldSpec f, const std::string& condition) {
    f.show_if = condition;
    return f;
}

OptionSpec when(OptionSpec o, const std::string& condition) {
    o.show_if = condition;
    return o;
}

std::vector<KindInfo> build() {
    std::vector<KindInfo> k;
    k.push_back({ "function", "2d", "Function", "y = f(x)", "The graph of a function of x, with its derivative, tangent and normal lines and osculating circle.", Space::Plane,
        { eq("y = sin(x)", "y = f(x), or x = g(y)"), field("at", "x\xE2\x82\x80", "1", "point for tangent, normal and curvature") },
        { option("derivative", "f\xE2\x80\xB2(x)"), option("second", "f\xE2\x80\xB3(x)"), option("tangent", "Tangent"), option("normal", "Normal"), option("circle", "Osculating circle"), option("area", "Shade area under") } });
    k.push_back({ "implicit", "2d", "Implicit curve", "f(x, y) = g(x, y)", "Curves like x\xC2\xB2 + y\xC2\xB2 = 4 or f(x, y) = n, and regions from inequalities such as x\xC2\xB2 + y\xC2\xB2 < 4.", Space::Plane,
        { eq("x^2 + y^2 = 4", "f(x, y) = g(x, y), = n, or < > \xE2\x89\xA4 \xE2\x89\xA5") },
        { option("gradient", "Gradient arrows"), option("fill", "Shade where f < g", false) } });
    k.push_back({ "polar", "2d", "Polar", "r = f(\xCE\xB8), f(r, \xCE\xB8) = g(r, \xCE\xB8)", "A polar curve traced as \xCE\xB8 runs over its range, or any polar equation or inequality such as r\xC2\xB2 = 4cos(2\xCE\xB8) or r < 1 + cos 3\xCE\xB8.", Space::Plane,
        { eq("r = 1 + cos(\xCE\xB8)", "r = f(\xCE\xB8), f(r, \xCE\xB8) = g(r, \xCE\xB8), or < > \xE2\x89\xA4 \xE2\x89\xA5"), field("min", "\xCE\xB8 from", "0"), field("max", "to", "2pi"), field("at", "\xCE\xB8\xE2\x82\x80", "1") },
        { option("tangent", "Tangent"), option("normal", "Normal"), option("circle", "Osculating circle"), option("rays", "Show \xCE\xB8 ray") } });
    k.push_back({ "parametric", "2d", "Parametric curve", "\xE2\x9F\xA8x(t), y(t)\xE2\x9F\xA9", "A curve traced by (x(t), y(t)), with velocity, acceleration, tangent, normal and curvature at t\xE2\x82\x80.", Space::Plane,
        { eq("<cos(3t), sin(2t)>", "\xE2\x9F\xA8x(t), y(t)\xE2\x9F\xA9"), field("min", "t from", "0"), field("max", "to", "2pi"), field("at", "t\xE2\x82\x80", "1") },
        { option("tangent", "Tangent"), option("normal", "Normal"), option("velocity", "Velocity r\xE2\x80\xB2(t)"), option("acceleration", "Acceleration r\xE2\x80\xB3(t)"), option("circle", "Osculating circle"), option("curvature_color", "Color by curvature"), option("arrows", "Direction arrows") } });
    k.push_back({ "curve_value", "2d", "Function along a curve", "f(x(t), y(t))", "A function f(x, y) read along a curve (x(t), y(t)): the curve is colored by f, with f(t) and the chain-rule derivative df/dt.", Space::Plane,
        { eq("x*y", "f(x, y)"), field("path", "path", "<cos(t), sin(t)>", "\xE2\x9F\xA8x(t), y(t)\xE2\x9F\xA9"), field("min", "t from", "0"), field("max", "to", "2pi"), field("at", "t\xE2\x82\x80", "1") },
        { option("inset", "Graph f(t)", true), option("contours", "Contours of f"), option("gradient", "\xE2\x88\x87" "f at t\xE2\x82\x80") } });
    k.push_back({ "vector_field", "2d", "Vector field", "\xE2\x9F\xA8P(x, y), Q(x, y)\xE2\x9F\xA9", "Arrows of a 2D vector field, colored by size, with divergence and curl and flow lines.", Space::Plane,
        { eq("<-y, x>", "\xE2\x9F\xA8P(x, y), Q(x, y)\xE2\x9F\xA9"), field("density", "arrows across", "21") },
        { option("normalize", "Same length", true), option("streamlines", "Flow lines"), option("divergence", "Shade divergence"), option("curl", "Shade curl") } });
    k.push_back({ "contour", "2d", "Contour map", "f(x, y)", "Level curves of f(x, y) over a color map, with gradient arrows.", Space::Plane,
        { eq("sin(x)*cos(y)", "f(x, y)"), field("levels", "levels", "12") },
        { option("shade", "Color map", true), option("gradient", "Gradient arrows"), option("labels", "Label levels") } });
    k.push_back({ "slope_field", "2d", "Slope field", "dy/dx = f(x, y)", "The direction field of a first-order ODE, with the solution through (x\xE2\x82\x80, y\xE2\x82\x80).", Space::Plane,
        { eq("x - y", "f(x, y) in dy/dx = f(x, y)"), field("x0", "x\xE2\x82\x80", "0"), field("y0", "y\xE2\x82\x80", "1"), field("density", "marks across", "25") },
        { option("solution", "Solution curve", true) } });
    k.push_back({ "ode", "2d", "Differential equation", "F(x, y, y\xE2\x80\xB2, \xE2\x80\xA6) = 0", "An ODE of any order, such as y\xE2\x80\xB3 = -y or y\xE2\x80\xB4 + y\xE2\x80\xB2 = sin(x), solved from x\xE2\x82\x80 in both directions. Write y\xE2\x80\xB2 as y', y\xE2\x80\xB3 as y'', or y^(n).", Space::Plane,
        { eq("y'' = -y", "y'' = f(x, y, y'), or any F(x, y, y', ..., y^(n)) = 0"), field("x0", "x\xE2\x82\x80", "0"), wide("init", "y(x\xE2\x82\x80), y\xE2\x80\xB2(x\xE2\x82\x80), ...", "1, 0", "starting values, comma separated"), field("min", "x from", ""), field("max", "to", ""), field("at", "read at x", "1") },
        { option("derivatives", "Graph y\xE2\x80\xB2, y\xE2\x80\xB3, y\xE2\x80\xB4"), option("phase", "Phase plane (y, y\xE2\x80\xB2)") } });
    k.push_back({ "rotation", "2d", "Rotated curve", "y = f(x) turned by \xCE\xB1", "A curve turned about a point by an angle, drawn with the original, with the rotated parametrization and the rotated curve's equation.", Space::Plane,
        { eq("y = x^2", "y = f(x), x = g(y) or \xE2\x9F\xA8x(t), y(t)\xE2\x9F\xA9"), field("angle", "angle \xCE\xB1", "pi/6", "radians, or degrees with Degrees on"), field("cx", "about x", "0"), field("cy", "y", "0"), field("min", "t from", ""), field("max", "to", "") },
        { option("original", "Show the original", true) } });
    k.push_back({ "points", "2d", "Points", "(a, b)", "Points, joined in order if you like.", Space::Plane,
        { eq("(1, 2), (3, 1), (4, 3)", "(x, y), (x, y), ...") },
        { option("join", "Join with lines"), option("labels", "Coordinates") } });

    k.push_back({ "surface", "3d", "Surface", "z = f(x, y)", "The graph of f(x, y), with traces, contours, normals, gradient and a tangent plane.", Space::Solid,
        { eq("z = sin(x)*cos(y)", "z = f(x, y)"), field("x0", "x\xE2\x82\x80", "1"), field("y0", "y\xE2\x82\x80", "0.5"), field("cx", "trace x =", "0"), field("cy", "trace y =", "0") },
        { option("mesh", "Mesh lines", true), option("contours", "Contours"), option("floor", "Contour map on floor"), option("traces", "Traces at x = c, y = c"), option("normals", "Normal vectors"), option("gradient", "Gradient on floor"), option("plane", "Tangent plane at (x\xE2\x82\x80, y\xE2\x82\x80)"), option("height_color", "Color by height", true) } });
    k.push_back({ "revolution", "3d", "Surface of revolution", "y = f(x) turned about an axis", "A 2D curve turned about the x-axis, the y-axis or any line in its plane, with its parametrization, volume and surface area.", Space::Solid,
        { eq("y = sqrt(x)", "y = f(x), x = g(y) or \xE2\x9F\xA8x(t), y(t)\xE2\x9F\xA9"), wide("axis", "about", "x-axis", "x-axis, y-axis, y = c, x = c or y = m x + b"), field("min", "from", "0"), field("max", "to", "4"), field("angle", "turned by", "2pi") },
        { option("curve", "The curve", true), option("axis_line", "The axis", true), option("arrows", "Direction arrows on the curve"), option("mesh", "Mesh lines", true), option("height_color", "Color by height", false) } });
    k.push_back({ "implicit3d", "3d", "Implicit surface", "f(x, y, z) = g(x, y, z)", "Level surfaces such as x\xC2\xB2 + y\xC2\xB2 + z\xC2\xB2 = 9 or f(x, y, z) = n.", Space::Solid,
        { eq("x^2 + y^2 - z^2 = 1", "f(x, y, z) = g(x, y, z) or = n"), field("resolution", "detail", "40") },
        { option("normals", "Normal vectors"), option("height_color", "Color by height", true) } });
    k.push_back({ "curve3d", "3d", "Space curve", "\xE2\x9F\xA8x(t), y(t), z(t)\xE2\x9F\xA9", "A curve in space with its Frenet frame (T, N, B), curvature, torsion (twist) and osculating circle.", Space::Solid,
        { eq("<cos(t), sin(t), t/4>", "\xE2\x9F\xA8x(t), y(t), z(t)\xE2\x9F\xA9"), field("min", "t from", "0"), field("max", "to", "4pi"), field("at", "t\xE2\x82\x80", "2") },
        { option("frame", "Frenet frame T, N, B", true), option("circle", "Osculating circle"), option("plane", "Osculating plane"), option("curvature_color", "Color by curvature"), option("torsion_color", "Color by torsion (twist)"), option("shadow", "Shadow on floor"), option("arrows", "Direction arrows") } });
    k.push_back({ "param_surface", "3d", "Parametric surface", "\xE2\x9F\xA8x(u, v), y(u, v), z(u, v)\xE2\x9F\xA9", "Manifolds given by a parametrization: tori, M\xC3\xB6" "bius strips, spheres, Klein bottles.", Space::Solid,
        { eq("<(2 + cos(v))*cos(u), (2 + cos(v))*sin(u), sin(v)>", "\xE2\x9F\xA8x(u, v), y(u, v), z(u, v)\xE2\x9F\xA9"), field("umin", "u from", "0"), field("umax", "to", "2pi"), field("vmin", "v from", "0"), field("vmax", "to", "2pi") },
        { option("mesh", "u and v lines", true), option("normals", "Normal vectors"), option("gauss", "Color by Gaussian curvature"), option("height_color", "Color by height") } });
    k.push_back({ "spherical", "3d", "Spherical", "\xCF\x81 = f(\xCE\xB8, \xCF\x86)", "Surfaces in spherical coordinates (\xCE\xB8 around the z axis, \xCF\x86 down from it): \xCF\x81 = f(\xCE\xB8, \xCF\x86), or any equation or inequality in \xCF\x81, \xCE\xB8 and \xCF\x86, like \xCF\x81 = 2 + cos(\xCF\x81 \xCF\x86) or \xCF\x81 < 1 + sin\xC2\xB2\xCF\x86.", Space::Solid,
        { eq("rho = 2 + 0.4*sin(4theta)*sin(phi)^2", "\xCF\x81 = f(\xCE\xB8, \xCF\x86), or f(\xCF\x81, \xCE\xB8, \xCF\x86) = g(\xCF\x81, \xCE\xB8, \xCF\x86)") },
        { option("mesh", "Mesh lines", true), option("height_color", "Color by height", true) } });
    k.push_back({ "cylindrical", "3d", "Cylindrical", "r = f(\xCE\xB8, z)", "Surfaces in cylindrical coordinates about the z, x or y axis: r = f(\xCE\xB8, z), or any equation or inequality in r, \xCE\xB8 and the axis variable (and \xCF\x81 and \xCF\x86 too), like r\xC2\xB2 + z\xC2\xB2 = 4 or r < 1 + z/4.", Space::Solid,
        { eq("r = 1 + 0.3*sin(3theta)*cos(2z)", "r = f(\xCE\xB8, z), or f(r, \xCE\xB8, z) = g(r, \xCE\xB8, z)"), field("axis", "about", "z", "z, x or y; the equation then uses that letter for the height"), field("zmin", "along from", "-2"), field("zmax", "to", "2") },
        { option("mesh", "Mesh lines", true), option("height_color", "Color by height", true) } });
    k.push_back({ "implicit_cylindrical", "3d", "Implicit cylindrical", "f(r, \xCE\xB8, z) = g(r, \xCE\xB8, z)", "Any equation or inequality in cylindrical coordinates about the z, x or y axis, like r\xC2\xB2 = 1 + z\xC2\xB2/2 + 0.2cos(4\xCE\xB8) or r < 2 - |z|. Pick the axis with \"about\"; the equation then uses x or y for the height instead of z.", Space::Solid,
        { eq("r^2 = 1 + z^2/2 + 0.2*cos(4theta)", "f(r, \xCE\xB8, z) = g(r, \xCE\xB8, z), or < > \xE2\x89\xA4 \xE2\x89\xA5"), field("axis", "about", "z", "z, x or y; the equation then uses that letter for the height"), field("resolution", "detail", "44") },
        { option("height_color", "Color by height", true) } });
    k.push_back({ "mixed3d", "3d", "\xCF\x81, r and \xCE\xB8", "f(\xCF\x81, r, \xCE\xB8) = g(\xCF\x81, r, \xCE\xB8)", "Surfaces and regions that mix the distance from the origin \xCF\x81, the distance from an axis r and the angle \xCE\xB8 around it, like \xCF\x81 r = 2 + 0.5cos(3\xCE\xB8) or \xCF\x81 < 1 + r/2. \xCF\x86 (the angle from the axis) and the height along the axis can be used too. The axis is z, x or y.", Space::Solid,
        { eq("rho*r = 2 + 0.5*cos(3theta)", "f(\xCF\x81, r, \xCE\xB8) = g(\xCF\x81, r, \xCE\xB8), or < > \xE2\x89\xA4 \xE2\x89\xA5"), field("axis", "about", "z", "z, x or y: r is the distance from it and \xCE\xB8 turns around it"), field("resolution", "detail", "44") },
        { option("height_color", "Color by height", true) } });
    k.push_back({ "curve_value3d", "3d", "Function along a space curve", "f(x(t), y(t), z(t))", "A function of x, y and z read along a curve in space: the curve is colored by f, with the chain rule \xE2\x88\x87" "f \xC2\xB7 r\xE2\x80\xB2.", Space::Solid,
        { eq("x*y + z", "f(x, y, z)"), field("path", "path", "<cos(t), sin(t), t/4>", "\xE2\x9F\xA8x(t), y(t), z(t)\xE2\x9F\xA9"), field("min", "t from", "0"), field("max", "to", "4pi"), field("at", "t\xE2\x82\x80", "2") },
        { option("inset", "Graph f(t)", true), option("gradient", "\xE2\x88\x87" "f at t\xE2\x82\x80", true) } });
    k.push_back({ "vector_field3d", "3d", "Vector field", "\xE2\x9F\xA8P, Q, R\xE2\x9F\xA9", "Arrows of a vector field in space, colored by size, with flow lines.", Space::Solid,
        { eq("<-y, x, 0.3*z>", "\xE2\x9F\xA8P(x, y, z), Q(x, y, z), R(x, y, z)\xE2\x9F\xA9"), field("density", "arrows across", "7") },
        { option("normalize", "Same length", true), option("streamlines", "Flow lines") } });
    k.push_back({ "points3d", "3d", "Points", "(a, b, c)", "Points in space, joined in order if you like.", Space::Solid,
        { eq("(1, 1, 1), (-1, 2, 0)", "(x, y, z), ...") },
        { option("join", "Join with lines") } });

    k.push_back({ "region2d", "regions", "Region from conditions", "conditions in x, y, r, \xCE\xB8", "Any region of the plane cut out by conditions on any functions, joined with and, or, not or commas, like y > x\xC2\xB2 - 2 and x\xC2\xB2 + y\xC2\xB2 < 9, or r < 2 + cos(3\xCE\xB8) and \xCE\xB8 < \xCF\x80. Shade it, outline it, and read its area.", Space::Plane,
        { eq("y > x^2 - 2 and x^2 + y^2 < 9", "conditions with < > \xE2\x89\xA4 \xE2\x89\xA5 joined by and, or, not or commas; x, y, r and \xCE\xB8") },
        { option("shade", "Shade it", true), option("boundary", "Its edge", true), option("each", "Each condition's curve") } });
    k.push_back({ "between2d", "regions", "Region between curves", "f(x) \xE2\x89\xA4 y \xE2\x89\xA4 g(x)", "The region between two curves, y from f(x) to g(x) (or x from f(y) to g(y)), from where they cross or between the ends you give, with its area integral and centroid.", Space::Plane,
        { eq("x^2 - 2", "the lower curve y = f(x) (or the left one x = f(y))"), field("upper", "up to", "4 - x^2", "the upper curve g(x) (or the right one g(y))"), field("min", "from", "", "where they cross"), field("max", "to", "", "where they cross"), field("var", "curves of", "x", "x: y runs between two functions of x; y: x runs between two functions of y") },
        { option("shade", "Shade it", true), option("boundary", "Its edge", true), option("slices", "Slices"), option("centroid", "Centroid", true) } });
    k.push_back({ "polar_region", "regions", "Polar region", "f(\xCE\xB8) \xE2\x89\xA4 r \xE2\x89\xA4 g(\xCE\xB8)", "The region swept between two polar curves as \xCE\xB8 turns, with its area \xC2\xBD\xE2\x88\xAB(g\xC2\xB2 - f\xC2\xB2) d\xCE\xB8.", Space::Plane,
        { eq("1", "the inner curve r = f(\xCE\xB8)"), field("upper", "out to", "2 + cos(3theta)", "the outer curve r = g(\xCE\xB8)"), field("min", "\xCE\xB8 from", "0"), field("max", "to", "2pi") },
        { option("shade", "Shade it", true), option("boundary", "Its edge", true) } });
    k.push_back({ "region3d", "regions", "Solid from conditions", "conditions in x, y, z, r, \xCF\x81, \xCE\xB8, \xCF\x86", "Any solid cut out by conditions on any functions, joined with and, or, not or commas, like x\xC2\xB2 + y\xC2\xB2 < 4 and 0 < z < 3 - x, or \xCF\x81 < 2 and \xCF\x86 < \xCF\x80/4 (an ice cream cone). Shade it on the walls of the box, show its edge, and read its volume.", Space::Solid,
        { eq("x^2 + y^2 < 4, 0 < z < 3 - x", "conditions with < > \xE2\x89\xA4 \xE2\x89\xA5 joined by and, or, not or commas; x, y, z, r, \xCF\x81, \xCE\xB8 and \xCF\x86"), field("resolution", "detail", "64") },
        { option("shade", "Shade it on the walls", true), option("boundary", "Its edge", true) } });
    k.push_back({ "between3d", "regions", "Solid between surfaces", "f(x, y) \xE2\x89\xA4 z \xE2\x89\xA4 g(x, y)", "The solid between two surfaces over a region of the xy-plane: x from a to b and y from h\xE2\x82\x81(x) to h\xE2\x82\x82(x), with its volume as an iterated integral.", Space::Solid,
        { eq("0", "the bottom z = f(x, y)"), field("upper", "up to", "4 - x^2 - y^2", "the top z = g(x, y)"), field("xmin", "x from", "-2"), field("xmax", "to", "2"), field("ymin", "y from", "-sqrt(4 - x^2)", "h\xE2\x82\x81(x)"), field("ymax", "to", "sqrt(4 - x^2)", "h\xE2\x82\x82(x)") },
        { option("top", "Top", true), option("bottom", "Bottom", true), option("walls", "Walls", true), option("base", "Base on the floor", true), option("mesh", "Mesh lines") } });
    k.push_back({ "domain", "complex", "Complex color map", "w = f(z)", "Domain coloring: hue is arg f(z), brightness rings show |f(z)|. Zeros and poles stand out.", Space::Plane,
        { eq("(z^2 - 1)/(z^2 + 1)", "f(z)") },
        { option("modulus", "|f| rings", true), option("phase", "Phase lines"), option("grid", "Re/Im grid"), option("residues", "Poles and residues", true) } });
    k.push_back({ "conformal", "complex", "Conformal map", "w = f(z)", "Where a square grid in the z-plane goes under w = f(z).", Space::Plane,
        { eq("z^2", "f(z)"), field("remin", "Re z from", "-1"), field("remax", "to", "1"), field("immin", "Im z from", "-1"), field("immax", "to", "1"), field("lines", "lines", "16") },
        { option("source", "Show the z grid too") } });
    k.push_back({ "complex_surface", "complex", "Modulus surface", "|f(z)|", "The height |f(z)| over the complex plane, colored by arg f(z). Poles rise as towers.", Space::Solid,
        { eq("1/(z^2 + 1)", "f(z)"), field("height", "height", "modulus", "modulus, real, imag or log") },
        { option("mesh", "Mesh lines", true), option("residues", "Poles and residues") } });
    k.push_back({ "complex_curve", "complex", "Complex-valued curve", "w = f(t)", "One real input t on one axis, the complex output f(t) in the plane across it: left-right gives (t, Re f, Im f), up-down gives (Re f, Im f, t).", Space::Solid,
        { eq("exp(i*t)*(1 + t/6)", "f(t), t real"), field("min", "t from", "0"), field("max", "to", "4pi"), field("axis", "t runs", "across", "across (left-right) or up (up-down)"), field("at", "t\xE2\x82\x80", "2") },
        { option("shadows", "Re and Im shadows", true), option("plane", "Output plane at t\xE2\x82\x80", true), option("frame", "Frenet frame") } });
    k.push_back({ "riemann", "complex", "Riemann surface", "z = g(w)", "The Riemann surface of the many-valued inverse of g: for z = w\xC2\xB2 it is the surface of \xE2\x88\x9Az.", Space::Solid,
        { eq("w^2", "g(w) in z = g(w)"), field("rmax", "|w| up to", "1.5"), field("height", "height", "real", "real or imag of w") },
        { option("mesh", "Mesh lines", true) } });

    k.push_back({ "transform2d", "transforms", "Change of variables", "(x, y) = T(u, v)", "How a map T bends the u-v grid, with its Jacobian matrix and determinant.", Space::Plane,
        { eq("<u*cos(v), u*sin(v)>", "\xE2\x9F\xA8x(u, v), y(u, v)\xE2\x9F\xA9"), field("umin", "u from", "0"), field("umax", "to", "2"), field("vmin", "v from", "0"), field("vmax", "to", "2pi"), field("lines", "lines", "16"), field("u0", "u\xE2\x82\x80", "1"), field("v0", "v\xE2\x82\x80", "0.8") },
        { option("jacobian", "Shade |J|", true), option("cell", "Jacobian at (u\xE2\x82\x80, v\xE2\x82\x80)", true), option("source", "Show the u-v grid") } });
    k.push_back({ "transform3d", "transforms", "Change of variables in space", "(x, y, z) = T(u, v, w)", "Grid lines of u, v and w carried into space by T, with the Jacobian determinant.", Space::Solid,
        { eq("<u*sin(v)*cos(w), u*sin(v)*sin(w), u*cos(v)>", "\xE2\x9F\xA8x, y, z\xE2\x9F\xA9 in terms of u, v, w"), field("umin", "u from", "0.5"), field("umax", "to", "2"), field("vmin", "v from", "0.3"), field("vmax", "to", "2.8"), field("wmin", "w from", "0"), field("wmax", "to", "4.7"), field("lines", "lines", "6") },
        { option("jacobian", "Color by |J|", true) } });

    k.push_back({ "define", "", "Definition", "f(x) = \xE2\x80\xA6 or a = \xE2\x80\xA6", "Define a function or a number to use in other items. Numbers become sliders.", Space::Plane,
        { eq("a = 1", "f(x) = ... or a = 2") }, {} });

    k.push_back({ "piecewise", "misc", "Piecewise function", "y = a if c, else b", "A function that follows one formula where the condition holds and another where it does not. Leave the false box empty to draw only where the condition holds.", Space::Plane,
        { eq("x < 0", "condition, e.g. x < 0, 0 \xE2\x89\xA4 x < 2, x mod 2 = 0 and x > 1"), wide("then", "if true", "x^2"), wide("else", "if false", "sqrt(x)") },
        { option("derivative", "f\xE2\x80\xB2(x)") } });
    k.push_back({ "sequence", "misc", "Conditional sequence", "a \xE2\x86\xA6 f(a) or g(a)", "Repeats a rule on a number a, choosing between two formulas each step, like 3k + 1 (Collatz): if a is even, halve it, otherwise take 3a + 1. k is the step number.", Space::Plane,
        { eq("a mod 2 = 0", "condition on a and k"), wide("then", "if true", "a/2"), wide("else", "if false", "3a + 1"), field("start", "start", "27"), field("steps", "steps", "300"), wide("stop", "stop when", "a = 1") },
        {} });
    k.push_back({ "escape", "misc", "Escape-time set", "z \xE2\x86\xA6 f(z, c)", "Repeats z \xE2\x86\xA6 f(z, c) for every point while the condition holds and colors each point by how long it lasted. z\xC2\xB2 + c gives the Mandelbrot set; turn on Julia for a fixed c.", Space::Plane,
        { eq("z^2 + c", "the step f(z, c)"), wide("while", "keep going while", "|z| <= 2"), field("z0", "z\xE2\x82\x80", "0"), field("max", "max steps", "120"), field("c", "Julia c", "-0.8 + 0.156i") },
        { option("julia", "Julia set (fixed c, each point is z\xE2\x82\x80)") } });
    k.push_back({ "brachistochrone", "misc", "Brachistochrone", "the fastest slide from A to B", "The curve a bead slides down fastest from A to B under gravity: an upside-down cycloid. Race it against the straight line and any curve y = f(x), and watch the circle that rolls to draw it.", Space::Plane,
        { eq("y = -2sin(x/2)", "another curve to race, y = f(x) from A to B (optional)"), field("x0", "A x", "0"), field("y0", "y", "0"), field("x1", "B x", "pi"), field("y1", "y", "-2"), field("g", "g", "9.81"), field("at", "t", "0.4") },
        { option("line", "Straight line", true), option("curve", "The other curve", true), option("beads", "Beads at time t", true), option("circle", "Rolling circle", true), option("whole", "The whole cycloid arch") } });
    k.push_back({ "pde", "pde", "Time evolution", "F(t, x, u, u_t, u_x, u_xt, \xE2\x80\xA6) = 0", "Any equation that moves forward in time: heat, waves, transport, Burgers, KdV, beams, Schr\xC3\xB6" "dinger as a pair, reaction\xE2\x80\x93" "diffusion systems like Gray\xE2\x80\x93Scott, and mixed derivatives like u_xxt. Each time step is implicit: Newton's method solves the whole grid at once with a sparse linear solver. In x alone it draws u(x, t) as a surface over x and time; with y it draws u(x, y) at t\xE2\x82\x80; with z it draws level surfaces and slices.", Space::Solid,
        { choice("space", "space", { "x", "x, y", "x, y, z" }, "x", "the space variables"), choice("functions", "functions", { "u", "u, v", "u, v, w" }, "u", "how many unknown functions"),
          eq("u_t = 0.5u_xx", "any F(t, x, u, u_t, u_tt, u_x, u_xt, \xE2\x80\xA6) = 0, with u, v, w and derivatives like u_xx or v_xt"),
          when(wide("eq_v", "v:", "v_t = 0.1v_xx + u - v", "the equation for v"), "functions=u, v|u, v, w"), when(wide("eq_w", "w:", "w_t = 0.1w_xx + v - w", "the equation for w"), "functions=u, v, w"),
          wide("init", "u, u_t, \xE2\x80\xA6 at the start", "cos(x/2) + 0.6sin(2x)", "u at the starting time, then u_t, u_tt, \xE2\x80\xA6 as the equation needs, comma separated"),
          when(wide("init_v", "v, v_t, \xE2\x80\xA6 at the start", "0", "v at the starting time, then v_t, \xE2\x80\xA6"), "functions=u, v|u, v, w"), when(wide("init_w", "w, w_t, \xE2\x80\xA6 at the start", "0", "w at the starting time, then w_t, \xE2\x80\xA6"), "functions=u, v, w"),
          wide("edge_xmin", "edge at x from", "u = 0", "conditions here, comma separated: u = 0, u_x = 1, u_x + u = sin(t), or insulated, periodic, free"), wide("edge_xmax", "edge at x to", "u = 0", "conditions here, comma separated: u = 0, u_x = 1, u_x + u = sin(t), or insulated, periodic, free"),
          when(wide("edge_ymin", "edge at y from", "u = 0", "conditions here, comma separated"), "space=x, y|x, y, z"), when(wide("edge_ymax", "edge at y to", "u = 0", "conditions here, comma separated"), "space=x, y|x, y, z"),
          when(wide("edge_zmin", "edge at z from", "u = 0", "conditions here, comma separated"), "space=x, y, z"), when(wide("edge_zmax", "edge at z to", "u = 0", "conditions here, comma separated"), "space=x, y, z"),
          field("xmin", "x from", "-pi"), field("xmax", "to", "pi"), when(field("ymin", "y from", "-pi"), "space=x, y|x, y, z"), when(field("ymax", "to", "pi"), "space=x, y|x, y, z"), when(field("zmin", "z from", "-pi"), "space=x, y, z"), when(field("zmax", "to", "pi"), "space=x, y, z"),
          field("start", "t from", "0"), field("tmax", "t up to", "4"), field("at", "t\xE2\x82\x80", "1"),
          choice("method", "time stepping", { "automatic", "Crank\xE2\x80\x93Nicolson", "BDF2", "backward Euler" }, "automatic", "automatic uses Crank\xE2\x80\x93Nicolson for waves and BDF2 otherwise"), field("steps", "time steps", "auto"), field("points", "grid points", "auto", "points along each space axis"),
          when(field("level", "level", "auto", "the value for the level surface, or several like 0.2, 0.5"), "space=x, y, z"), when(field("slices", "slices", "z = 0", "planes like z = 0, x = 1"), "space=x, y, z") },
        { when(option("surface", "u(x, t) surface", true), "space=x"), when(option("profile", "Line at t\xE2\x82\x80", true), "space=x"), option("height_color", "Color by value", true), when(option("mesh", "Mesh lines"), "space=x|x, y"),
          when(option("iso", "Level surface", true), "space=x, y, z"), when(option("slice", "Slices", true), "space=x, y, z"),
          when(option("show_u", "Show u", true), "functions=u, v|u, v, w"), when(option("show_v", "Show v", true), "functions=u, v|u, v, w"), when(option("show_w", "Show w", true), "functions=u, v, w") } });
    k.push_back({ "pde_steady", "pde", "Steady state", "F(x, y, u, u_x, u_yy, \xE2\x80\xA6) = 0", "Boundary value problems with no time: Laplace's equation u_xx + u_yy = 0, Poisson's equation, a beam's bending, nonlinear ones like u_xx + u_yy = e^u, and systems, with the values or slopes given on each edge. Everything is solved at once with Newton's method and a sparse linear solver.", Space::Solid,
        { choice("space", "space", { "x", "x, y", "x, y, z" }, "x, y", "the space variables"), choice("functions", "functions", { "u", "u, v", "u, v, w" }, "u", "how many unknown functions"),
          eq("u_xx + u_yy = 0", "any F(x, y, z, u, u_x, u_xx, \xE2\x80\xA6) = 0, with u, v, w and their derivatives"),
          when(wide("eq_v", "v:", "v_xx + v_yy = u", "the equation for v"), "functions=u, v|u, v, w"), when(wide("eq_w", "w:", "w_xx + w_yy = v", "the equation for w"), "functions=u, v, w"),
          wide("edge_xmin", "edge at x from", "u = 0", "conditions here, comma separated: u = 0, u_x = 1, u_x + u = 0, or insulated, periodic, free"), wide("edge_xmax", "edge at x to", "u = 0", "conditions here, comma separated: u = 0, u_x = 1, u_x + u = 0, or insulated, periodic, free"),
          when(wide("edge_ymin", "edge at y from", "u = 0", "conditions here, comma separated"), "space=x, y|x, y, z"), when(wide("edge_ymax", "edge at y to", "u = sin(pi x)", "conditions here, comma separated"), "space=x, y|x, y, z"),
          when(wide("edge_zmin", "edge at z from", "u = 0", "conditions here, comma separated"), "space=x, y, z"), when(wide("edge_zmax", "edge at z to", "u = 0", "conditions here, comma separated"), "space=x, y, z"),
          wide("guess", "starting guess for u", "0", "where Newton's method starts; only matters for nonlinear equations"), when(wide("guess_v", "starting guess for v", "0"), "functions=u, v|u, v, w"), when(wide("guess_w", "starting guess for w", "0"), "functions=u, v, w"),
          field("xmin", "x from", "0"), field("xmax", "to", "1"), when(field("ymin", "y from", "0"), "space=x, y|x, y, z"), when(field("ymax", "to", "1"), "space=x, y|x, y, z"), when(field("zmin", "z from", "0"), "space=x, y, z"), when(field("zmax", "to", "1"), "space=x, y, z"),
          field("points", "grid points", "auto", "points along each axis"),
          when(field("level", "level", "auto", "the value for the level surface, or several like 0.2, 0.5"), "space=x, y, z"), when(field("slices", "slices", "z = 0.5", "planes like z = 0, x = 1"), "space=x, y, z") },
        { option("height_color", "Color by value", true), when(option("mesh", "Mesh lines"), "space=x, y"), when(option("iso", "Level surface", true), "space=x, y, z"), when(option("slice", "Slices", true), "space=x, y, z"),
          when(option("show_u", "Show u", true), "functions=u, v|u, v, w"), when(option("show_v", "Show v", true), "functions=u, v|u, v, w"), when(option("show_w", "Show w", true), "functions=u, v, w") } });
    k.push_back({ "pde_march", "pde", "Marching in x", "F(x, t, u, u_x, u_t, \xE2\x80\xA6) = 0 from x = x\xE2\x82\x80", "Solves along x instead of forward in time: give u, u_x, \xE2\x80\xA6 along the line where x starts, as functions of t (and y), and conditions at the t edges. This works for wave-like equations. For heat-like ones it's the sideways heat problem, which is unstable: tiny errors grow without bound.", Space::Solid,
        { choice("space", "across", { "t", "t, y" }, "t", "the other variables"), choice("functions", "functions", { "u", "u, v", "u, v, w" }, "u", "how many unknown functions"),
          eq("u_xx = u_tt", "any F(x, t, u, u_x, u_xx, u_t, \xE2\x80\xA6) = 0"),
          when(wide("eq_v", "v:", "v_x = u_t", "the equation for v"), "functions=u, v|u, v, w"), when(wide("eq_w", "w:", "w_x = v_t", "the equation for w"), "functions=u, v, w"),
          wide("init", "u, u_x, \xE2\x80\xA6 where x starts", "exp(-4t^2), 0", "u along the starting line as a function of t, then u_x, \xE2\x80\xA6 as needed, comma separated"),
          when(wide("init_v", "v, v_x, \xE2\x80\xA6 where x starts", "0"), "functions=u, v|u, v, w"), when(wide("init_w", "w, w_x, \xE2\x80\xA6 where x starts", "0"), "functions=u, v, w"),
          wide("edge_tmin", "edge at t from", "u = 0", "conditions here, comma separated: u = 0, u_t = 0, or insulated, periodic, free"), wide("edge_tmax", "edge at t to", "u = 0", "conditions here, comma separated: u = 0, u_t = 0, or insulated, periodic, free"),
          when(wide("edge_ymin", "edge at y from", "u = 0", "conditions here, comma separated"), "space=t, y"), when(wide("edge_ymax", "edge at y to", "u = 0", "conditions here, comma separated"), "space=t, y"),
          field("tmin", "t from", "-pi"), field("tmax", "to", "pi"), when(field("ymin", "y from", "-pi"), "space=t, y"), when(field("ymax", "to", "pi"), "space=t, y"),
          field("start", "x from", "0"), field("xmax", "x up to", "4"), field("at", "x\xE2\x82\x80", "1"),
          choice("method", "stepping", { "automatic", "Crank\xE2\x80\x93Nicolson", "BDF2", "backward Euler" }, "automatic", "automatic uses Crank\xE2\x80\x93Nicolson for second order and BDF2 otherwise"), field("steps", "x steps", "auto"), field("points", "grid points", "auto", "points along each other axis") },
        { when(option("surface", "u(x, t) surface", true), "space=t"), when(option("profile", "Line at x\xE2\x82\x80", true), "space=t"), option("height_color", "Color by value", true), option("mesh", "Mesh lines"),
          when(option("show_u", "Show u", true), "functions=u, v|u, v, w"), when(option("show_v", "Show v", true), "functions=u, v|u, v, w"), when(option("show_w", "Show w", true), "functions=u, v, w") } });
    k.push_back({ "turmite", "misc", "Langton's ant", "turn, flip, move", "An ant on a grid of cells. Each step it checks the condition on the cell under it and follows one list of actions or the other. The defaults are Langton's ant, which builds a highway after about 10,000 steps.", Space::Plane,
        { eq("cell = 0", "condition on cell, k, x, y"), wide("then", "if true", "R F M", "R, L turn, U turn around, F flip, S2 set cell, M move"), wide("else", "if false", "L F M"), field("steps", "steps", "11000"), field("cell", "cell size", "0.1") },
        {} });
    return k;
}

} // namespace

const std::vector<KindInfo>& kinds() {
    static const std::vector<KindInfo> all = build();
    return all;
}

const KindInfo* find_kind(const std::string& id) {
    for (const KindInfo& k : kinds()) {
        if (k.id == id) return &k;
    }
    return nullptr;
}

std::vector<const KindInfo*> kinds_for_space(Space space) {
    std::vector<const KindInfo*> out;
    for (const KindInfo& k : kinds()) {
        if (k.id == "define" || k.space == space) out.push_back(&k);
    }
    return out;
}

bool shown_for(const std::string& condition, const std::function<std::string(const std::string&)>& value_of) {
    if (condition.empty()) return true;
    std::size_t start = 0;
    while (start <= condition.size()) {
        std::size_t end = condition.find(';', start);
        if (end == std::string::npos) end = condition.size();
        const std::string part = condition.substr(start, end - start);
        const std::size_t eq = part.find('=');
        if (eq != std::string::npos) {
            const std::string value = value_of(part.substr(0, eq));
            const std::string options = part.substr(eq + 1);
            bool any = false;
            std::size_t a = 0;
            while (a <= options.size()) {
                std::size_t b = options.find('|', a);
                if (b == std::string::npos) b = options.size();
                if (options.substr(a, b - a) == value) any = true;
                a = b + 1;
            }
            if (!any) return false;
        }
        start = end + 1;
    }
    return true;
}

const std::vector<std::string>& palette() {
    static const std::vector<std::string> p = { "#e5534b", "#4f8fe6", "#3fa65a", "#9e6fe0", "#f08c2e", "#d9488f", "#2bb3a6", "#c9a227" };
    return p;
}

} // namespace arbor
