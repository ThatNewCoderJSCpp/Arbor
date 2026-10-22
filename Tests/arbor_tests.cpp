#include "arbor/examples.hpp"
#include "arbor/expr.hpp"
#include "arbor/graph.hpp"
#include "arbor/library.hpp"
#include "arbor/png.hpp"
#include "arbor/program.hpp"
#include "arbor/render.hpp"
#include "arbor/scene.hpp"

#include <chrono>
#include <cmath>
#include <functional>
#include <map>
#include <iostream>
#include <string>
#include <vector>

using namespace arbor;

namespace {

int g_failed = 0;
int g_passed = 0;

void check(bool ok, const std::string& what) {
    if (ok) {
        ++g_passed;
    } else {
        ++g_failed;
        std::cerr << "FAIL: " << what << "\n";
    }
}

double eval_at(const std::string& text, const std::vector<std::string>& vars, std::vector<double> at, NumberType type = NumberType::Hardware) {
    ParseOptions o;
    for (const std::string& v : vars) o.known_names.insert(v);
    ParseResult r = parse_expression(text, o);
    if (!r.ok) return std::nan("");
    std::string err;
    Program p = compile(r.statement.lhs, vars, false, err);
    if (!p.valid) return std::nan("");
    auto ev = make_evaluator(p, type);
    std::vector<const double*> in;
    for (double& v : at) in.push_back(&v);
    double out = 0.0;
    ev->real(1, in.data(), &out);
    return out;
}

bool near(double a, double b, double tol = 1e-9) {
    return std::fabs(a - b) <= tol * std::max(1.0, std::fabs(b));
}

void test_parser() {
    check(near(eval_at("2x^2 + 3x - 1", { "x" }, { 2.0 }), 13.0), "implicit multiplication");
    check(near(eval_at("sin^2 x + cos^2 x", { "x" }, { 0.7 }), 1.0), "sin^2 x");
    check(near(eval_at("|x - 3|", { "x" }, { 1.0 }), 2.0), "absolute value bars");
    check(near(eval_at("sqrt(16) + 3!", {}, {}), 10.0), "sqrt and factorial");
    check(near(eval_at("e^(ln 5)", {}, {}), 5.0), "e^ and ln");
    check(near(eval_at("2pi", {}, {}), 6.283185307179586), "2pi");
    check(near(eval_at("(-8)^(1/3)", {}, {}), -2.0), "odd root of a negative");
    ParseResult def = parse_statement("f(x) = x^2 + 1");
    check(def.ok && def.statement.is_function_definition && def.statement.defines == "f", "function definition");
    ParseResult val = parse_statement("rate = 2");
    check(val.ok && val.statement.is_value_definition && val.statement.defines == "rate", "multi-letter value definition");
    ParseResult imp = parse_statement("xy = 1");
    check(imp.ok && !imp.statement.is_value_definition, "xy = 1 stays an implicit curve");
    ParseResult vec = parse_statement("<cos(t), sin(t), t>");
    check(vec.ok && component_count(vec.statement.lhs) == 3, "vector literal");
    ParseResult bad = parse_statement("sin(x");
    check(!bad.ok, "unbalanced parentheses is an error");
}

void test_precision() {
    ParseResult r = parse_expression("4atan(1)");
    std::string err;
    Program p = compile(r.statement.lhs, {}, false, err);
    const std::string q = make_evaluator(p, NumberType::Float)->text({}, 34);
    check(q.rfind("3.14159265358979323846264338327950", 0) == 0, "float pi digits: " + q);
    const std::string l = make_evaluator(p, NumberType::LongFloat)->text({}, 70);
    check(l.rfind("3.14159265358979323846264338327950288419716939937510582097494459230", 0) == 0, "long float pi digits: " + l);
    for (NumberType t : all_number_types()) {
        const double v = eval_at("exp(x)*sin(x) + x^3", { "x" }, { 0.5 }, t);
        const double tol = t == NumberType::ShortShortFloat ? 1e-6 : 1e-12;
        check(near(v, std::exp(0.5) * std::sin(0.5) + 0.125, tol), std::string("evaluates in ") + number_type_label(t));
    }
    ParseResult c = parse_expression("gamma(1 + i)");
    Program cp = compile(c.statement.lhs, {}, true, err);
    double re = 0, im = 0;
    make_evaluator(cp, NumberType::Hardware)->complex(1, nullptr, nullptr, &re, &im);
    check(near(re, 0.4980156681183560, 1e-10) && near(im, -0.1549498283018106, 1e-10), "complex gamma");
}

void test_tidy() {
    auto t = [](const std::string& s) { return to_string(tidy(parse_expression(s).statement.lhs)); };
    check(t("3*x^2/6 - 1") == "x^2 / 2 - 1", "tidy fractions: " + t("3*x^2/6 - 1"));
    check(t("cos(v)*(u*cos(v)) + u*sin(v)*sin(v)") == "u", "tidy sin^2 + cos^2");
    check(t("(u+1)*(u-1)") == "u^2 - 1", "tidy expands products");
    check(t("2x + 3x") == "5 * x", "tidy like terms");
    ExprPtr d = derivative(parse_expression("x^3/6 - x").statement.lhs, "x");
    check(d && to_string(tidy(simplify(d))) == "x^2 / 2 - 1", "derivative");
}

void test_graph_roundtrip() {
    Graph g;
    make_example("calculus", g);
    g.items[1].fields["eq"] = "y = \"quoted\" x";
    const std::string text = save_graph(g);
    Graph back;
    std::string err;
    check(load_graph(text, back, err), "load saved graph: " + err);
    check(back.items.size() == g.items.size(), "items survive saving");
    check(back.items.size() > 1 && back.items[1].fields["eq"] == g.items[1].fields["eq"], "fields with quotes survive saving");
    check(back.title == g.title && back.space == g.space, "title and space survive saving");
    check(!back.sliders.empty() && near(back.sliders[0].max, 3.0), "slider ranges survive saving");
}

void test_examples() {
    for (const Example& e : examples()) {
        Graph g;
        check(make_example(e.id, g), "make example " + e.id);
        BuildRequest req;
        req.graph = g;
        req.width = 320;
        req.height = 220;
        BuildControl control;
        Scene s = build_scene(req, control, NumberType::Hardware);
        bool clean = true;
        for (const ItemStatus& st : s.status) {
            if (!st.error.empty()) {
                clean = false;
                std::cerr << "  " << e.id << ": " << st.error << "\n";
            }
        }
        check(clean, "example builds without errors: " + e.id);
        const bool drew = !s.s2.strokes.empty() || !s.s2.rasters.empty() || !s.s2.tris.empty() || !s.s3.meshes.empty() || !s.s3.lines.empty() || !s.s2.arrows.empty();
        check(drew, "example draws something: " + e.id);
        RenderOptions o;
        o.space = g.space;
        o.view2 = g.view2;
        o.view3 = g.view3;
        Image img;
        img.resize(320, 220);
        render_scene(s, o, img);
        long differs = 0;
        for (std::size_t i = 0; i + 3 < img.px.size(); i += 4) {
            if (img.px[i] != img.px[0] || img.px[i + 1] != img.px[1] || img.px[i + 2] != img.px[2]) ++differs;
        }
        check(differs > 1000, "example renders: " + e.id);
    }
}

void test_precise_pass() {
    Graph g;
    make_example("calculus", g);
    BuildRequest req;
    req.graph = g;
    req.width = 200;
    req.height = 150;
    req.value_type = NumberType::Float;
    BuildControl control;
    Scene s = build_scene(req, control, NumberType::Float);
    bool long_readout = false;
    for (const ItemStatus& st : s.status) {
        for (const Readout& r : st.readouts) long_readout = long_readout || r.value.size() > 25;
    }
    check(long_readout, "float readouts show more than double's digits");
    check(s.plotted_with == NumberType::Float, "precise pass records its type");
}

bool complex_at(const std::string& text, int branch, double zr, double zi, double& re, double& im, NumberType type = NumberType::Hardware) {
    ParseOptions o;
    o.known_names.insert("z");
    ParseResult r = parse_expression(text, o);
    if (!r.ok) return false;
    std::string err;
    CompileBranch scope(branch);
    Program p = compile(r.statement.lhs, { "z" }, true, err);
    if (!p.valid) return false;
    const double* rin[1] = { &zr };
    const double* iin[1] = { &zi };
    make_evaluator(p, type)->complex(1, rin, iin, &re, &im);
    return true;
}

double real_branch(const std::string& text, int branch, double x, NumberType type = NumberType::Hardware) {
    CompileBranch scope(branch);
    return eval_at(text, { "x" }, { x }, type);
}

void test_branches() {
    const double pi = 3.14159265358979323846;
    double re = 0.0, im = 0.0;
    check(complex_at("ln(z)", 1, -1.0, 0.0, re, im) && near(re, 0.0, 1e-12) && near(im, 3.0 * pi), "ln on branch 1 adds 2 pi i");
    check(complex_at("ln(z)", -1, 1.0, 0.0, re, im) && near(im, -2.0 * pi), "ln on branch -1 subtracts 2 pi i");
    check(complex_at("sqrt(z)", 1, 4.0, 0.0, re, im) && near(re, -2.0) && near(im, 0.0, 1e-12), "sqrt on branch 1 is the other root");
    check(complex_at("sqrt(z)", 2, 4.0, 0.0, re, im) && near(re, 2.0), "sqrt on branch 2 is back to the principal root");
    check(complex_at("cbrt(z)", 1, 8.0, 0.0, re, im) && near(re, -1.0) && near(im, std::sqrt(3.0)), "cbrt on branch 1 turns by a third");
    check(complex_at("z^(1/2)", 1, 9.0, 0.0, re, im) && near(re, -3.0) && near(im, 0.0, 1e-9), "z^(1/2) follows the branch");
    check(complex_at("z^2", 1, 3.0, 0.0, re, im) && near(re, 9.0), "integer powers have no branches");
    check(complex_at("asin(z)", 1, 0.5, 0.0, re, im) && near(re, pi - pi / 6.0), "complex asin on branch 1");
    check(complex_at("gamma(z)", 1, 1.0, 1.0, re, im) && near(re, 0.4980156681183560, 1e-10), "gamma ignores the branch");
    check(complex_at("ln(z)", 1, -1.0, 0.0, re, im, NumberType::Float) && near(im, 3.0 * pi), "branches in Fizmo floats");
    check(near(real_branch("sqrt(x)", 1, 4.0), -2.0), "real sqrt on branch 1");
    check(near(real_branch("asin(x)", 1, 0.5), pi - pi / 6.0), "real asin on branch 1");
    check(near(real_branch("asin(x)", 2, 0.5), pi / 6.0 + 2.0 * pi), "real asin on branch 2");
    check(near(real_branch("acos(x)", 1, 0.5), 2.0 * pi - pi / 3.0), "real acos on branch 1");
    check(near(real_branch("atan(x)", -1, 1.0), pi / 4.0 - pi), "real atan on branch -1");
    check(near(real_branch("nthroot(x, 4)", 1, 16.0), -2.0), "even roots on branch 1");
    check(near(real_branch("nthroot(x, 3)", 1, 8.0), 2.0), "odd real roots have one real value");
    check(near(real_branch("sqrt(x)", 1, 4.0, NumberType::LongFloat), -2.0), "real branches in long float");
    ParseResult b = parse_expression("sin(x) + 2");
    check(b.ok && !uses_branches(b.statement.lhs, false) && !uses_branches(b.statement.lhs, true), "no branches in sin(x) + 2");
    ParseResult c = parse_expression("sqrt(x) + asin(x)");
    check(c.ok && uses_branches(c.statement.lhs, false), "sqrt and asin have branches");
    ParseResult d = parse_expression("z^(1/3)", ParseOptions{ { "z" }, {} });
    check(d.ok && uses_branches(d.statement.lhs, true) && !uses_branches(d.statement.lhs, false), "fractional powers branch only for complex");
}

std::vector<Readout> item_readouts(const std::string& kind, const std::string& eq, const std::map<std::string, std::string>& fields, const std::map<std::string, bool>& options, Space space) {
    Graph g;
    g.space = space;
    Item it;
    it.id = "item-1";
    it.kind = kind;
    it.fields = fields;
    it.fields["eq"] = eq;
    it.options = options;
    g.items.push_back(it);
    g.view2.scale = 60.0;
    BuildRequest req;
    req.graph = g;
    req.width = 600;
    req.height = 400;
    BuildControl control;
    Scene s = build_scene(req, control, NumberType::Hardware);
    return s.status.empty() ? std::vector<Readout>{} : s.status[0].readouts;
}

std::string item_error(const std::string& kind, const std::string& eq, Space space) {
    Graph g;
    g.space = space;
    Item it;
    it.id = "item-1";
    it.kind = kind;
    it.fields["eq"] = eq;
    g.items.push_back(it);
    g.view2.scale = 60.0;
    BuildRequest req;
    req.graph = g;
    req.width = 600;
    req.height = 400;
    BuildControl control;
    Scene s = build_scene(req, control, NumberType::Hardware);
    return s.status.empty() ? std::string() : s.status[0].error + s.status[0].note;
}

std::string readout(const std::vector<Readout>& rs, const std::string& label) {
    for (const Readout& r : rs) {
        if (r.label == label) return r.value;
    }
    return "(missing)";
}

void test_residues() {
    const auto a = item_readouts("domain", "(z^2 - 1)/(z^2 + 1)", {}, { { "residues", true } }, Space::Plane);
    check(readout(a, "pole i") == "Res = i", "residue at i: " + readout(a, "pole i"));
    check(readout(a, "pole -i") == "Res = -i", "residue at -i: " + readout(a, "pole -i"));
    const auto b = item_readouts("domain", "exp(z)/(z-1)^2", {}, { { "residues", true } }, Space::Plane);
    check(readout(b, "pole 1 (order 2)") == "Res = 2.718281828", "double pole residue: " + readout(b, "pole 1 (order 2)"));
    const auto c = item_readouts("domain", "1/(z*(z - 0.01))", {}, { { "residues", true } }, Space::Plane);
    check(readout(c, "pole 0") == "Res = -100" && readout(c, "pole 0.01") == "Res = 100", "two close poles are told apart");
    const auto d = item_readouts("domain", "sin(z)/z", {}, { { "residues", true } }, Space::Plane);
    check(readout(d, "poles in view") == "none", "a removable singularity is not a pole");
    const auto e = item_readouts("domain", "exp(1/z)", {}, { { "residues", true } }, Space::Plane);
    check(readout(e, "poles in view") == "none", "no false poles near an essential singularity");
    const auto f = item_readouts("domain", "gamma(z)", {}, { { "residues", true } }, Space::Plane);
    check(readout(f, "pole -3") == "Res = -0.1666666667", "gamma's residue at -3: " + readout(f, "pole -3"));
}

void test_complex_curve() {
    const auto a = item_readouts("complex_curve", "exp(i*t)", { { "min", "0" }, { "max", "2pi" }, { "axis", "up" }, { "at", "0" } }, { { "plane", true } }, Space::Solid);
    check(readout(a, "f(0)") == "1", "complex curve value at t0: " + readout(a, "f(0)"));
    check(readout(a, "f\xE2\x80\xB2(0)") == "i" || readout(a, "f\xE2\x80\xB2(0)") == "1i", "complex curve derivative: " + readout(a, "f\xE2\x80\xB2(0)"));
    Graph g;
    g.space = Space::Solid;
    Item it;
    it.id = "item-1";
    it.kind = "complex_curve";
    it.fields = { { "eq", "exp(i*t)" }, { "min", "0" }, { "max", "6" }, { "axis", "sideways" } };
    g.items.push_back(it);
    BuildRequest req;
    req.graph = g;
    BuildControl control;
    Scene s = build_scene(req, control, NumberType::Hardware);
    check(!s.status.empty() && !s.status[0].error.empty(), "a bad axis name is an error");
}

void test_special_functions() {
    check(near(eval_at("zeta(2)", {}, {}), 1.6449340668482264, 1e-12), "zeta(2)");
    check(near(eval_at("zeta(-1)", {}, {}), -1.0 / 12.0, 1e-12), "zeta(-1)");
    check(near(eval_at("lambertw(1)", {}, {}), 0.5671432904097838, 1e-13), "lambertw(1)");
    check(near(eval_at("LambertW(-0.2, -1)", {}, {}), -2.5426413577735265, 1e-12), "lambertw branch -1");
    check(near(eval_at("eulergamma", {}, {}), 0.5772156649015329), "euler-mascheroni constant");
    check(near(eval_at("golden", {}, {}), 1.618033988749895), "golden ratio constant");
    check(near(eval_at("digamma(1)", {}, {}), -0.5772156649015329, 1e-13), "digamma(1)");
    check(near(eval_at("erfc(0.5)", {}, {}), 0.4795001221869535, 1e-13), "erfc");
    check(near(eval_at("binomial(10, 3)", {}, {}), 120.0), "binomial");
    check(near(eval_at("if(x < 0, -x, x^2)", { "x" }, { -2.0 }), 2.0) && near(eval_at("if(x < 0, -x, x^2)", { "x" }, { 3.0 }), 9.0), "if with a condition");
    check(near(eval_at("7 mod 3", {}, {}), 1.0), "infix mod");
    check(std::isnan(eval_at("if(x > 1, x)", { "x" }, { 0.0 })), "if without an else is undefined");
    check(near(eval_at("zeta(2)", {}, {}, NumberType::Float), 1.6449340668482264, 1e-15), "zeta(2) in float");
    {
        ParseOptions o;
        o.known_names.insert("u");
        ParseResult r = parse_expression("u/2", o);
        std::string err;
        Program p = compile(r.statement.lhs, { "u" }, false, err);
        check(make_evaluator(p, NumberType::Float)->text({ 0.8 }, 34) == "0.4", "a typed 0.8 is the decimal 0.8 in float, not the double nearest it");
        check(make_evaluator(p, NumberType::LongLongDouble)->text({ 0.8 }, 600) == "0.4", "a typed 0.8 is the decimal 0.8 in long long double");
    }
    check(near(eval_at("lambertw(1)", {}, {}, NumberType::LongLongFloat), 0.5671432904097838, 1e-15), "lambertw in long long float");
    check(library_function("ProductLog") != nullptr && library_constant("eulergamma") != nullptr, "library lookups by alias");
    for (NumberType t : { NumberType::Hardware, NumberType::LongLongFloat }) {
        const std::string in = t == NumberType::Hardware ? " (double)" : " (long long float)";
        auto at = [t](const std::string& e) { return eval_at(e, {}, {}, t); };
        check(near(at("log(8, 2)"), 3.0, 1e-12), "log(x, b) takes the base second" + in);
        check(near(at("asech(0.5)"), 1.3169578969248166, 1e-12) && near(at("acsch(2)"), 0.48121182505960347, 1e-12) && near(at("acoth(2)"), 0.5493061443340548, 1e-12), "asech, acsch, acoth" + in);
        check(near(at("invgd(gd(0.7))"), 0.7, 1e-12), "invgd undoes gd" + in);
        check(near(at("invsinc(sinc(1.3))"), 1.3, 1e-10), "invsinc undoes sinc" + in);
        check(near(at("lagamma(-0.5)"), 1.2655121234846454, 1e-12) && std::isnan(at("lgamma(-0.5)")), "lagamma is ln|gamma|, lgamma needs gamma > 0" + in);
        check(near(at("mag(-3)"), 3.0) && near(at("mag2(-3)"), 9.0), "mag and mag2" + in);
        check(near(at("bernoulli(2)"), 1.0 / 6.0, 1e-13) && at("bernoulli(7)") == 0.0 && near(at("eulernumber(4)"), 5.0), "bernoulli and euler numbers" + in);
        check(near(at("stieltjes(0)"), 0.5772156649015329, 1e-12) && near(at("metallic(1)"), 1.618033988749895, 1e-13) && near(at("metallic(2)"), 2.414213562373095, 1e-13), "stieltjes and metallic ratios" + in);
        check(near(at("psi"), 0.6180339887498949, 1e-14) && near(at("bronze"), 3.302775637731995, 1e-13), "psi and bronze constants" + in);
        check(near(at("Hi(0)"), 0.4099510849640004, 1e-10) && near(at("Gi(0)"), 0.2049755424820002, 1e-10), "scorer Hi and Gi" + in);
        check(near(at("cd(0.5, 0.3)"), at("cn(0.5, 0.3)") / at("dn(0.5, 0.3)"), 1e-12), "jacobi cd" + in);
        check(near(at("carlsonRF(0, 1, 2)"), 1.3110287771460599, 1e-11), "carlson RF" + in);
        check(near(at("ellipticPi(pi/2, 0.3, 0.4)"), at("ellipticPi(0.3, 0.4)"), 1e-10), "incomplete ellipticPi at pi/2 is complete" + in);
        check(near(at("chebyshevT(0.5, 3)"), -1.0, 1e-12) && near(at("fibonacci(10)"), 55.0, 1e-9), "chebyshev and fibonacci" + in);
        check(near(at("Ei(1, 1)"), 0.21938393439552029, 1e-11) && near(at("clausen(1, 2)"), at("clausen(1)"), 1e-11), "En and Cl_s" + in);
    }
    check(!library_text().empty(), "library text");
}

void test_number_types() {
    check(number_type_from_id("long-long-float") == NumberType::LongLongFloat, "long long float id");
    check(number_type_from_id("double") == NumberType::LongLongFloat, "Walnut double is 512-bit");
    check(number_type_from_id("long-long-double") == NumberType::LongLongDouble, "long long double id");
    bool hardware = false;
    for (NumberType t : selectable_number_types()) hardware = hardware || t == NumberType::Hardware;
    check(!hardware, "the hardware double isn't offered as a number type");
    check(number_type_digits(NumberType::LongDouble) > 300, "long double digits");
}

void test_misc_kinds() {
    const auto ode = item_readouts("ode", "y'' = -y", { { "x0", "0" }, { "init", "0, 1" }, { "at", "1.5707963267948966" }, { "min", "-4" }, { "max", "4" } }, {}, Space::Plane);
    bool found = false;
    for (const Readout& r : ode) {
        if (r.label.rfind("y(", 0) == 0) {
            found = true;
            check(std::fabs(std::atof(r.value.c_str()) - 1.0) < 2e-3, "y'' = -y with y(0) = 0, y'(0) = 1 gives sin: " + r.value);
        }
    }
    check(found, "ODE readout");
    {
        Graph g;
        g.space = Space::Plane;
        Item it;
        it.id = "item-1";
        it.kind = "ode";
        it.fields = { { "eq", "y'' + y' = -y^2 + sqrt(3)" }, { "init", "1, 0" }, { "at", "1" } };
        it.options = { { "derivatives", true } };
        g.items.push_back(it);
        BuildRequest req;
        req.graph = g;
        req.width = 800;
        req.height = 600;
        BuildControl control;
        Scene scene = build_scene(req, control, NumberType::Hardware);
        RenderOptions o;
        o.space = g.space;
        o.view2 = g.view2;
        Image img;
        img.resize(800, 600);
        const auto t0 = std::chrono::steady_clock::now();
        render_scene(scene, o, img);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        check(ms < 2000.0, "an ODE that blows up draws its derivatives quickly: " + std::to_string(ms) + " ms");
    }
    const auto seq = item_readouts("sequence", "a mod 2 = 0", { { "then", "a/2" }, { "else", "3a + 1" }, { "start", "27" }, { "steps", "500" }, { "stop", "a = 1" } }, {}, Space::Plane);
    check(readout(seq, "stopped at step") == "111", "Collatz 27 takes 111 steps: " + readout(seq, "stopped at step"));
    check(readout(seq, "largest") == "9232", "Collatz 27 peaks at 9232: " + readout(seq, "largest"));
    const auto ant = item_readouts("turmite", "cell = 0", { { "then", "R F M" }, { "else", "L F M" }, { "steps", "11000" } }, {}, Space::Plane);
    check(readout(ant, "steps taken") == "11000", "Langton's ant runs every step");
    const auto esc = item_readouts("escape", "z^2 + c", { { "max", "50" } }, {}, Space::Plane);
    check(readout(esc, "steps at most") == "50", "escape-time set builds");
}

std::vector<ItemStatus> graph_status(Space space, const std::vector<Item>& items) {
    Graph g;
    g.space = space;
    g.items = items;
    g.view2.scale = 60.0;
    BuildRequest req;
    req.graph = g;
    req.width = 600;
    req.height = 400;
    BuildControl control;
    return build_scene(req, control, NumberType::Hardware).status;
}

double prefix_readout(const std::vector<Readout>& rs, const std::string& prefix) {
    for (const Readout& r : rs) {
        if (r.label.rfind(prefix, 0) == 0) return std::atof(r.value.c_str());
    }
    return std::nan("");
}

ItemStatus pde_status(const std::map<std::string, std::string>& fields, const std::string& kind = "pde") {
    Item it;
    it.id = "item-1";
    it.kind = kind;
    it.fields = fields;
    const std::vector<ItemStatus> st = graph_status(Space::Solid, { it });
    return st.empty() ? ItemStatus{} : st[0];
}

void test_pde_kinds() {
    const double pi = 3.141592653589793;
    const ItemStatus lap = pde_status({ { "space", "x, y" }, { "eq", "u_xx + u_yy = 0" }, { "edge_xmin", "u = 0" }, { "edge_xmax", "u = 0" }, { "edge_ymin", "u = 0" }, { "edge_ymax", "u = sin(pi x)" } }, "pde_steady");
    const double lv = prefix_readout(lap.readouts, "u(0.5, 0.5");
    check(lap.error.empty() && std::fabs(lv - std::sinh(pi / 2) / std::sinh(pi)) < 1e-3, "Laplace's equation on a square: " + lap.error + " " + std::to_string(lv));
    const ItemStatus rod = pde_status({ { "space", "x" }, { "eq", "u_xx = -1" }, { "edge_xmin", "u = 0" }, { "edge_xmax", "u_x = 0" } }, "pde_steady");
    const double rv = prefix_readout(rod.readouts, "u(0.5");
    check(rod.error.empty() && std::fabs(rv - 0.375) < 1e-6, "a rod with an insulated end: " + rod.error + " " + std::to_string(rv));
    const ItemStatus bratu = pde_status({ { "space", "x" }, { "eq", "u_xx + e^u = 0" }, { "edge_xmin", "u = 0" }, { "edge_xmax", "u = 0" } }, "pde_steady");
    const double bv = prefix_readout(bratu.readouts, "u(0.5");
    check(bratu.error.empty() && std::fabs(bv - 0.140539) < 2e-4, "Bratu's equation by Newton's method: " + bratu.error + " " + std::to_string(bv));
    const ItemStatus pseudo = pde_status({ { "eq", "u_t - u_xxt = u_xx" }, { "init", "sin(pi x)" }, { "edge_xmin", "u = 0" }, { "edge_xmax", "u = 0" }, { "xmin", "0" }, { "xmax", "1" }, { "tmax", "1" }, { "at", "1" } });
    const double pv = prefix_readout(pseudo.readouts, "u(0.5");
    check(pseudo.error.empty() && std::fabs(pv - std::exp(-pi * pi / (1 + pi * pi))) < 2e-3, "a mixed derivative u_xxt needs the full linear solve: " + pseudo.error + " " + std::to_string(pv));
    const ItemStatus pair = pde_status({ { "functions", "u, v" }, { "eq", "u_t = v" }, { "eq_v", "v_t = u_xx" }, { "init", "sin(pi x)" }, { "init_v", "0" }, { "edge_xmin", "u = 0, v = 0" }, { "edge_xmax", "u = 0, v = 0" }, { "xmin", "0" }, { "xmax", "1" }, { "tmax", "1" }, { "at", "0.25" }, { "method", "Crank\xE2\x80\x93Nicolson" } });
    const double qv = prefix_readout(pair.readouts, "u(0.5");
    check(pair.error.empty() && std::fabs(qv - std::cos(pi / 4)) < 3e-3, "a wave written as a pair u, v: " + pair.error + " " + std::to_string(qv));
    const ItemStatus beam = pde_status({ { "eq", "u_tt = -u_xxxx" }, { "init", "sin(pi x), 0" }, { "edge_xmin", "u = 0, u_xx = 0" }, { "edge_xmax", "u = 0, u_xx = 0" }, { "xmin", "0" }, { "xmax", "1" }, { "tmax", "0.2" }, { "at", "0.1" } });
    const double mv = prefix_readout(beam.readouts, "u(0.5");
    check(beam.error.empty() && std::fabs(mv - std::cos(pi * pi * 0.1)) < 5e-3, "a hinged beam vibrates at pi^2: " + beam.error + " " + std::to_string(mv));
    const ItemStatus march = pde_status({ { "eq", "u_xx = u_tt" }, { "init", "sin(pi t), pi cos(pi t)" }, { "edge_tmin", "periodic" }, { "tmin", "0" }, { "tmax", "2" }, { "start", "0" }, { "xmax", "1" }, { "at", "0.5" } }, "pde_march");
    const double xv = prefix_readout(march.readouts, "u(x\xE2\x82\x80, 1");
    check(march.error.empty() && std::fabs(xv + 1.0) < 3e-3, "marching a wave along x: " + march.error + " " + std::to_string(xv));
    const ItemStatus cube = pde_status({ { "space", "x, y, z" }, { "eq", "u_t = u_xx + u_yy + u_zz" }, { "init", "sin(pi x)sin(pi y)sin(pi z)" }, { "edge_xmin", "u = 0" }, { "edge_xmax", "u = 0" }, { "edge_ymin", "u = 0" }, { "edge_ymax", "u = 0" }, { "edge_zmin", "u = 0" }, { "edge_zmax", "u = 0" }, { "xmin", "0" }, { "xmax", "1" }, { "ymin", "0" }, { "ymax", "1" }, { "zmin", "0" }, { "zmax", "1" }, { "tmax", "0.1" }, { "at", "0.05" } });
    const double cv = prefix_readout(cube.readouts, "u(0.5, 0.5, 0.5");
    check(cube.error.empty() && std::fabs(cv - std::exp(-3 * pi * pi * 0.05)) < 1e-2, "heat in a cube: " + cube.error + " " + std::to_string(cv));
    const ItemStatus wrong = pde_status({ { "eq", "u_t = u_xx" } }, "pde_steady");
    check(!wrong.error.empty(), "a steady state can't have time derivatives: " + wrong.error);
    const ItemStatus extra = pde_status({ { "eq", "u_t = v_xx" } });
    check(extra.error.find("isn't one of the functions") != std::string::npos, "v needs two functions: " + extra.error);
}

void test_brachistochrone_pde() {
    const auto br = item_readouts("brachistochrone", "y = -2sin(x/2)", {}, {}, Space::Plane);
    const double tc = std::atof(readout(br, "time on the cycloid").c_str());
    check(std::fabs(tc - 3.141592653589793 / std::sqrt(9.81)) < 1e-3, "the cycloid from (0, 0) to (pi, -2) takes pi/sqrt(g): " + readout(br, "time on the cycloid"));
    const double tl = std::atof(readout(br, "time on the straight line").c_str());
    check(tl > tc, "the straight line is slower than the cycloid: " + readout(br, "time on the straight line"));
    const ItemStatus heat = pde_status({ { "eq", "u_t = 0.5u_xx" }, { "init", "sin(pi x)" }, { "edge", "0" }, { "xmin", "0" }, { "xmax", "1" }, { "tmax", "1" }, { "at", "0.5" } });
    const double hv = prefix_readout(heat.readouts, "u(0.5");
    check(heat.error.empty() && std::fabs(hv - std::exp(-0.25 * 3.141592653589793 * 3.141592653589793)) < 2e-3, "heat flow decays like e^(-pi^2 t/2): " + heat.error + " " + std::to_string(hv));
    const ItemStatus wave = pde_status({ { "eq", "u_tt = u_xx" }, { "init", "sin(pi x)" }, { "init2", "0" }, { "edge", "0" }, { "xmin", "0" }, { "xmax", "1" }, { "tmax", "2" }, { "at", "0.25" } });
    const double wv = prefix_readout(wave.readouts, "u(0.5");
    check(wave.error.empty() && std::fabs(wv - std::cos(0.25 * 3.141592653589793)) < 2e-3, "a standing wave swings like cos(pi t): " + wave.error + " " + std::to_string(wv));
    const ItemStatus h2 = pde_status({ { "eq", "u_t = u_xx + u_yy" }, { "init", "sin(pi x)sin(pi y)" }, { "edge", "0" }, { "xmin", "0" }, { "xmax", "1" }, { "ymin", "0" }, { "ymax", "1" }, { "tmax", "1" }, { "at", "0.1" } });
    const double v2 = prefix_readout(h2.readouts, "u(0.5, 0.5");
    check(h2.error.empty() && std::fabs(v2 - std::exp(-0.2 * 3.141592653589793 * 3.141592653589793)) < 3e-3, "2D heat decays like e^(-2pi^2 t): " + h2.error + " " + std::to_string(v2));
    const ItemStatus adv = pde_status({ { "eq", "u_t + u_x = 0" }, { "init", "sin(x)" }, { "edge", "periodic" }, { "tmax", "2" }, { "at", "1" } });
    const double av = prefix_readout(adv.readouts, "u(0");
    double ax = 0.0;
    for (const Readout& r : adv.readouts) {
        if (r.label.rfind("u(", 0) == 0) ax = std::atof(r.label.c_str() + 2);
    }
    check(adv.error.empty() && std::fabs(av - std::sin(ax - 1.0)) < 2e-3, "periodic advection moves sin(x) right: " + adv.error + " " + std::to_string(av));
    const ItemStatus back = pde_status({ { "eq", "u_t = -u_xx" }, { "init", "sin(x) + 0.01sin(20x)" }, { "edge", "0" }, { "tmax", "4" } });
    check(!back.error.empty(), "backward heat flow is reported as unstable: " + back.error);
}

void test_theta_slider() {
    Item def;
    def.id = "item-1";
    def.kind = "define";
    def.fields = { { "eq", "theta = 0.5" } };
    Item rot;
    rot.id = "item-2";
    rot.kind = "rotation";
    rot.fields = { { "eq", "y = x^2" }, { "angle", "theta" } };
    Item pol;
    pol.id = "item-3";
    pol.kind = "polar";
    pol.fields = { { "eq", "r = theta" }, { "min", "0" }, { "max", "6" }, { "at", "2" } };
    const std::vector<ItemStatus> st = graph_status(Space::Plane, { def, rot, pol });
    check(st.size() == 3 && st[0].error.empty(), "theta can be defined as a slider: " + (st.empty() ? std::string() : st[0].error));
    check(st.size() == 3 && readout(st[1].readouts, "angle").rfind("0.5 rad", 0) == 0, "a rotation by theta uses the slider: " + (st.size() == 3 ? readout(st[1].readouts, "angle") : std::string()));
    check(st.size() == 3 && st[2].error.empty() && std::fabs(std::atof(readout(st[2].readouts, "r(\xCE\xB8\xE2\x82\x80)").c_str()) - 2.0) < 1e-9, "a polar curve keeps theta as its coordinate: " + (st.size() == 3 ? readout(st[2].readouts, "r(\xCE\xB8\xE2\x82\x80)") : std::string()));
}

void test_revolve_rotate() {
    const auto rev = item_readouts("revolution", "y = sqrt(x)", { { "axis", "x-axis" }, { "min", "0" }, { "max", "4" }, { "angle", "2pi" } }, {}, Space::Solid);
    check(std::fabs(std::atof(readout(rev, "volume").c_str()) - 8.0 * 3.141592653589793) < 1e-3, "volume of revolution of sqrt(x) on [0, 4] is 8 pi: " + readout(rev, "volume"));
    const auto line = item_readouts("revolution", "y = 2", { { "axis", "y = 1" }, { "min", "0" }, { "max", "3" }, { "angle", "2pi" } }, {}, Space::Solid);
    check(std::fabs(std::atof(readout(line, "volume").c_str()) - 3.0 * 3.141592653589793) < 1e-4, "a cylinder about y = 1: " + readout(line, "volume"));
    const auto horn = item_readouts("revolution", "y = 1/x", { { "axis", "x-axis" }, { "min", "1" }, { "max", "" }, { "angle", "2pi" } }, {}, Space::Solid);
    check(std::fabs(std::atof(readout(horn, "volume (improper integral)").c_str()) - 3.141592653589793) < 1e-5, "Gabriel's horn holds pi: " + readout(horn, "volume (improper integral)"));
    check(readout(horn, "surface area (improper integral)").find("diverges") != std::string::npos, "Gabriel's horn has infinite area: " + readout(horn, "surface area (improper integral)"));
    const auto spike = item_readouts("revolution", "y = 1/x", { { "axis", "x-axis" }, { "min", "0" }, { "max", "4" }, { "angle", "2pi" } }, {}, Space::Solid);
    check(readout(spike, "volume").find("diverges") != std::string::npos, "1/x turned on (0, 4] holds infinite volume: " + readout(spike, "volume"));
    const auto bell = item_readouts("revolution", "y = exp(-x^2)", { { "axis", "x-axis" }, { "min", "-inf" }, { "max", "inf" }, { "angle", "2pi" } }, {}, Space::Solid);
    check(std::fabs(std::atof(readout(bell, "volume (improper integral)").c_str()) - 3.141592653589793 * std::sqrt(3.141592653589793 / 2.0)) < 1e-5, "a revolved bell curve over the whole line: " + readout(bell, "volume (improper integral)") + " " + (bell.empty() ? std::string("no readouts") : bell[0].label + " " + bell.back().label + " " + bell.back().value));
    {
        Graph g;
        g.space = Space::Plane;
        Item it;
        it.id = "item-1";
        it.kind = "parametric";
        it.fields = { { "eq", "<t, t^2/4>" }, { "min", "" }, { "max", "" }, { "at", "1" } };
        it.options = { { "arrows", true } };
        g.items.push_back(it);
        BuildRequest req;
        req.graph = g;
        BuildControl control;
        const Scene s = build_scene(req, control, NumberType::Hardware);
        check(!s.status.empty() && s.status[0].error.empty() && !s.s2.strokes.empty() && s.s2.strokes[0].pts.size() > 100, "an unbounded parametric curve draws");
    }
    for (bool symbols : { true, false }) {
        Graph g;
        g.space = Space::Solid;
        Item it;
        it.id = "item-1";
        it.kind = "curve3d";
        it.fields = { { "eq", "<cos(t), sin(t), t/4>" }, { "min", "0" }, { "max", "4pi" }, { "at", "2" } };
        g.items.push_back(it);
        BuildRequest req;
        req.graph = g;
        req.symbols = symbols;
        BuildControl control;
        const Scene s = build_scene(req, control, NumberType::Hardware);
        std::string t_symbol = "(missing)";
        for (const Readout& r : s.status[0].readouts) {
            if (r.label.rfind("T = ", 0) == 0) t_symbol = r.symbol;
        }
        check(symbols ? !t_symbol.empty() && t_symbol != "(missing)" : t_symbol.empty(), std::string("the Frenet T readout ") + (symbols ? "has" : "leaves out") + " its formula: " + t_symbol);
    }
    const auto rot = item_readouts("rotation", "y = x", { { "angle", "pi/2" } }, {}, Space::Plane);
    check(readout(rot, "r(t)") != "(missing)", "rotation gives a parametrization");
    Graph g;
    g.space = Space::Solid;
    Item it;
    it.id = "item-1";
    it.kind = "complex_curve";
    it.fields = { { "eq", "ln(t)" }, { "min", "-1" }, { "max", "4pi" }, { "at", "1" } };
    g.items.push_back(it);
    BuildRequest req;
    req.graph = g;
    req.width = 900;
    req.height = 600;
    BuildControl control;
    Scene scene = build_scene(req, control, NumberType::Hardware);
    RenderOptions o;
    o.space = g.space;
    o.view3 = g.view3;
    o.supersample = 2;
    Image img;
    img.resize(900, 600);
    const auto t0 = std::chrono::steady_clock::now();
    render_scene(scene, o, img);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    check(ms < 3000.0, "ln(t) from -1 draws without hanging: " + std::to_string(ms) + " ms");
}

void test_regions() {
    const double pi = 3.141592653589793;
    const auto disk = item_readouts("region2d", "x^2 + y^2 < 4", {}, { { "shade", true }, { "boundary", true } }, Space::Plane);
    check(std::fabs(std::atof(readout(disk, "area in view \xE2\x89\x88").c_str()) - 4.0 * pi) < 0.2, "a disk's area from its condition: " + readout(disk, "area in view \xE2\x89\x88"));
    const auto mixed = item_readouts("region2d", "r < 2, theta < pi", {}, { { "shade", true } }, Space::Plane);
    check(std::fabs(std::atof(readout(mixed, "area in view \xE2\x89\x88").c_str()) - 2.0 * pi) < 0.15, "a half disk from r and theta: " + readout(mixed, "area in view \xE2\x89\x88") + " " + item_error("region2d", "r < 2, theta < pi", Space::Plane));
    const auto bad = item_readouts("region2d", "x = y", {}, {}, Space::Plane);
    check(readout(bad, "area in view \xE2\x89\x88") == "(missing)", "= isn't a region");
    const auto lens = item_readouts("between2d", "x^2", { { "upper", "x" }, { "min", "" }, { "max", "" }, { "var", "x" } }, { { "shade", true } }, Space::Plane);
    check(std::fabs(std::atof(readout(lens, "area").c_str()) - 1.0 / 6.0) < 1e-5, "between y = x^2 and y = x, found where they cross: " + readout(lens, "area"));
    const auto sideways = item_readouts("between2d", "x = y^2", { { "upper", "y + 2" }, { "min", "" }, { "max", "" }, { "var", "x" } }, {}, Space::Plane);
    check(std::fabs(std::atof(readout(sideways, "area").c_str()) - 4.5) < 1e-4, "x between y^2 and y + 2: " + readout(sideways, "area"));
    const auto ring = item_readouts("polar_region", "1", { { "upper", "2" }, { "min", "0" }, { "max", "2pi" } }, {}, Space::Plane);
    check(std::fabs(std::atof(readout(ring, "area").c_str()) - 3.0 * pi) < 1e-4, "a polar ring 1 < r < 2: " + readout(ring, "area"));
    const auto ball = item_readouts("region3d", "rho < 2", { { "resolution", "60" } }, { { "boundary", true } }, Space::Solid);
    check(std::fabs(std::atof(readout(ball, "volume in the box \xE2\x89\x88").c_str()) - 32.0 * pi / 3.0) < 0.35, "a ball from rho < 2: " + readout(ball, "volume in the box \xE2\x89\x88"));
    const auto wedge = item_readouts("region3d", "x^2 + y^2 < 4, 0 < z < 3 - x", {}, { { "boundary", true } }, Space::Solid);
    check(std::fabs(std::atof(readout(wedge, "volume in the box \xE2\x89\x88").c_str()) - 12.0 * pi) < 0.6, "the cylinder cut by z = 3 - x holds 12 pi: " + readout(wedge, "volume in the box \xE2\x89\x88"));
    const auto dome = item_readouts("between3d", "0", { { "upper", "4 - x^2 - y^2" }, { "xmin", "-2" }, { "xmax", "2" }, { "ymin", "-sqrt(4 - x^2)" }, { "ymax", "sqrt(4 - x^2)" } }, { { "top", true }, { "walls", true } }, Space::Solid);
    check(std::fabs(std::atof(readout(dome, "volume").c_str()) - 8.0 * pi) < 1e-3, "the solid under 4 - x^2 - y^2 is 8 pi: " + readout(dome, "volume"));
    const auto cyl = item_readouts("implicit_cylindrical", "r^2 = 1 + x^2/2", { { "axis", "x" }, { "resolution", "30" } }, {}, Space::Solid);
    check(readout(cyl, "triangles") != "(missing)" && readout(cyl, "triangles") != "0", "an implicit cylindrical surface about the x axis draws");
    const auto mix3 = item_readouts("mixed3d", "rho*r = 2", { { "axis", "z" }, { "resolution", "30" } }, {}, Space::Solid);
    check(readout(mix3, "triangles") != "(missing)" && readout(mix3, "triangles") != "0", "a surface mixing rho, r and theta draws");
}

void test_display() {
    auto show = [](const std::string& text) {
        const ParseResult r = parse_statement_as_typed(text);
        return r.ok ? display_mathtext(r.statement) : std::string("error");
    };
    check(show("y = sqrt(x+1)") == "y = sqrt(x + 1)", "display sqrt");
    check(show("cbrt(x)") == "root(3, x)", "display cbrt as a root");
    check(show("a_1^2") == "a_{1}^{2}", "display subscripts and powers");
    check(show("a_{12}") == "a_{12}", "braced subscripts");
    check(show("sin^2 x") == "sin^{2}(x)", "display sin squared");
    check(show("1/(2x)") == "{(1)/(2 x)}", "display fractions");
    check(show("log(x, 2)") == "log_{2}(x)", "display log with its base");
    {
        const ExprPtr sq = pow(neg(call("sin", { variable("t") })), number(2.0));
        const ExprPtr e = tidy(add(add(sq, pow(call("cos", { variable("t") }), number(2.0))), number(3.0)));
        check(to_string(e) == "4", "tidy keeps (-sin t)^2 positive and merges sin^2 + cos^2: " + to_string(e));
    }
    check(show("lambertw(x)") == "LambertW(x)" && show("invsinc(x)") == "InverseSinc(x)", "LambertW and InverseSinc show their full names");
    check(show("bernoulli(n) + chebyshevT(x, 3)") == "B_{n} + T_{3}(x)", "display subscripted sequences and polynomials");
}

void test_png() {
    std::vector<std::uint8_t> px(16 * 8 * 4, 200);
    const std::vector<std::uint8_t> png = encode_png(px.data(), 16, 8);
    check(png.size() > 40 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G', "png signature");
}

} // namespace

int main() {
    test_parser();
    test_precision();
    test_tidy();
    test_graph_roundtrip();
    test_examples();
    test_precise_pass();
    test_branches();
    test_residues();
    test_complex_curve();
    test_display();
    test_regions();
    test_special_functions();
    test_number_types();
    test_misc_kinds();
    test_revolve_rotate();
    test_theta_slider();
    test_brachistochrone_pde();
    test_pde_kinds();
    test_png();
    std::cout << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
