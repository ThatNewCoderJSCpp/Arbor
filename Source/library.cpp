#include "arbor/library.hpp"

#include <algorithm>

namespace arbor {

namespace {

using Args = std::vector<std::pair<std::string, std::string>>;

LibEntry fn(const std::string& category, const std::string& name, int min_args, int max_args, const std::string& usage, const std::string& symbol, const std::string& description, Args args = {}, std::vector<std::string> aliases = {}) {
    LibEntry e;
    e.kind = LibKind::Function;
    e.category = category;
    e.name = name;
    e.min_args = min_args;
    e.max_args = max_args;
    e.usage = usage;
    e.symbol = symbol;
    e.description = description;
    e.args = std::move(args);
    e.aliases = std::move(aliases);
    return e;
}

LibEntry real_only(LibEntry e) {
    e.complex = false;
    return e;
}

LibEntry branched(LibEntry e) {
    e.branched = true;
    return e;
}

LibEntry paren(LibEntry e) {
    e.needs_paren = true;
    return e;
}

LibEntry complex_only(LibEntry e) {
    e.complex_only = true;
    return e;
}

LibEntry constant(const std::string& name, const std::string& symbol, const std::string& literal, const std::string& value, const std::string& description, std::vector<std::string> aliases = {}) {
    LibEntry e;
    e.kind = LibKind::Constant;
    e.category = "Constants";
    e.name = name;
    e.usage = name;
    e.symbol = symbol;
    e.literal = literal;
    e.value = value;
    e.description = description;
    e.aliases = std::move(aliases);
    e.min_args = e.max_args = 0;
    return e;
}

LibEntry variable(const std::string& category, const std::string& name, const std::string& symbol, const std::string& description) {
    LibEntry e;
    e.kind = LibKind::Variable;
    e.category = category;
    e.name = name;
    e.usage = name;
    e.symbol = symbol;
    e.description = description;
    e.min_args = e.max_args = 0;
    return e;
}

LibEntry op(const std::string& usage, const std::string& symbol, const std::string& description) {
    LibEntry e;
    e.kind = LibKind::Operator;
    e.category = "Operators";
    e.name = usage;
    e.usage = usage;
    e.symbol = symbol;
    e.description = description;
    e.min_args = e.max_args = 0;
    return e;
}

std::vector<LibEntry> build() {
    std::vector<LibEntry> v;
    const Args X = { { "x", "any number" } };
    const Args Z = { { "z", "any number (real or complex)" } };

    v.push_back(constant("pi", "\xCF\x80", "@pi", "3.14159265358979323846264338327950288", "Half the turn of a circle: the circumference over the diameter.", { "\xCF\x80" }));
    v.push_back(constant("tau", "\xCF\x84", "@tau", "6.28318530717958647692528676655900577", "A full turn, 2\xCF\x80.", { "\xCF\x84" }));
    v.push_back(constant("e", "e", "@e", "2.71828182845904523536028747135266250", "The base of the natural logarithm, the limit of (1 + 1/n)\xE2\x81\xBF."));
    v.push_back(constant("golden", "\xCF\x86", "@golden", "1.61803398874989484820458683436563812", "The golden ratio (1 + \xE2\x88\x9A" "5)/2.", { "goldenratio", "golden_ratio" }));
    v.push_back(constant("eulergamma", "\xCE\xB3", "@euler", "0.57721566490153286060651209008240243", "The Euler-Mascheroni constant: the limit of H\xE2\x82\x99 - ln n.", { "euler_gamma", "eulermascheroni", "euler_mascheroni" }));
    v.push_back(constant("catalan", "G", "@catalan", "0.91596559417721901505460351493238411", "Catalan's constant \xCE\xB2(2) = 1 - 1/3\xC2\xB2 + 1/5\xC2\xB2 - \xE2\x80\xA6", { "catalans" }));
    v.push_back(constant("apery", "\xCE\xB6(3)", "@apery", "1.20205690315959428539973816151144999", "Ap\xC3\xA9ry's constant \xCE\xB6(3) = 1 + 1/2\xC2\xB3 + 1/3\xC2\xB3 + \xE2\x80\xA6", { "aperys" }));
    v.push_back(constant("silver", "\xCE\xB4\xE2\x82\x9B", "@silver", "2.41421356237309504880168872420969808", "The silver ratio 1 + \xE2\x88\x9A" "2.", { "silverratio", "silver_ratio" }));
    v.push_back(constant("psi", "\xCF\x88", "@psi", "0.61803398874989484820458683436563812", "The golden ratio conjugate (\xE2\x88\x9A" "5 - 1)/2 = 1/\xCF\x86 = \xCF\x86 - 1.", { "\xCF\x88", "invgolden", "goldenconjugate" }));
    v.push_back(constant("silverconstant", "\xCF\x82", "@silverconstant", "3.24697960371746706105000976800847962", "The silver constant 2 + 2cos(2\xCF\x80/7), the Beraha constant B\xE2\x82\x87.", { "silver_constant" }));
    v.push_back(constant("bronze", "\xCF\x83\xE2\x82\x83", "@bronze", "3.30277563773199464655961063373524797", "The bronze ratio (3 + \xE2\x88\x9A" "13)/2, the third metallic ratio.", { "bronzeratio", "bronze_ratio" }));
    v.push_back(constant("inf", "\xE2\x88\x9E", "@inf", "\xE2\x88\x9E", "Infinity, for limits like t from 0 to inf.", { "infinity", "\xE2\x88\x9E" }));
    v.push_back(constant("i", "i", "@i", "\xE2\x88\x9A(-1)", "The imaginary unit, in complex graphs."));

    v.push_back(op("+ - * /", "+ \xE2\x88\x92 \xC2\xB7 \xC3\xB7", "Add, subtract, multiply, divide. Multiplication can be left out: 2x, 3sin x, (x+1)(x-1)."));
    v.push_back(op("^", "x\xE2\x81\xBF", "Powers: x^2, e^(-x), x^(1/3). \xC2\xB2 and \xC2\xB3 work too."));
    v.push_back(op("!", "n!", "Factorial, \xCE\x93(n + 1), for any real or complex n."));
    v.push_back(op("|x|", "|x|", "Absolute value (modulus for complex numbers)."));
    v.push_back(op("_", "a\xE2\x82\x81", "Subscripts in names: a_1, x_n, v_{12}."));
    v.push_back(op("< <= > >= = !=", "< \xE2\x89\xA4 > \xE2\x89\xA5 = \xE2\x89\xA0", "Comparisons. At the top level they make inequalities and equations to graph; inside if(...) they're conditions (1 when true, 0 when false). Chains work: 0 < x < 1."));
    v.push_back(op("and or not", "\xE2\x88\xA7 \xE2\x88\xA8 \xC2\xAC", "Combine conditions inside if(...) and in the Misc conditions."));
    v.push_back(op("<a, b>", "\xE2\x9F\xA8" "a, b\xE2\x9F\xA9", "A vector, for parametric curves, vector fields and maps."));

    const std::string VE = "Vectors";
    v.push_back(op("v.x  v.y  v.z", "v\xE2\x82\x93", "One part of a vector: r(t).x, F(x, y).y, or by number, v.1, v.2, v.3."));
    v.push_back(op("+ - * / with vectors", "u + v", "Vectors add and subtract part by part, a number times a vector scales it, a vector over a number divides each part, and two vectors multiplied give their dot product. |v| is the length."));
    v.push_back(paren(fn(VE, "dot", 2, 2, "dot(u, v)", "u\xC2\xB7v", "The dot product: the parts multiplied in pairs and added.", { { "u", "a vector" }, { "v", "a vector with as many parts" } })));
    v.push_back(paren(fn(VE, "cross", 2, 2, "cross(u, v)", "u\xC3\x97v", "The cross product of two 3D vectors; for 2D vectors, the number u.x v.y - u.y v.x.", { { "u", "a vector" }, { "v", "a vector" } })));
    v.push_back(paren(fn(VE, "norm", 1, 1, "norm(v)", "\xE2\x80\x96v\xE2\x80\x96", "The length of a vector, the same as |v|.", { { "v", "a vector" } }, { "length" })));
    v.push_back(paren(fn(VE, "unit", 1, 1, "unit(v)", "v\xCC\x82", "The vector of length 1 pointing the same way as v.", { { "v", "a vector" } }, { "normalize" })));
    v.push_back(paren(fn(VE, "component", 2, 2, "component(v, k)", "v\xE2\x82\x96", "Part k of v, counting from 1; v.x, v.y and v.z are the short forms.", { { "v", "a vector" }, { "k", "1, 2, 3 or 4" } })));

    const std::string SI = "Sums and integrals";
    v.push_back(op("sum_{n=a}^{b}", "\xE2\x88\x91", "A sum typed the way it's written: sum_{n=1}^{10} 1/n^2 adds up the term after it (one product; put a sum of terms in parentheses). The bounds can use x, so y = sum_{n=0}^{20} x^n/n! graphs, and the top can be inf."));
    v.push_back(op("prod_{k=a}^{b}", "\xE2\x88\x8F", "A product typed the way it's written: prod_{k=1}^{n} (1 + 1/k^2)."));
    v.push_back(op("int_{a}^{b} ... dt", "\xE2\x88\xAB", "An integral typed the way it's written: int_{0}^{x} sin(t)/t dt. Everything up to the d and the variable is integrated, the bounds can use x and inf, and integrals nest for double and triple ones: int_{0}^{1} int_{0}^{x} x y dy dx."));
    v.push_back(paren(fn(SI, "sum", 4, 4, "sum(expression, n, from, to)", "\xE2\x88\x91", "Adds the expression for n = from, from + 1, ... up to to. The bounds can depend on x, and to can be inf (it stops once the terms stop mattering).", { { "expression", "what to add up, using n" }, { "n", "the counting variable (any name)" }, { "from", "where it starts (rounded up to a whole number)" }, { "to", "where it stops (rounded down), or inf" } }, { "summation", "Sum" })));
    v.push_back(paren(fn(SI, "prod", 4, 4, "prod(expression, k, from, to)", "\xE2\x88\x8F", "Multiplies the expression for k = from, from + 1, ... up to to.", { { "expression", "what to multiply, using k" }, { "k", "the counting variable" }, { "from", "where it starts" }, { "to", "where it stops, or inf" } }, { "product", "Product" })));
    v.push_back(paren(fn(SI, "integral", 4, 4, "integral(expression, t, a, b)", "\xE2\x88\xAB", "The definite integral of the expression dt from a to b, worked out numerically (adaptive Gauss-Kronrod). a and b can depend on x, so y = integral(e^(-t^2), t, 0, x) graphs an antiderivative, and either can be inf.", { { "expression", "the integrand, using t" }, { "t", "the variable of integration (any name)" }, { "a", "the lower bound" }, { "b", "the upper bound" } }, { "int", "integrate", "defint" })));
    v.push_back(paren(fn(SI, "doubleintegral", 7, 7, "doubleintegral(expression, x, a, b, y, c, d)", "\xE2\x88\xAC", "A double integral. The bounds of one variable can use the other (c and d can use x), and Arbor integrates that one on the inside.", { { "expression", "the integrand" }, { "x", "the first variable" }, { "a, b", "its bounds" }, { "y", "the second variable" }, { "c, d", "its bounds" } }, { "iint", "dblint" })));
    v.push_back(paren(fn(SI, "tripleintegral", 10, 10, "tripleintegral(expression, x, a, b, y, c, d, z, e, f)", "\xE2\x88\xAD", "A triple integral; inner bounds can use the outer variables.", { { "expression", "the integrand" }, { "x, a, b", "the first variable and its bounds" }, { "y, c, d", "the second" }, { "z, e, f", "the third" } }, { "iiint" })));
    v.push_back(paren(fn(SI, "maximum", 4, 4, "maximum(expression, t, a, b)", "max", "The largest value of the expression for t from a to b (the absolute maximum on that interval).", { { "expression", "using t" }, { "t", "the variable" }, { "a", "the start" }, { "b", "the end" } }, { "maxof", "fmax" })));
    v.push_back(paren(fn(SI, "minimum", 4, 4, "minimum(expression, t, a, b)", "min", "The smallest value of the expression for t from a to b.", { { "expression", "using t" }, { "t", "the variable" }, { "a", "the start" }, { "b", "the end" } }, { "minof", "fmin" })));
    v.push_back(paren(fn(SI, "argmax", 4, 4, "argmax(expression, t, a, b)", "argmax", "Where the expression is largest for t from a to b.", { { "expression", "using t" }, { "t", "the variable" }, { "a", "the start" }, { "b", "the end" } })));
    v.push_back(paren(fn(SI, "argmin", 4, 4, "argmin(expression, t, a, b)", "argmin", "Where the expression is smallest for t from a to b.", { { "expression", "using t" }, { "t", "the variable" }, { "a", "the start" }, { "b", "the end" } })));

    v.push_back(paren(fn("Logic", "if", 2, 3, "if(condition, a, b)", "if", "a where the condition holds and b where it doesn't (b is left out: undefined there). Conditions use < <= > >= = != and, or, not.", { { "condition", "a comparison like x < 0, or any number (nonzero is true)" }, { "a", "the value where it's true" }, { "b", "the value where it's false (optional)" } }, { "piecewise", "ifelse" })));

    const std::string T = "Trigonometric";
    v.push_back(fn(T, "sin", 1, 1, "sin(x)", "sin", "Sine.", X));
    v.push_back(fn(T, "cos", 1, 1, "cos(x)", "cos", "Cosine.", X));
    v.push_back(fn(T, "tan", 1, 1, "tan(x)", "tan", "Tangent.", X));
    v.push_back(fn(T, "sec", 1, 1, "sec(x)", "sec", "Secant, 1/cos x.", X));
    v.push_back(fn(T, "csc", 1, 1, "csc(x)", "csc", "Cosecant, 1/sin x.", X));
    v.push_back(fn(T, "cot", 1, 1, "cot(x)", "cot", "Cotangent, cos x / sin x.", X));
    v.push_back(branched(fn(T, "asin", 1, 1, "arcsin(x)", "arcsin", "Inverse sine. Many-valued: the branch box picks which.", X, { "arcsin" })));
    v.push_back(branched(fn(T, "acos", 1, 1, "arccos(x)", "arccos", "Inverse cosine.", X, { "arccos" })));
    v.push_back(branched(fn(T, "atan", 1, 2, "arctan(x), arctan(y, x)", "arctan", "Inverse tangent; with two inputs, the angle of the point (x, y).", { { "y", "a number, or the y of a point" }, { "x", "with two inputs, the x of the point" } }, { "arctan" })));
    v.push_back(fn(T, "atan2", 2, 2, "atan2(y, x)", "atan2", "The angle of the point (x, y), from -\xCF\x80 to \xCF\x80.", { { "y", "y coordinate" }, { "x", "x coordinate" } }));
    v.push_back(branched(fn(T, "asec", 1, 1, "arcsec(x)", "arcsec", "Inverse secant.", X, { "arcsec" })));
    v.push_back(branched(fn(T, "acsc", 1, 1, "arccsc(x)", "arccsc", "Inverse cosecant.", X, { "arccsc" })));
    v.push_back(branched(fn(T, "acot", 1, 1, "arccot(x)", "arccot", "Inverse cotangent.", X, { "arccot" })));
    v.push_back(fn(T, "sinc", 1, 1, "sinc(x)", "sinc", "sin x / x, with sinc 0 = 1.", X));
    v.push_back(fn(T, "gd", 1, 1, "gd(x)", "gd", "The Gudermannian function, arctan(sinh x).", X, { "gudermannian" }));

    const std::string H = "Hyperbolic";
    v.push_back(fn(H, "sinh", 1, 1, "sinh(x)", "sinh", "Hyperbolic sine.", X));
    v.push_back(fn(H, "cosh", 1, 1, "cosh(x)", "cosh", "Hyperbolic cosine.", X));
    v.push_back(fn(H, "tanh", 1, 1, "tanh(x)", "tanh", "Hyperbolic tangent.", X));
    v.push_back(fn(H, "sech", 1, 1, "sech(x)", "sech", "Hyperbolic secant.", X));
    v.push_back(fn(H, "csch", 1, 1, "csch(x)", "csch", "Hyperbolic cosecant.", X));
    v.push_back(fn(H, "coth", 1, 1, "coth(x)", "coth", "Hyperbolic cotangent.", X));
    v.push_back(branched(fn(H, "asinh", 1, 1, "arcsinh(x)", "arcsinh", "Inverse hyperbolic sine.", X, { "arcsinh", "arsinh" })));
    v.push_back(branched(fn(H, "acosh", 1, 1, "arccosh(x)", "arccosh", "Inverse hyperbolic cosine.", X, { "arccosh", "arcosh" })));
    v.push_back(branched(fn(H, "atanh", 1, 1, "arctanh(x)", "arctanh", "Inverse hyperbolic tangent.", X, { "arctanh", "artanh" })));
    v.push_back(branched(fn(H, "asech", 1, 1, "arcsech(x)", "arcsech", "Inverse hyperbolic secant, arccosh(1/x), for 0 < x \xE2\x89\xA4 1.", X, { "arcsech", "arsech" })));
    v.push_back(fn(H, "acsch", 1, 1, "arccsch(x)", "arccsch", "Inverse hyperbolic cosecant, arcsinh(1/x), for x \xE2\x89\xA0 0.", X, { "arccsch", "arcsch" }));
    v.push_back(branched(fn(H, "acoth", 1, 1, "arccoth(x)", "arccoth", "Inverse hyperbolic cotangent, arctanh(1/x), for |x| > 1.", X, { "arccoth", "arcoth" })));
    v.push_back(fn(H, "invgd", 1, 1, "invgd(x)", "gd\xE2\x81\xBB\xC2\xB9", "The inverse Gudermannian function, arctanh(sin x) = ln|sec x + tan x|, for |x| < \xCF\x80/2.", X, { "inversegudermannian", "InverseGudermannian", "arcgd", "lambertian" }));

    const std::string L = "Exponentials and logarithms";
    v.push_back(fn(L, "exp", 1, 1, "exp(x)", "exp", "e^x.", X));
    v.push_back(branched(fn(L, "ln", 1, 1, "ln(x)", "ln", "Natural logarithm. In the complex plane, the branch box adds 2\xCF\x80ki.", X)));
    v.push_back(branched(fn(L, "log", 1, 2, "log(x), log(x, b)", "log", "Base-10 logarithm, or with two inputs the base-b logarithm of x.", { { "x", "the number" }, { "b", "the base (with two inputs)" } })));
    v.push_back(branched(fn(L, "log2", 1, 1, "log2(x)", "log\xE2\x82\x82", "Base-2 logarithm.", X)));
    v.push_back(branched(fn(L, "log10", 1, 1, "log10(x)", "log\xE2\x82\x81\xE2\x82\x80", "Base-10 logarithm.", X, { "lg" })));
    v.push_back(fn(L, "expm1", 1, 1, "expm1(x)", "expm1", "e^x - 1, accurate when x is tiny.", X));
    v.push_back(fn(L, "log1p", 1, 1, "log1p(x)", "log1p", "ln(1 + x), accurate when x is tiny.", X));
    v.push_back(branched(fn(L, "sqrt", 1, 1, "sqrt(x)", "\xE2\x88\x9A", "Square root. \xE2\x88\x9A works too. Branch 1 is the negative root.", X)));
    v.push_back(branched(fn(L, "cbrt", 1, 1, "cbrt(x)", "\xE2\x88\x9B", "Cube root; real for negative x.", X)));
    v.push_back(branched(fn(L, "nthroot", 2, 2, "nthroot(x, n)", "\xE2\x81\xBF\xE2\x88\x9A", "The nth root of x; odd roots of negative numbers are real.", { { "x", "the number" }, { "n", "which root" } })));
    v.push_back(branched((fn(L, "lambertw", 1, 2, "lambertw(x), lambertw(x, k)", "W", "The Lambert W function, the inverse of w e^w. Branch 0 is W\xE2\x82\x80 (x \xE2\x89\xA5 -1/e); branch -1 is W\xE2\x82\x8B\xE2\x82\x81 (-1/e \xE2\x89\xA4 x < 0). With two inputs, k picks the branch.", { { "x", "any number at least -1/e (any complex number in complex graphs)" }, { "k", "the branch, an integer (optional)" } }, { "LambertW", "lambert_w", "productlog", "ProductLog" }))));
    v.push_back(real_only(fn(T, "invsinc", 1, 1, "invsinc(x)", "InverseSinc", "The inverse of sinc on its main lobe: the y in [0, 4.4934] with sin y / y = x, for -0.2172 \xE2\x89\xA4 x \xE2\x89\xA4 1.", X, { "InverseSinc", "inverse_sinc", "inversesinc", "arcsinc" })));
    v.push_back(real_only(fn(T, "nsinc", 1, 1, "nsinc(x)", "sinc\xE2\x82\x99", "The normalized sinc sin(\xCF\x80x)/(\xCF\x80x), which is 0 at every nonzero whole number.", X, { "normalizedsinc", "normalized_sinc" })));

    const std::string R = "Rounding and parts";
    v.push_back(fn(R, "abs", 1, 1, "abs(x)", "|x|", "Absolute value. |x| works too.", X));
    v.push_back(fn(R, "sign", 1, 1, "sign(x)", "sgn", "-1, 0 or 1 by the sign of x.", X, { "sgn", "signum" }));
    v.push_back(real_only(fn(R, "floor", 1, 1, "floor(x)", "\xE2\x8C\x8Ax\xE2\x8C\x8B", "The largest integer \xE2\x89\xA4 x.", X)));
    v.push_back(real_only(fn(R, "ceil", 1, 1, "ceil(x)", "\xE2\x8C\x88x\xE2\x8C\x89", "The smallest integer \xE2\x89\xA5 x.", X)));
    v.push_back(real_only(fn(R, "round", 1, 1, "round(x)", "round", "The nearest integer.", X)));
    v.push_back(real_only(fn(R, "mod", 2, 2, "mod(a, b)", "mod", "The remainder of a / b, with the sign of b: mod(-1, 3) = 2.", { { "a", "the number" }, { "b", "the divisor" } })));
    v.push_back(real_only(fn(R, "min", 2, 8, "min(a, b, ...)", "min", "The smallest of its inputs.", { { "a, b, ...", "two to eight numbers" } })));
    v.push_back(real_only(fn(R, "max", 2, 8, "max(a, b, ...)", "max", "The largest of its inputs.", { { "a, b, ...", "two to eight numbers" } })));
    v.push_back(fn(R, "hypot", 2, 2, "hypot(a, b)", "hypot", "\xE2\x88\x9A(a\xC2\xB2 + b\xC2\xB2).", { { "a", "a number" }, { "b", "a number" } }));

    const std::string C = "Complex numbers";
    v.push_back(complex_only(fn(C, "re", 1, 1, "re(z)", "Re", "The real part.", Z, { "Re", "real" })));
    v.push_back(complex_only(fn(C, "im", 1, 1, "im(z)", "Im", "The imaginary part.", Z, { "Im", "imag" })));
    v.push_back(complex_only(fn(C, "arg", 1, 1, "arg(z)", "arg", "The argument (angle), from -\xCF\x80 to \xCF\x80.", Z, { "angle" })));
    v.push_back(complex_only(fn(C, "conj", 1, 1, "conj(z)", "z\xCC\x84", "The complex conjugate.", Z)));
    v.push_back(fn(C, "mag", 1, 1, "mag(z)", "|z|", "The magnitude (modulus) \xE2\x88\x9A(re\xC2\xB2 + im\xC2\xB2); |x| for real x.", Z, { "magnitude", "modulus" }));
    v.push_back(fn(C, "mag2", 1, 1, "mag2(z)", "|z|\xC2\xB2", "The squared magnitude re\xC2\xB2 + im\xC2\xB2, without a square root.", Z, { "norm2", "abs2" }));

    const std::string G = "Gamma and related";
    v.push_back(fn(G, "gamma", 1, 1, "gamma(x)", "\xCE\x93", "The gamma function, \xCE\x93(n) = (n - 1)! for whole n. x! is \xCE\x93(x + 1).", X, { "Gamma" }));
    v.push_back(fn(G, "lgamma", 1, 1, "lgamma(x)", "ln \xCE\x93", "ln \xCE\x93(x), where \xCE\x93(x) > 0 (for complex z, the principal ln \xCE\x93(z)); doesn't overflow. For ln |\xCE\x93(x)| use lagamma.", X, { "loggamma", "lngamma" }));
    v.push_back(fn(G, "lagamma", 1, 1, "lagamma(x)", "ln|\xCE\x93|", "ln |\xCE\x93(x)|, real for every x that isn't 0, -1, -2, \xE2\x80\xA6 (lgamma is undefined where \xCE\x93 < 0 in real graphs).", X, { "logabsgamma", "lnabsgamma" }));
    v.push_back(fn(G, "factorial", 1, 1, "factorial(x)", "x!", "x! = \xCE\x93(x + 1).", X, { "fact" }));
    v.push_back(paren(fn(G, "digamma", 1, 1, "digamma(x)", "\xCF\x88", "The digamma function \xCF\x88 = \xCE\x93\xE2\x80\xB2/\xCE\x93.", X, { "Digamma" })));
    v.push_back(real_only(fn(G, "trigamma", 1, 1, "trigamma(x)", "\xCF\x88\xE2\x82\x81", "The trigamma function \xCF\x88\xE2\x80\xB2.", X)));
    v.push_back(real_only(fn(G, "polygamma", 2, 2, "polygamma(n, x)", "\xCF\x88\xE2\x81\xBD\xE2\x81\xBF\xE2\x81\xBE", "The nth derivative of \xCF\x88.", { { "n", "which derivative, a whole number \xE2\x89\xA5 0" }, { "x", "the number" } })));
    v.push_back(paren(fn(G, "beta", 2, 2, "beta(a, b)", "B", "The beta function \xCE\x93(a)\xCE\x93(b)/\xCE\x93(a + b).", { { "a", "a number" }, { "b", "a number" } }, { "Beta" })));
    v.push_back(fn(G, "binomial", 2, 2, "binomial(n, k)", "(n k)", "The binomial coefficient n!/(k!(n - k)!), for any real n and k.", { { "n", "the top" }, { "k", "the bottom" } }, { "choose", "nCr" }));
    v.push_back(real_only(fn(G, "pochhammer", 2, 2, "pochhammer(x, n)", "(x)\xE2\x82\x99", "The rising factorial x(x + 1)\xE2\x8B\xAF(x + n - 1) = \xCE\x93(x + n)/\xCE\x93(x).", { { "x", "the start" }, { "n", "how many factors" } }, { "rising" })));
    v.push_back(fn(G, "harmonic", 1, 1, "harmonic(x)", "H\xE2\x82\x93", "Harmonic numbers H\xE2\x82\x99 = 1 + 1/2 + \xE2\x8B\xAF + 1/n, extended to every x as \xCF\x88(x + 1) + \xCE\xB3.", X));
    v.push_back(real_only(fn(G, "falling", 2, 2, "falling(x, n)", "x\xE2\x81\xBD\xE2\x81\xBF\xE2\x81\xBE", "The falling factorial x(x - 1)\xE2\x8B\xAF(x - n + 1) = \xCE\x93(x + 1)/\xCE\x93(x - n + 1).", { { "x", "the start" }, { "n", "how many factors" } }, { "fallingfactorial" })));
    v.push_back(real_only(fn(G, "lbeta", 2, 2, "lbeta(a, b)", "ln|B|", "ln |B(a, b)|, without overflow.", { { "a", "a number" }, { "b", "a number" } }, { "logbeta" })));
    v.push_back(real_only(fn(G, "betainc", 3, 3, "betainc(x, a, b)", "B\xE2\x82\x93", "The incomplete beta function \xE2\x88\xAB\xE2\x82\x80\xCB\xA3 t\xE1\xB5\x83\xE2\x81\xBB\xC2\xB9(1 - t)\xE1\xB5\x87\xE2\x81\xBB\xC2\xB9 dt.", { { "x", "the upper limit, 0 to 1" }, { "a", "a shape" }, { "b", "a shape" } }, { "incompletebeta" })));
    v.push_back(real_only(fn(G, "betareg", 3, 3, "betareg(x, a, b)", "I\xE2\x82\x93", "The regularized incomplete beta function B\xE2\x82\x93(a, b)/B(a, b).", { { "x", "the upper limit, 0 to 1" }, { "a", "a shape" }, { "b", "a shape" } }, { "regularizedbeta" })));
    v.push_back(real_only(fn(G, "betaregc", 3, 3, "betaregc(x, a, b)", "1 - I\xE2\x82\x93", "1 - I\xE2\x82\x93(a, b), accurate when it's tiny.", { { "x", "the upper limit, 0 to 1" }, { "a", "a shape" }, { "b", "a shape" } })));
    v.push_back(real_only(fn(G, "invbetareg", 3, 3, "invbetareg(p, a, b)", "I\xE2\x81\xBB\xC2\xB9", "The x with I\xE2\x82\x93(a, b) = p.", { { "p", "a probability, 0 to 1" }, { "a", "a shape" }, { "b", "a shape" } })));
    v.push_back(real_only(fn(G, "invgammaP", 2, 2, "invgammaP(a, p)", "P\xE2\x81\xBB\xC2\xB9", "The x with gammaP(a, x) = p.", { { "a", "the shape" }, { "p", "a probability, 0 to 1" } })));
    v.push_back(real_only(fn(G, "invgammaQ", 2, 2, "invgammaQ(a, q)", "Q\xE2\x81\xBB\xC2\xB9", "The x with gammaQ(a, x) = q.", { { "a", "the shape" }, { "q", "a probability, 0 to 1" } })));
    v.push_back(real_only(fn(G, "gammaP", 2, 2, "gammaP(a, x)", "P", "The regularized lower incomplete gamma function \xCE\xB3(a, x)/\xCE\x93(a).", { { "a", "the shape" }, { "x", "where to stop" } }, { "gammap" })));
    v.push_back(real_only(fn(G, "gammaQ", 2, 2, "gammaQ(a, x)", "Q", "The regularized upper incomplete gamma function \xCE\x93(a, x)/\xCE\x93(a) = 1 - P(a, x).", { { "a", "the shape" }, { "x", "where to start" } }, { "gammaq" })));
    v.push_back(real_only(fn(G, "lowergamma", 2, 2, "lowergamma(a, x)", "\xCE\xB3", "The lower incomplete gamma function \xE2\x88\xAB\xE2\x82\x80\xCB\xA3 t\xE1\xB5\x83\xE2\x81\xBB\xC2\xB9 e\xE2\x81\xBB\xE1\xB5\x97 dt.", { { "a", "the shape" }, { "x", "the upper limit" } })));
    v.push_back(real_only(fn(G, "uppergamma", 2, 2, "uppergamma(a, x)", "\xCE\x93(a, x)", "The upper incomplete gamma function \xE2\x88\xAB\xE2\x82\x93\xE2\x88\x9E t\xE1\xB5\x83\xE2\x81\xBB\xC2\xB9 e\xE2\x81\xBB\xE1\xB5\x97 dt.", { { "a", "the shape" }, { "x", "the lower limit" } })));

    const std::string Zt = "Zeta and polylogarithms";
    v.push_back(paren(fn(Zt, "zeta", 1, 2, "zeta(s), zeta(s, a)", "\xCE\xB6", "The Riemann zeta function, 1 + 1/2\xCB\xA2 + 1/3\xCB\xA2 + \xE2\x8B\xAF continued to every s \xE2\x89\xA0 1 (complex too); with two inputs, the Hurwitz zeta \xCE\xA3 1/(n + a)\xCB\xA2.", { { "s", "any number except 1" }, { "a", "the shift, for Hurwitz zeta (optional)" } }, { "Zeta", "riemannzeta" })));
    v.push_back(fn(Zt, "dirichleteta", 1, 1, "dirichleteta(s)", "\xCE\xB7", "The Dirichlet eta function 1 - 1/2\xCB\xA2 + 1/3\xCB\xA2 - \xE2\x8B\xAF = (1 - 2\xC2\xB9\xE2\x81\xBB\xCB\xA2)\xCE\xB6(s).", { { "s", "any number" } }, { "eta_function" }));
    v.push_back(real_only(fn(Zt, "dirichletbeta", 1, 1, "dirichletbeta(s)", "\xCE\xB2", "The Dirichlet beta function 1 - 1/3\xCB\xA2 + 1/5\xCB\xA2 - \xE2\x8B\xAF", { { "s", "any number" } })));
    v.push_back(real_only(fn(Zt, "riemannxi", 1, 1, "riemannxi(s)", "\xCE\xBE", "Riemann's xi function \xC2\xBD s(s - 1)\xCF\x80\xE2\x81\xBB\xCB\xA2\xE1\x90\x9F\xC2\xB2\xCE\x93(s/2)\xCE\xB6(s).", { { "s", "any number" } })));
    v.push_back(real_only(fn(Zt, "polylog", 2, 2, "polylog(s, z)", "Li\xE2\x82\x9B", "The polylogarithm Li\xE2\x82\x9B(z) = z + z\xC2\xB2/2\xCB\xA2 + z\xC2\xB3/3\xCB\xA2 + \xE2\x8B\xAF", { { "s", "the order" }, { "z", "the number" } }, { "Li" })));
    v.push_back(real_only(fn(Zt, "dilog", 1, 1, "dilog(x)", "Li\xE2\x82\x82", "The dilogarithm Li\xE2\x82\x82(x).", X, { "Li2" })));
    v.push_back(real_only(fn(Zt, "clausen", 1, 2, "clausen(\xCE\xB8), clausen(\xCE\xB8, s)", "Cl\xE2\x82\x9B", "The Clausen function. With one input, Cl\xE2\x82\x82(\xCE\xB8) = -\xE2\x88\xAB\xE2\x82\x80\xE1\xB6\xBF ln|2 sin(t/2)| dt (the Clausen integral); with two, Cl\xE2\x82\x9B(\xCE\xB8) = \xCE\xA3 sin(k\xCE\xB8)/k\xCB\xA2 for even s and \xCE\xA3 cos(k\xCE\xB8)/k\xCB\xA2 for odd s.", { { "\xCE\xB8", "the angle" }, { "s", "the order, a whole number \xE2\x89\xA5 1 (optional)" } }, { "Cl" })));
    v.push_back(real_only(fn(Zt, "clausenintegral", 1, 1, "clausenintegral(\xCE\xB8)", "Cl\xE2\x82\x82", "The Clausen integral Cl\xE2\x82\x82(\xCE\xB8) = -\xE2\x88\xAB\xE2\x82\x80\xE1\xB6\xBF ln|2 sin(t/2)| dt, the same as clausen(\xCE\xB8).", { { "\xCE\xB8", "the angle" } }, { "clausen_integral" })));
    v.push_back(real_only(fn(Zt, "trilog", 1, 1, "trilog(x)", "Li\xE2\x82\x83", "The trilogarithm Li\xE2\x82\x83(x).", X, { "Li3", "trilogarithm" })));
    v.push_back(real_only(fn(Zt, "spence", 1, 1, "spence(x)", "Sp", "Spence's function \xE2\x88\xAB\xE2\x82\x81\xCB\xA3 ln t/(1 - t) dt = Li\xE2\x82\x82(1 - x).", X, { "spencefunction" })));
    v.push_back(real_only(fn(Zt, "rogersL", 1, 1, "rogersL(x)", "L", "The Rogers dilogarithm Li\xE2\x82\x82(x) + \xC2\xBD ln x ln(1 - x).", X, { "rogersl", "rogersdilog" })));
    v.push_back(real_only(fn(Zt, "dirichletlambda", 1, 1, "dirichletlambda(s)", "\xCE\xBB", "The Dirichlet lambda function 1 + 1/3\xCB\xA2 + 1/5\xCB\xA2 + \xE2\x8B\xAF = (1 - 2\xE2\x81\xBB\xCB\xA2)\xCE\xB6(s).", { { "s", "any number except 1" } })));
    v.push_back(real_only(fn(Zt, "riemannchi", 1, 1, "riemannchi(s)", "\xCF\x87", "The factor in \xCE\xB6(s) = \xCF\x87(s)\xCE\xB6(1 - s): 2\xCB\xA2\xCF\x80\xCB\xA2\xE2\x81\xBB\xC2\xB9 sin(\xCF\x80s/2)\xCE\x93(1 - s).", { { "s", "any number" } })));
    v.push_back(real_only(fn(Zt, "legendrechi", 2, 2, "legendrechi(x, s)", "\xCF\x87\xE2\x82\x9B", "Legendre's chi function \xCE\xA3 x\xC2\xB2\xE1\xB5\x8F\xE2\x81\xBA\xC2\xB9/(2k + 1)\xCB\xA2.", { { "x", "the number, |x| \xE2\x89\xA4 1" }, { "s", "the order" } })));
    v.push_back(real_only(fn(Zt, "lerch", 3, 3, "lerch(z, s, a)", "\xCE\xA6", "The Lerch transcendent \xCE\xA6(z, s, a) = \xCE\xA3 z\xE1\xB5\x8F/(k + a)\xCB\xA2.", { { "z", "the number, |z| \xE2\x89\xA4 1" }, { "s", "the order" }, { "a", "the shift" } }, { "lerchphi" })));
    v.push_back(real_only(fn(Zt, "debye", 2, 2, "debye(x, n)", "D\xE2\x82\x99", "The Debye function (n/x\xE2\x81\xBF)\xE2\x88\xAB\xE2\x82\x80\xCB\xA3 t\xE2\x81\xBF/(e\xE1\xB5\x97 - 1) dt.", { { "x", "the number" }, { "n", "the order, a whole number \xE2\x89\xA5 1" } })));

    const std::string E = "Error functions and integrals";
    v.push_back(real_only(fn(E, "erf", 1, 1, "erf(x)", "erf", "The error function (2/\xE2\x88\x9A\xCF\x80)\xE2\x88\xAB\xE2\x82\x80\xCB\xA3 e\xE2\x81\xBB\xE1\xB5\x97\xC2\xB2 dt.", X)));
    v.push_back(real_only(fn(E, "erfc", 1, 1, "erfc(x)", "erfc", "1 - erf(x), accurate for large x.", X)));
    v.push_back(real_only(fn(E, "erfi", 1, 1, "erfi(x)", "erfi", "The imaginary error function -i erf(ix).", X)));
    v.push_back(real_only(fn(E, "inverf", 1, 1, "inverf(x)", "erf\xE2\x81\xBB\xC2\xB9", "The inverse of erf, for -1 < x < 1.", X, { "erfinv" })));
    v.push_back(real_only(fn(E, "inverfi", 1, 1, "inverfi(x)", "erfi\xE2\x81\xBB\xC2\xB9", "The inverse of erfi.", X, { "erfiinv" })));
    v.push_back(real_only(fn(E, "inverfc", 1, 1, "inverfc(x)", "erfc\xE2\x81\xBB\xC2\xB9", "The inverse of erfc, for 0 < x < 2.", X, { "erfcinv" })));
    v.push_back(paren(real_only(fn(E, "Si", 1, 1, "Si(x)", "Si", "The sine integral \xE2\x88\xAB\xE2\x82\x80\xCB\xA3 sin t / t dt.", X, { "sinint" }))));
    v.push_back(paren(real_only(fn(E, "Ci", 1, 1, "Ci(x)", "Ci", "The cosine integral -\xE2\x88\xAB\xE2\x82\x93\xE2\x88\x9E cos t / t dt.", X, { "cosint" }))));
    v.push_back(paren(real_only(fn(E, "Shi", 1, 1, "Shi(x)", "Shi", "The hyperbolic sine integral \xE2\x88\xAB\xE2\x82\x80\xCB\xA3 sinh t / t dt.", X))));
    v.push_back(paren(real_only(fn(E, "Chi", 1, 1, "Chi(x)", "Chi", "The hyperbolic cosine integral.", X))));
    v.push_back(paren(real_only(fn(E, "Ei", 1, 2, "Ei(x), Ei(x, n)", "Ei", "The exponential integral Ei(x) = -\xE2\x88\xAB\xE2\x82\x8B\xE2\x82\x93\xE2\x88\x9E e\xE2\x81\xBB\xE1\xB5\x97/t dt; with two inputs, the generalized exponential integral E\xE2\x82\x99(x) = \xE2\x88\xAB\xE2\x82\x81\xE2\x88\x9E e\xE2\x81\xBB\xCB\xA3\xE1\xB5\x97/t\xE2\x81\xBF dt.", { { "x", "the number" }, { "n", "the order, a whole number \xE2\x89\xA5 0 (optional)" } }, { "expint" }))));
    v.push_back(paren(real_only(fn(E, "li", 1, 2, "li(x), li(x, n)", "li", "The logarithmic integral li(x) = \xE2\x88\xAB\xE2\x82\x80\xCB\xA3 dt / ln t; with two inputs, the generalized li\xE2\x82\x99(x) = (ln x)\xE2\x81\xBF\xE2\x81\xBB\xC2\xB9 E\xE2\x82\x99(-ln x) (li\xE2\x82\x81 = li).", { { "x", "the number" }, { "n", "the order, a whole number \xE2\x89\xA5 1 (optional)" } }, { "logint" }))));
    v.push_back(real_only(fn(E, "fresnelS", 1, 1, "fresnelS(x)", "S", "The Fresnel integral \xE2\x88\xAB\xE2\x82\x80\xCB\xA3 sin(\xCF\x80t\xC2\xB2/2) dt.", X, { "fresnels" })));
    v.push_back(real_only(fn(E, "fresnelC", 1, 1, "fresnelC(x)", "C", "The Fresnel integral \xE2\x88\xAB\xE2\x82\x80\xCB\xA3 cos(\xCF\x80t\xC2\xB2/2) dt.", X, { "fresnelc" })));

    const std::string A = "Airy functions";
    v.push_back(paren(real_only(fn(A, "Ai", 1, 1, "Ai(x)", "Ai", "The Airy function Ai, a solution of y\xE2\x80\xB3 = xy that dies away for x > 0.", X, { "airyai" }))));
    v.push_back(paren(real_only(fn(A, "Bi", 1, 1, "Bi(x)", "Bi", "The Airy function Bi, the other solution of y\xE2\x80\xB3 = xy.", X, { "airybi" }))));
    v.push_back(paren(real_only(fn(A, "Hi", 1, 1, "Hi(x)", "Hi", "The Scorer function Hi, a solution of y\xE2\x80\xB3 - xy = 1/\xCF\x80.", X, { "scorerhi" }))));
    v.push_back(paren(real_only(fn(A, "Gi", 1, 1, "Gi(x)", "Gi", "The Scorer function Gi, a solution of y\xE2\x80\xB3 - xy = -1/\xCF\x80.", X, { "scorergi" }))));

    const std::string El = "Elliptic functions";
    v.push_back(real_only(fn(El, "ellipticK", 1, 1, "ellipticK(m)", "K", "The complete elliptic integral of the first kind (parameter m = k\xC2\xB2).", { { "m", "the parameter, m < 1" } }, { "elliptick" })));
    v.push_back(real_only(fn(El, "ellipticE", 1, 2, "ellipticE(m), ellipticE(\xCF\x86, m)", "E", "The elliptic integral of the second kind: complete with one input, incomplete with two.", { { "\xCF\x86", "the amplitude (with two inputs)" }, { "m", "the parameter" } }, { "elliptice" })));
    v.push_back(real_only(fn(El, "ellipticF", 2, 2, "ellipticF(\xCF\x86, m)", "F", "The incomplete elliptic integral of the first kind.", { { "\xCF\x86", "the amplitude" }, { "m", "the parameter" } }, { "ellipticf" })));
    v.push_back(real_only(fn(El, "ellipticPi", 2, 3, "ellipticPi(n, m), ellipticPi(\xCF\x86, n, m)", "\xCE\xA0", "The elliptic integral of the third kind: complete with two inputs, incomplete with three.", { { "\xCF\x86", "the amplitude (with three inputs)" }, { "n", "the characteristic" }, { "m", "the parameter" } }, { "ellipticpi" })));
    v.push_back(real_only(fn(El, "carlsonRF", 3, 3, "carlsonRF(x, y, z)", "R\xE2\x82\x93", "Carlson's symmetric integral R_F, \xC2\xBD\xE2\x88\xAB\xE2\x82\x80\xE2\x88\x9E dt/\xE2\x88\x9A((t + x)(t + y)(t + z)).", { { "x", "\xE2\x89\xA5 0" }, { "y", "\xE2\x89\xA5 0" }, { "z", "\xE2\x89\xA5 0" } }, { "carlsonrf" })));
    v.push_back(real_only(fn(El, "carlsonRD", 3, 3, "carlsonRD(x, y, z)", "R_D", "Carlson's integral R_D, the degenerate case of R_J with p = z.", { { "x", "\xE2\x89\xA5 0" }, { "y", "\xE2\x89\xA5 0" }, { "z", "> 0" } }, { "carlsonrd" })));
    v.push_back(real_only(fn(El, "carlsonRJ", 4, 4, "carlsonRJ(x, y, z, p)", "R_J", "Carlson's symmetric integral of the third kind R_J.", { { "x", "\xE2\x89\xA5 0" }, { "y", "\xE2\x89\xA5 0" }, { "z", "\xE2\x89\xA5 0" }, { "p", "\xE2\x89\xA0 0" } }, { "carlsonrj" })));
    v.push_back(real_only(fn(El, "carlsonRC", 2, 2, "carlsonRC(x, y)", "R_C", "Carlson's degenerate integral R_C(x, y) = R_F(x, y, y).", { { "x", "\xE2\x89\xA5 0" }, { "y", "\xE2\x89\xA0 0" } }, { "carlsonrc" })));
    v.push_back(real_only(fn(El, "carlsonRG", 3, 3, "carlsonRG(x, y, z)", "R_G", "Carlson's symmetric integral of the second kind R_G.", { { "x", "\xE2\x89\xA5 0" }, { "y", "\xE2\x89\xA5 0" }, { "z", "\xE2\x89\xA5 0" } }, { "carlsonrg" })));
    v.push_back(paren(real_only(fn(El, "sn", 2, 2, "sn(u, m)", "sn", "The Jacobi elliptic function sn.", { { "u", "the argument" }, { "m", "the parameter" } }, { "jacobisn" }))));
    v.push_back(paren(real_only(fn(El, "cn", 2, 2, "cn(u, m)", "cn", "The Jacobi elliptic function cn.", { { "u", "the argument" }, { "m", "the parameter" } }, { "jacobicn" }))));
    v.push_back(paren(real_only(fn(El, "dn", 2, 2, "dn(u, m)", "dn", "The Jacobi elliptic function dn.", { { "u", "the argument" }, { "m", "the parameter" } }, { "jacobidn" }))));
    v.push_back(paren(real_only(fn(El, "am", 2, 2, "am(u, m)", "am", "The Jacobi amplitude.", { { "u", "the argument" }, { "m", "the parameter" } }, { "jacobiam" }))));
    {
        const char* const more[][3] = {
            { "cd", "cn/dn", "jacobicd" }, { "cs", "cn/sn", "jacobics" }, { "dc", "dn/cn", "jacobidc" }, { "ds", "dn/sn", "jacobids" },
            { "nc", "1/cn", "jacobinc" }, { "nd", "1/dn", "jacobind" }, { "ns", "1/sn", "jacobins" }, { "sc", "sn/cn", "jacobisc" }, { "sd", "sn/dn", "jacobisd" }
        };
        for (const auto& j : more) {
            v.push_back(paren(real_only(fn(El, j[0], 2, 2, std::string(j[0]) + "(u, m)", j[0], std::string("The Jacobi elliptic function ") + j[0] + " = " + j[1] + ".", { { "u", "the argument" }, { "m", "the parameter" } }, { j[2] }))));
        }
    }

    const std::string Nm = "Number sequences";
    const Args N1 = { { "n", "the index, a whole number \xE2\x89\xA5 0" } };
    v.push_back(real_only(fn(Nm, "bernoulli", 1, 1, "bernoulli(n)", "B\xE2\x82\x99", "The Bernoulli numbers B\xE2\x82\x80 = 1, B\xE2\x82\x81 = -1/2, B\xE2\x82\x82 = 1/6, \xE2\x80\xA6 (0 for odd n > 1).", N1, { "bernoullinumber" })));
    v.push_back(real_only(fn(Nm, "eulernumber", 1, 1, "eulernumber(n)", "E\xE2\x82\x99", "The Euler numbers E\xE2\x82\x80 = 1, E\xE2\x82\x82 = -1, E\xE2\x82\x84 = 5, \xE2\x80\xA6 (0 for odd n), from sech x = \xCE\xA3 E\xE2\x82\x99x\xE2\x81\xBF/n!.", N1, { "euler_number" })));
    v.push_back(real_only(fn(Nm, "tangentnumber", 1, 1, "tangentnumber(n)", "T\xE2\x82\x99", "The tangent (zag) numbers, from tan x = \xCE\xA3 T\xE2\x82\x99x\xE2\x81\xBF/n!.", N1, { "zag" })));
    v.push_back(real_only(fn(Nm, "secantnumber", 1, 1, "secantnumber(n)", "S\xE2\x82\x99", "The secant (zig) numbers, from sec x = \xCE\xA3 S\xE2\x82\x99x\xE2\x81\xBF/n!.", N1, { "zig" })));
    v.push_back(real_only(fn(Nm, "eulerian", 2, 2, "eulerian(n, k)", "A(n, k)", "The Eulerian numbers: how many orderings of 1, \xE2\x80\xA6, n have exactly k ascents.", { { "n", "a whole number \xE2\x89\xA5 0" }, { "k", "a whole number \xE2\x89\xA5 0" } }, { "euleriannumber" })));
    v.push_back(real_only(fn(Nm, "stieltjes", 1, 1, "stieltjes(n)", "\xCE\xB3\xE2\x82\x99", "The Stieltjes constants, the Laurent coefficients of \xCE\xB6(s) at s = 1 (\xCE\xB3\xE2\x82\x80 is Euler's \xCE\xB3).", N1, { "stieltjesgamma" })));
    v.push_back(real_only(fn(Nm, "metallic", 1, 1, "metallic(n)", "\xCF\x83\xE2\x82\x99", "The metallic ratios (n + \xE2\x88\x9A(n\xC2\xB2 + 4))/2: 1 is golden, 2 silver, 3 bronze.", N1, { "metallicratio" })));
    v.push_back(real_only(fn(Nm, "invmetallic", 1, 1, "invmetallic(n)", "1/\xCF\x83\xE2\x82\x99", "1 over the nth metallic ratio, (\xE2\x88\x9A(n\xC2\xB2 + 4) - n)/2.", N1, { "invmetallicratio" })));
    v.push_back(real_only(fn(Nm, "metallicmean", 1, 1, "metallicmean(n)", "M\xE2\x82\x99", "The metallic means (1 + \xE2\x88\x9A(4n + 1))/2, the positive root of x\xC2\xB2 = x + n.", N1)));
    v.push_back(real_only(fn(Nm, "beraha", 1, 1, "beraha(n)", "Ber\xE2\x82\x99", "The Beraha constants 4cos\xC2\xB2(\xCF\x80/n) = 2 + 2cos(2\xCF\x80/n).", { { "n", "a whole number \xE2\x89\xA5 1" } })));
    v.push_back(real_only(fn(Nm, "fibonacci", 1, 2, "fibonacci(x), fibonacci(x, n)", "F\xE2\x82\x93", "The Fibonacci numbers F\xE2\x82\x93 = (\xCF\x86\xCB\xA3 - cos(\xCF\x80x)\xCF\x86\xE2\x81\xBB\xCB\xA3)/\xE2\x88\x9A" "5 for any real x; with two inputs, the Fibonacci polynomial F\xE2\x82\x99(x).", { { "x", "the index (or the variable, with two inputs)" }, { "n", "the polynomial's index (optional)" } }, { "fib" })));
    v.push_back(real_only(fn(Nm, "lucas", 1, 2, "lucas(x), lucas(x, n)", "L\xE2\x82\x93", "The Lucas numbers L\xE2\x82\x93 = \xCF\x86\xCB\xA3 + cos(\xCF\x80x)\xCF\x86\xE2\x81\xBB\xCB\xA3 for any real x; with two inputs, the Lucas polynomial L\xE2\x82\x99(x).", { { "x", "the index (or the variable, with two inputs)" }, { "n", "the polynomial's index (optional)" } })));

    const std::string Po = "Orthogonal polynomials";
    const Args XN = { { "x", "the variable" }, { "n", "the degree, a whole number \xE2\x89\xA5 0" } };
    v.push_back(real_only(fn(Po, "chebyshevT", 2, 2, "chebyshevT(x, n)", "T\xE2\x82\x99", "Chebyshev polynomials of the first kind, T\xE2\x82\x99(cos \xCE\xB8) = cos n\xCE\xB8.", XN, { "chebyshevt" })));
    v.push_back(real_only(fn(Po, "chebyshevU", 2, 2, "chebyshevU(x, n)", "U\xE2\x82\x99", "Chebyshev polynomials of the second kind, U\xE2\x82\x99(cos \xCE\xB8) = sin((n + 1)\xCE\xB8)/sin \xCE\xB8.", XN, { "chebyshevu" })));
    v.push_back(real_only(fn(Po, "chebyshevV", 2, 2, "chebyshevV(x, n)", "V\xE2\x82\x99", "Chebyshev polynomials of the third kind.", XN, { "chebyshevv" })));
    v.push_back(real_only(fn(Po, "chebyshevW", 2, 2, "chebyshevW(x, n)", "W\xE2\x82\x99", "Chebyshev polynomials of the fourth kind.", XN, { "chebyshevw" })));
    v.push_back(real_only(fn(Po, "legendreP", 2, 2, "legendreP(x, n)", "P\xE2\x82\x99", "Legendre polynomials.", XN, { "legendrep" })));
    v.push_back(real_only(fn(Po, "legendreQ", 2, 2, "legendreQ(x, n)", "Q\xE2\x82\x99", "Legendre functions of the second kind, for |x| < 1.", XN, { "legendreq" })));

    const std::string V = "Variables";
    v.push_back(variable(V, "x", "x", "The input of y = f(x), the horizontal axis; in 3D, the first coordinate."));
    v.push_back(variable(V, "y", "y", "The vertical axis in 2D; the second coordinate in 3D."));
    v.push_back(variable(V, "z", "z", "The height in 3D, or the complex input of complex graphs."));
    v.push_back(variable(V, "t", "t", "The parameter of parametric curves and complex-valued curves."));
    v.push_back(variable(V, "u, v", "u, v", "The parameters of parametric surfaces and changes of variables."));
    v.push_back(variable(V, "w", "w", "The third variable of 3D changes of variables, and the input of Riemann surfaces."));
    v.push_back(variable(V, "r", "r", "The distance from the origin in polar and cylindrical graphs."));
    v.push_back(variable(V, "theta", "\xCE\xB8", "The angle in polar, cylindrical and spherical graphs."));
    v.push_back(variable(V, "phi", "\xCF\x86", "The angle from the top in spherical graphs."));
    v.push_back(variable(V, "rho", "\xCF\x81", "The distance from the origin in spherical graphs."));
    v.push_back(variable(V, "n, k", "n, k", "The step in the Misc sequences."));

    const std::string Gr = "Greek letters";
    const char* const greek[][2] = {
        { "alpha", "\xCE\xB1" }, { "beta", "\xCE\xB2" }, { "delta", "\xCE\xB4" }, { "epsilon", "\xCE\xB5" }, { "zeta", "\xCE\xB6" }, { "eta", "\xCE\xB7" },
        { "kappa", "\xCE\xBA" }, { "lambda", "\xCE\xBB" }, { "mu", "\xCE\xBC" }, { "nu", "\xCE\xBD" }, { "xi", "\xCE\xBE" }, { "sigma", "\xCF\x83" },
        { "chi", "\xCF\x87" }, { "omega", "\xCF\x89" }
    };
    for (const auto& g : greek) v.push_back(variable(Gr, g[0], g[1], std::string("A variable named ") + g[1] + ". Type the name (or paste the letter); give it a value with a Definition or a slider."));
    return v;
}

} // namespace

const std::vector<LibEntry>& library() {
    static const std::vector<LibEntry> all = build();
    return all;
}

const LibEntry* library_function(const std::string& name) {
    for (const LibEntry& e : library()) {
        if (e.kind != LibKind::Function) continue;
        if (e.name == name) return &e;
        for (const std::string& a : e.aliases) {
            if (a == name) return &e;
        }
    }
    return nullptr;
}

const LibEntry* library_constant(const std::string& name) {
    for (const LibEntry& e : library()) {
        if (e.kind != LibKind::Constant) continue;
        if (e.name == name) return &e;
        for (const std::string& a : e.aliases) {
            if (a == name) return &e;
        }
    }
    return nullptr;
}

const std::vector<std::string>& library_categories() {
    static const std::vector<std::string> cats = [] {
        std::vector<std::string> out;
        for (const LibEntry& e : library()) {
            if (std::find(out.begin(), out.end(), e.category) == out.end()) out.push_back(e.category);
        }
        return out;
    }();
    return cats;
}

std::string library_text() {
    auto clean = [](std::string s) {
        for (char& c : s) {
            if (c == '\t' || c == '\n') c = ' ';
        }
        return s;
    };
    std::string out;
    for (const LibEntry& e : library()) {
        const char* kind = e.kind == LibKind::Function ? "function" : e.kind == LibKind::Constant ? "constant" : e.kind == LibKind::Variable ? "variable" : "operator";
        std::string aliases;
        for (const std::string& a : e.aliases) aliases += (aliases.empty() ? "" : ", ") + a;
        std::string args;
        for (const auto& [n, d] : e.args) args += (args.empty() ? "" : "\x1f") + n + ": " + d;
        std::string notes;
        if (e.kind == LibKind::Function) {
            if (e.complex_only) notes = "complex graphs";
            else if (!e.complex) notes = "real numbers";
            else notes = "real and complex";
            if (e.branched) notes += ", has branches";
        }
        if (e.kind == LibKind::Constant && !e.value.empty()) notes = e.value + (e.literal == "@i" || e.literal == "@inf" ? "" : "\xE2\x80\xA6");
        out += std::string(kind) + "\t" + clean(e.name) + "\t" + clean(e.usage) + "\t" + clean(e.symbol) + "\t" + clean(e.category) + "\t" + clean(e.description) + "\t" + clean(aliases) + "\t" + clean(args) + "\t" + clean(notes) + "\n";
    }
    return out;
}

} // namespace arbor
