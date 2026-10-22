#include "arbor/expr.hpp"
#include "arbor/library.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <sstream>

namespace arbor {

namespace {

thread_local bool t_as_typed = false;

ExprPtr make(Op op, std::vector<ExprPtr> args = {}, std::string text = {}, double value = 0.0) {
    auto e = std::make_shared<Expr>();
    e->op = op;
    e->args = std::move(args);
    e->text = std::move(text);
    e->value = value;
    return e;
}

bool same(const ExprPtr& a, const ExprPtr& b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->op != b->op) return false;
    if (a->op == Op::Number) return a->value == b->value;
    if (a->text != b->text || a->args.size() != b->args.size()) return false;
    for (std::size_t i = 0; i < a->args.size(); ++i) {
        if (!same(a->args[i], b->args[i])) return false;
    }
    return true;
}

bool num_value(const ExprPtr& e, double& v) {
    if (e && e->op == Op::Number) {
        v = e->value;
        return true;
    }
    return false;
}

} // namespace

ExprPtr number(double v) {
    return make(Op::Number, {}, {}, v);
}

ExprPtr number_text(const std::string& literal) {
    return make(Op::Number, {}, literal, std::strtod(literal.c_str(), nullptr));
}

ExprPtr variable(const std::string& name) {
    return make(Op::Variable, {}, name);
}

ExprPtr imag_unit() {
    return make(Op::Imag);
}

bool is_number(const ExprPtr& e, double v) {
    return e && e->op == Op::Number && e->value == v;
}

ExprPtr add(ExprPtr a, ExprPtr b) {
    if (t_as_typed) return make(Op::Add, { std::move(a), std::move(b) });
    double x = 0.0;
    double y = 0.0;
    if (num_value(a, x) && num_value(b, y)) return number(x + y);
    if (is_number(a, 0.0)) return b;
    if (is_number(b, 0.0)) return a;
    if (b->op == Op::Neg) return sub(a, b->args[0]);
    if (num_value(b, y) && y < 0.0) return sub(a, number(-y));
    return make(Op::Add, { std::move(a), std::move(b) });
}

ExprPtr sub(ExprPtr a, ExprPtr b) {
    if (t_as_typed) return make(Op::Sub, { std::move(a), std::move(b) });
    double x = 0.0;
    double y = 0.0;
    if (num_value(a, x) && num_value(b, y)) return number(x - y);
    if (is_number(b, 0.0)) return a;
    if (is_number(a, 0.0)) return neg(b);
    if (same(a, b)) return number(0.0);
    if (b->op == Op::Neg) return add(a, b->args[0]);
    return make(Op::Sub, { std::move(a), std::move(b) });
}

ExprPtr mul(ExprPtr a, ExprPtr b) {
    if (t_as_typed) return make(Op::Mul, { std::move(a), std::move(b) });
    double x = 0.0;
    double y = 0.0;
    if (num_value(a, x) && num_value(b, y)) return number(x * y);
    if (is_number(a, 0.0) || is_number(b, 0.0)) return number(0.0);
    if (is_number(a, 1.0)) return b;
    if (is_number(b, 1.0)) return a;
    if (is_number(a, -1.0)) return neg(b);
    if (is_number(b, -1.0)) return neg(a);
    if (a->op == Op::Neg && b->op == Op::Neg) return mul(a->args[0], b->args[0]);
    if (a->op == Op::Neg) return neg(mul(a->args[0], b));
    if (b->op == Op::Neg) return neg(mul(a, b->args[0]));
    if (num_value(b, y) && !num_value(a, x)) return mul(b, a);
    if (num_value(a, x) && b->op == Op::Mul && num_value(b->args[0], y)) return mul(number(x * y), b->args[1]);
    if (same(a, b)) return pow(a, number(2.0));
    return make(Op::Mul, { std::move(a), std::move(b) });
}

ExprPtr div(ExprPtr a, ExprPtr b) {
    if (t_as_typed) return make(Op::Div, { std::move(a), std::move(b) });
    double x = 0.0;
    double y = 0.0;
    if (num_value(a, x) && num_value(b, y) && y != 0.0) return number(x / y);
    if (is_number(b, 1.0)) return a;
    if (is_number(a, 0.0) && !is_number(b, 0.0)) return number(0.0);
    if (same(a, b) && !is_number(a, 0.0)) return number(1.0);
    if (a->op == Op::Neg && b->op == Op::Neg) return div(a->args[0], b->args[0]);
    if (a->op == Op::Neg) return neg(div(a->args[0], b));
    return make(Op::Div, { std::move(a), std::move(b) });
}

ExprPtr pow(ExprPtr a, ExprPtr b) {
    if (t_as_typed) return make(Op::Pow, { std::move(a), std::move(b) });
    double x = 0.0;
    double y = 0.0;
    if (num_value(a, x) && num_value(b, y)) {
        const double r = std::pow(x, y);
        if (std::isfinite(r) && (x >= 0.0 || std::floor(y) == y)) return number(r);
    }
    if (is_number(b, 1.0)) return a;
    if (is_number(b, 0.0)) return number(1.0);
    if (is_number(a, 1.0)) return number(1.0);
    if (a->op == Op::Variable && a->text == "e") return call("exp", { b });
    if (a->op == Op::Pow && num_value(a->args[1], x) && num_value(b, y) && std::floor(y) == y) return pow(a->args[0], number(x * y));
    return make(Op::Pow, { std::move(a), std::move(b) });
}

ExprPtr neg(ExprPtr a) {
    if (t_as_typed) return make(Op::Neg, { std::move(a) });
    double x = 0.0;
    if (num_value(a, x)) return number(-x);
    if (a->op == Op::Neg) return a->args[0];
    if (a->op == Op::Sub) return sub(a->args[1], a->args[0]);
    return make(Op::Neg, { std::move(a) });
}

ExprPtr vector_of(std::vector<ExprPtr> parts) {
    if (parts.size() == 1) return parts[0];
    return make(Op::Vector, std::move(parts));
}

namespace {

const std::vector<FunctionInfo>& base_function_table() {
    static const std::vector<FunctionInfo> table = {
        { "sin", 1, 1, false }, { "cos", 1, 1, false }, { "tan", 1, 1, false },
        { "sec", 1, 1, false }, { "csc", 1, 1, false }, { "cot", 1, 1, false },
        { "asin", 1, 1, false }, { "acos", 1, 1, false }, { "atan", 1, 2, false },
        { "asec", 1, 1, false }, { "acsc", 1, 1, false }, { "acot", 1, 1, false },
        { "atan2", 2, 2, false },
        { "sinh", 1, 1, false }, { "cosh", 1, 1, false }, { "tanh", 1, 1, false },
        { "sech", 1, 1, false }, { "csch", 1, 1, false }, { "coth", 1, 1, false },
        { "asinh", 1, 1, false }, { "acosh", 1, 1, false }, { "atanh", 1, 1, false },
        { "exp", 1, 1, false }, { "ln", 1, 1, false }, { "log", 1, 2, false },
        { "log2", 1, 1, false }, { "log10", 1, 1, false },
        { "sqrt", 1, 1, false }, { "cbrt", 1, 1, false }, { "nthroot", 2, 2, false },
        { "abs", 1, 1, false }, { "sign", 1, 1, false },
        { "floor", 1, 1, false }, { "ceil", 1, 1, false }, { "round", 1, 1, false },
        { "mod", 2, 2, false }, { "min", 2, 8, false }, { "max", 2, 8, false },
        { "hypot", 2, 2, false }, { "gamma", 1, 1, false }, { "erf", 1, 1, false },
        { "re", 1, 1, true }, { "im", 1, 1, true }, { "arg", 1, 1, true },
        { "conj", 1, 1, true },
        { "lt", 2, 2, false }, { "le", 2, 2, false }, { "gt", 2, 2, false }, { "ge", 2, 2, false },
        { "eq", 2, 2, false }, { "ne", 2, 2, false }, { "and", 2, 2, false }, { "or", 2, 2, false }, { "not", 1, 1, false }
    };
    return table;
}

const std::vector<FunctionInfo>& function_table() {
    static const std::vector<FunctionInfo> table = [] {
        std::vector<FunctionInfo> t = base_function_table();
        for (const LibEntry& e : library()) {
            if (e.kind != LibKind::Function) continue;
            bool known = false;
            for (const FunctionInfo& f : t) known = known || f.name == e.name;
            if (!known) t.push_back(FunctionInfo{ e.name, e.min_args, e.max_args, e.complex_only });
        }
        return t;
    }();
    return table;
}

bool needs_paren_name(const std::string& n) {
    const LibEntry* e = library_function(n);
    return e && e->needs_paren;
}

struct Alias {
    const char* from;
    const char* to;
};

const Alias k_aliases[] = {
    { "arcsin", "asin" }, { "arccos", "acos" }, { "arctan", "atan" },
    { "arcsec", "asec" }, { "arccsc", "acsc" }, { "arccot", "acot" },
    { "arsinh", "asinh" }, { "arcosh", "acosh" }, { "artanh", "atanh" },
    { "arcsinh", "asinh" }, { "arccosh", "acosh" }, { "arctanh", "atanh" },
    { "arsech", "asech" }, { "arcsch", "acsch" }, { "arcoth", "acoth" },
    { "arcsech", "asech" }, { "arccsch", "acsch" }, { "arccoth", "acoth" },
    { "Re", "re" }, { "Im", "im" }, { "real", "re" }, { "imag", "im" },
    { "sgn", "sign" }, { "signum", "sign" }, { "lg", "log10" }, { "angle", "arg" },
    { "theta", "theta" }, { "pi", "pi" }
};

const char* k_constants[] = { "pi", "e", "tau", "inf" };

} // namespace

const std::vector<FunctionInfo>& builtin_functions() {
    return function_table();
}

std::string canonical_name(const std::string& name) {
    for (const Alias& a : k_aliases) {
        if (name == a.from) return a.to;
    }
    if (const LibEntry* e = library_function(name)) return e->name;
    if (const LibEntry* e = library_constant(name)) {
        if (e->literal != "@i") return e->name;
    }
    return name;
}

const FunctionInfo* find_function(const std::string& name) {
    const std::string n = canonical_name(name);
    for (const FunctionInfo& f : function_table()) {
        if (f.name == n) return &f;
    }
    return nullptr;
}

bool is_builtin_constant(const std::string& name) {
    for (const char* c : k_constants) {
        if (name == c) return true;
    }
    const LibEntry* e = library_constant(name);
    return e && e->literal != "@i";
}

ExprPtr call(const std::string& name_in, std::vector<ExprPtr> args) {
    const std::string name = canonical_name(name_in);
    if (t_as_typed) return make(Op::Call, std::move(args), name);
    bool all = !args.empty();
    std::vector<double> v;
    for (const ExprPtr& a : args) {
        double x = 0.0;
        if (!num_value(a, x)) all = false;
        v.push_back(x);
    }
    if (all) {
        double r = std::nan("");
        if (name == "sin") r = std::sin(v[0]);
        else if (name == "cos") r = std::cos(v[0]);
        else if (name == "exp") r = std::exp(v[0]);
        else if (name == "sqrt" && v[0] >= 0.0) r = std::sqrt(v[0]);
        else if (name == "abs") r = std::fabs(v[0]);
        else if (name == "ln" && v[0] > 0.0) r = std::log(v[0]);
        if (std::isfinite(r) && (r == std::floor(r) || name == "abs")) return number(r);
    }
    if (name == "sqrt" && args.size() == 1 && args[0]->op == Op::Pow && is_number(args[0]->args[1], 2.0)) return call("abs", { args[0]->args[0] });
    if (name == "exp" && args.size() == 1 && args[0]->op == Op::Call && args[0]->text == "ln") return args[0]->args[0];
    return make(Op::Call, std::move(args), name);
}

bool uses_branches(const ExprPtr& e, bool complex) {
    if (!e) return false;
    if (e->op == Op::Call) {
        static const char* const real_branched[] = { "sqrt", "nthroot", "asin", "acos", "atan", "asec", "acsc", "acot", "acosh", "lambertw" };
        static const char* const complex_branched[] = { "sqrt", "cbrt", "nthroot", "ln", "log", "log2", "log10", "asin", "acos", "atan", "asec", "acsc", "acot", "asinh", "acosh", "atanh", "asech", "acsch", "acoth", "lambertw" };
        if (complex) {
            for (const char* n : complex_branched) {
                if (e->text == n) return true;
            }
        } else {
            for (const char* n : real_branched) {
                if (e->text == n && !(e->text == "atan" && e->args.size() == 2)) return true;
            }
        }
    }
    if (complex && e->op == Op::Pow && e->args.size() == 2) {
        const ExprPtr& p = e->args[1];
        const bool integer = p->op == Op::Number ? std::floor(p->value) == p->value : (p->op == Op::Neg && p->args[0]->op == Op::Number && std::floor(p->args[0]->value) == p->args[0]->value);
        if (!integer) return true;
    }
    for (const ExprPtr& a : e->args) {
        if (uses_branches(a, complex)) return true;
    }
    return false;
}

bool is_constant(const ExprPtr& e) {
    if (!e) return true;
    if (e->op == Op::Variable) return is_builtin_constant(e->text);
    for (const ExprPtr& a : e->args) {
        if (!is_constant(a)) return false;
    }
    return true;
}

bool depends_on(const ExprPtr& e, const std::string& name) {
    if (!e) return false;
    if (e->op == Op::Variable) return e->text == name;
    for (const ExprPtr& a : e->args) {
        if (depends_on(a, name)) return true;
    }
    return false;
}

void collect_names(const ExprPtr& e, std::set<std::string>& out) {
    if (!e) return;
    if (e->op == Op::Variable && !is_builtin_constant(e->text)) out.insert(e->text);
    for (const ExprPtr& a : e->args) collect_names(a, out);
}

bool uses_imaginary(const ExprPtr& e) {
    if (!e) return false;
    if (e->op == Op::Imag) return true;
    if (e->op == Op::Call) {
        const FunctionInfo* f = find_function(e->text);
        if (f && f->complex_only) return true;
    }
    for (const ExprPtr& a : e->args) {
        if (uses_imaginary(a)) return true;
    }
    return false;
}

ExprPtr substitute(const ExprPtr& e, const std::map<std::string, ExprPtr>& with) {
    if (!e) return e;
    if (e->op == Op::Variable) {
        auto it = with.find(e->text);
        return it != with.end() ? it->second : e;
    }
    if (e->args.empty()) return e;
    std::vector<ExprPtr> args;
    bool changed = false;
    for (const ExprPtr& a : e->args) {
        args.push_back(substitute(a, with));
        changed = changed || args.back() != a;
    }
    if (!changed) return e;
    switch (e->op) {
        case Op::Add: return add(args[0], args[1]);
        case Op::Sub: return sub(args[0], args[1]);
        case Op::Mul: return mul(args[0], args[1]);
        case Op::Div: return div(args[0], args[1]);
        case Op::Pow: return pow(args[0], args[1]);
        case Op::Neg: return neg(args[0]);
        case Op::Call: return call(e->text, std::move(args));
        case Op::Vector: return vector_of(std::move(args));
        default: return e;
    }
}

ExprPtr inline_functions(const ExprPtr& e, const FunctionTable& functions, std::string& error, int depth) {
    if (!e) return e;
    if (depth > 40) {
        error = "a function refers to itself";
        return e;
    }
    std::vector<ExprPtr> args;
    bool changed = false;
    for (const ExprPtr& a : e->args) {
        args.push_back(inline_functions(a, functions, error, depth));
        changed = changed || args.back() != a;
    }
    if (e->op == Op::Call) {
        auto it = functions.find(e->text);
        if (it != functions.end()) {
            const UserFunction& f = it->second;
            if (f.params.size() != args.size()) {
                error = e->text + " takes " + std::to_string(f.params.size()) + (f.params.size() == 1 ? " input" : " inputs");
                return e;
            }
            std::map<std::string, ExprPtr> with;
            for (std::size_t i = 0; i < args.size(); ++i) with[f.params[i]] = args[i];
            return inline_functions(substitute(f.body, with), functions, error, depth + 1);
        }
    }
    if (e->op == Op::Variable) {
        auto it = functions.find(e->text);
        if (it != functions.end() && it->second.params.empty()) return inline_functions(it->second.body, functions, error, depth + 1);
    }
    if (!changed) return e;
    std::map<std::string, ExprPtr> none;
    auto rebuilt = std::make_shared<Expr>(*e);
    rebuilt->args = std::move(args);
    return simplify(rebuilt);
}

ExprPtr simplify(const ExprPtr& e) {
    if (!e || e->args.empty()) return e;
    std::vector<ExprPtr> args;
    for (const ExprPtr& a : e->args) args.push_back(simplify(a));
    switch (e->op) {
        case Op::Add: return add(args[0], args[1]);
        case Op::Sub: return sub(args[0], args[1]);
        case Op::Mul: return mul(args[0], args[1]);
        case Op::Div: return div(args[0], args[1]);
        case Op::Pow: return pow(args[0], args[1]);
        case Op::Neg: return neg(args[0]);
        case Op::Call: return call(e->text, std::move(args));
        case Op::Vector: return vector_of(std::move(args));
        default: return e;
    }
}

std::size_t component_count(const ExprPtr& e) {
    return e && e->op == Op::Vector ? e->args.size() : 1;
}

ExprPtr component(const ExprPtr& e, std::size_t i) {
    if (e && e->op == Op::Vector) return i < e->args.size() ? e->args[i] : number(0.0);
    return i == 0 ? e : number(0.0);
}

ExprPtr derivative(const ExprPtr& e, const std::string& v) {
    if (!e) return nullptr;
    if (!depends_on(e, v)) return e->op == Op::Vector ? vector_of(std::vector<ExprPtr>(e->args.size(), number(0.0))) : number(0.0);
    switch (e->op) {
        case Op::Number:
        case Op::Imag:
            return number(0.0);
        case Op::Variable:
            return number(e->text == v ? 1.0 : 0.0);
        case Op::Add: {
            ExprPtr a = derivative(e->args[0], v);
            ExprPtr b = derivative(e->args[1], v);
            return a && b ? add(a, b) : nullptr;
        }
        case Op::Sub: {
            ExprPtr a = derivative(e->args[0], v);
            ExprPtr b = derivative(e->args[1], v);
            return a && b ? sub(a, b) : nullptr;
        }
        case Op::Neg: {
            ExprPtr a = derivative(e->args[0], v);
            return a ? neg(a) : nullptr;
        }
        case Op::Mul: {
            const ExprPtr& f = e->args[0];
            const ExprPtr& g = e->args[1];
            ExprPtr df = derivative(f, v);
            ExprPtr dg = derivative(g, v);
            if (!df || !dg) return nullptr;
            return add(mul(df, g), mul(f, dg));
        }
        case Op::Div: {
            const ExprPtr& f = e->args[0];
            const ExprPtr& g = e->args[1];
            ExprPtr df = derivative(f, v);
            ExprPtr dg = derivative(g, v);
            if (!df || !dg) return nullptr;
            if (!depends_on(g, v)) return div(df, g);
            return div(sub(mul(df, g), mul(f, dg)), pow(g, number(2.0)));
        }
        case Op::Pow: {
            const ExprPtr& f = e->args[0];
            const ExprPtr& g = e->args[1];
            ExprPtr df = derivative(f, v);
            ExprPtr dg = derivative(g, v);
            if (!df || !dg) return nullptr;
            if (!depends_on(g, v)) return mul(mul(g, pow(f, sub(g, number(1.0)))), df);
            if (!depends_on(f, v)) return mul(mul(e, call("ln", { f })), dg);
            return mul(e, add(mul(dg, call("ln", { f })), div(mul(g, df), f)));
        }
        case Op::Vector: {
            std::vector<ExprPtr> parts;
            for (const ExprPtr& a : e->args) {
                ExprPtr d = derivative(a, v);
                if (!d) return nullptr;
                parts.push_back(d);
            }
            return vector_of(std::move(parts));
        }
        case Op::Call: {
            const std::string& n = e->text;
            if (e->args.empty()) return nullptr;
            const ExprPtr& u = e->args[0];
            ExprPtr du = derivative(u, v);
            if (!du) return nullptr;
            auto chain = [&](ExprPtr outer) { return mul(outer, du); };
            const ExprPtr one = number(1.0);
            const ExprPtr two = number(2.0);
            if (n == "sin") return chain(call("cos", { u }));
            if (n == "cos") return chain(neg(call("sin", { u })));
            if (n == "tan") return chain(div(one, pow(call("cos", { u }), two)));
            if (n == "sec") return chain(mul(call("sec", { u }), call("tan", { u })));
            if (n == "csc") return chain(neg(mul(call("csc", { u }), call("cot", { u }))));
            if (n == "cot") return chain(neg(div(one, pow(call("sin", { u }), two))));
            if (n == "asin") return chain(div(one, call("sqrt", { sub(one, pow(u, two)) })));
            if (n == "acos") return chain(neg(div(one, call("sqrt", { sub(one, pow(u, two)) }))));
            if ((n == "atan" && e->args.size() == 2) || n == "atan2") {
                const ExprPtr& y = e->args[0];
                const ExprPtr& x = e->args[1];
                ExprPtr dy = du;
                ExprPtr dx = derivative(x, v);
                if (!dx) return nullptr;
                return div(sub(mul(x, dy), mul(y, dx)), add(pow(x, two), pow(y, two)));
            }
            if (n == "atan") return chain(div(one, add(one, pow(u, two))));
            if (n == "acot") return chain(neg(div(one, add(one, pow(u, two)))));
            if (n == "asec") return chain(div(one, mul(call("abs", { u }), call("sqrt", { sub(pow(u, two), one) }))));
            if (n == "acsc") return chain(neg(div(one, mul(call("abs", { u }), call("sqrt", { sub(pow(u, two), one) })))));
            if (n == "sinh") return chain(call("cosh", { u }));
            if (n == "cosh") return chain(call("sinh", { u }));
            if (n == "tanh") return chain(div(one, pow(call("cosh", { u }), two)));
            if (n == "sech") return chain(neg(mul(call("sech", { u }), call("tanh", { u }))));
            if (n == "csch") return chain(neg(mul(call("csch", { u }), call("coth", { u }))));
            if (n == "coth") return chain(neg(div(one, pow(call("sinh", { u }), two))));
            if (n == "asinh") return chain(div(one, call("sqrt", { add(pow(u, two), one) })));
            if (n == "acosh") return chain(div(one, call("sqrt", { sub(pow(u, two), one) })));
            if (n == "atanh") return chain(div(one, sub(one, pow(u, two))));
            if (n == "asech") return chain(neg(div(one, mul(u, call("sqrt", { sub(one, pow(u, two)) })))));
            if (n == "acsch") return chain(neg(div(one, mul(call("abs", { u }), call("sqrt", { add(one, pow(u, two)) })))));
            if (n == "acoth") return chain(div(one, sub(one, pow(u, two))));
            if (n == "invgd") return chain(call("sec", { u }));
            if (n == "invsinc") return chain(div(e, sub(call("cos", { e }), call("sinc", { e }))));
            if (n == "mag") return chain(call("sign", { u }));
            if (n == "mag2") return chain(mul(two, u));
            if (n == "exp") return chain(e);
            if (n == "ln") return chain(div(one, u));
            if (n == "log" && e->args.size() == 1) return chain(div(one, mul(u, call("ln", { number(10.0) }))));
            if (n == "log" && e->args.size() == 2) {
                ExprPtr q = div(call("ln", { e->args[0] }), call("ln", { e->args[1] }));
                return derivative(q, v);
            }
            if (n == "log10") return chain(div(one, mul(u, call("ln", { number(10.0) }))));
            if (n == "log2") return chain(div(one, mul(u, call("ln", { two }))));
            if (n == "sqrt") return chain(div(one, mul(two, e)));
            if (n == "cbrt") return chain(div(one, mul(number(3.0), pow(e, two))));
            if (n == "nthroot") return derivative(pow(e->args[0], div(one, e->args[1])), v);
            if (n == "abs") return chain(call("sign", { u }));
            if (n == "sign" || n == "floor" || n == "ceil" || n == "round") return number(0.0);
            if (n == "mod") {
                ExprPtr db = derivative(e->args[1], v);
                if (!db) return nullptr;
                return sub(du, mul(call("floor", { div(u, e->args[1]) }), db));
            }
            if (n == "hypot") {
                ExprPtr db = derivative(e->args[1], v);
                if (!db) return nullptr;
                return div(add(mul(u, du), mul(e->args[1], db)), e);
            }
            if ((n == "min" || n == "max") && e->args.size() == 2) {
                const ExprPtr& b = e->args[1];
                ExprPtr db = derivative(b, v);
                if (!db) return nullptr;
                ExprPtr s = call("sign", { sub(u, b) });
                ExprPtr avg = div(add(du, db), two);
                ExprPtr half = div(mul(s, sub(du, db)), two);
                return n == "max" ? add(avg, half) : sub(avg, half);
            }
            if (n == "re" || n == "im" || n == "conj") return call(n, { du });
            if (n == "arg") return call("im", { div(du, u) });
            if (n == "erf") return chain(mul(div(two, call("sqrt", { variable("pi") })), call("exp", { neg(pow(u, two)) })));
            if (n == "erfc") return chain(neg(mul(div(two, call("sqrt", { variable("pi") })), call("exp", { neg(pow(u, two)) }))));
            if (n == "erfi") return chain(mul(div(two, call("sqrt", { variable("pi") })), call("exp", { pow(u, two) })));
            if (n == "gamma") return chain(mul(e, call("digamma", { u })));
            if (n == "factorial") return chain(mul(e, call("digamma", { add(u, one) })));
            if (n == "lgamma" || n == "lagamma") return chain(call("digamma", { u }));
            if (n == "digamma") return chain(call("trigamma", { u }));
            if (n == "trigamma") return chain(call("polygamma", { two, u }));
            if (n == "harmonic") return chain(call("trigamma", { add(u, one) }));
            if (n == "lambertw" && e->args.size() == 1) return chain(div(e, mul(u, add(one, e))));
            if (n == "expm1") return chain(call("exp", { u }));
            if (n == "log1p") return chain(div(one, add(one, u)));
            if (n == "sinc") return chain(div(sub(call("cos", { u }), call("sinc", { u })), u));
            if (n == "gd") return chain(call("sech", { u }));
            if (n == "Si") return chain(call("sinc", { u }));
            if (n == "Ci") return chain(div(call("cos", { u }), u));
            if (n == "Shi") return chain(div(call("sinh", { u }), u));
            if (n == "Chi") return chain(div(call("cosh", { u }), u));
            if (n == "Ei" && e->args.size() == 1) return chain(div(call("exp", { u }), u));
            if (n == "li" && e->args.size() == 1) return chain(div(one, call("ln", { u })));
            if (n == "trilog") return chain(div(call("dilog", { u }), u));
            if (n == "fresnelS") return chain(call("sin", { mul(div(variable("pi"), two), pow(u, two)) }));
            if (n == "fresnelC") return chain(call("cos", { mul(div(variable("pi"), two), pow(u, two)) }));
            if (n == "dilog") return chain(neg(div(call("ln", { sub(one, u) }), u)));
            if ((n == "clausen" && e->args.size() == 1) || n == "clausenintegral") return chain(neg(call("ln", { call("abs", { mul(two, call("sin", { div(u, two) })) }) })));
            if (n == "inverf") return chain(mul(div(call("sqrt", { variable("pi") }), two), call("exp", { pow(e, two) })));
            if (n == "lt" || n == "le" || n == "gt" || n == "ge" || n == "eq" || n == "ne" || n == "and" || n == "or" || n == "not") return number(0.0);
            if (n == "if" && e->args.size() >= 2) {
                ExprPtr da = derivative(e->args[1], v);
                ExprPtr db = e->args.size() > 2 ? derivative(e->args[2], v) : number(0.0);
                if (!da || !db) return nullptr;
                return make(Op::Call, e->args.size() > 2 ? std::vector<ExprPtr>{ e->args[0], da, db } : std::vector<ExprPtr>{ e->args[0], da }, "if");
            }
            {
                const ExprPtr h = mul(number(1e-6), add(one, call("abs", { variable(v) })));
                std::map<std::string, ExprPtr> plus{ { v, add(variable(v), h) } };
                std::map<std::string, ExprPtr> minus{ { v, sub(variable(v), h) } };
                return div(sub(substitute(e, plus), substitute(e, minus)), mul(two, h));
            }
        }
    }
    return nullptr;
}

namespace {

int precedence(const ExprPtr& e) {
    switch (e->op) {
        case Op::Add:
        case Op::Sub:
            return 1;
        case Op::Mul:
        case Op::Div:
            return 2;
        case Op::Neg:
            return 3;
        case Op::Pow:
            return 4;
        default:
            return 5;
    }
}

std::string format_number(double v) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    std::ostringstream os;
    os.precision(12);
    os << v;
    return os.str();
}

std::string greek_math(const std::string& name) {
    static const char* greek[] = { "alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta", "iota", "kappa", "lambda", "mu", "nu", "xi", "pi", "rho", "sigma", "tau", "upsilon", "phi", "chi", "psi", "omega" };
    for (const char* g : greek) {
        if (name == g) return std::string("\\") + g + "\\";
    }
    return name;
}

std::string print(const ExprPtr& e, bool math) {
    if (!e) return "?";
    auto wrap = [&](const ExprPtr& c, int min_prec) {
        std::string s = print(c, math);
        return precedence(c) < min_prec ? "(" + s + ")" : s;
    };
    switch (e->op) {
        case Op::Number:
            return e->text.empty() ? format_number(e->value) : e->text;
        case Op::Variable:
            return math ? greek_math(e->text) : e->text;
        case Op::Imag:
            return "i";
        case Op::Add:
            return wrap(e->args[0], 1) + " + " + wrap(e->args[1], 1);
        case Op::Sub:
            return wrap(e->args[0], 1) + " - " + wrap(e->args[1], 2);
        case Op::Mul:
            return wrap(e->args[0], 2) + (math ? "*" : " * ") + wrap(e->args[1], 3);
        case Op::Div:
            return wrap(e->args[0], 2) + " / " + wrap(e->args[1], 3);
        case Op::Pow:
            return wrap(e->args[0], 5) + "^" + wrap(e->args[1], 4);
        case Op::Neg:
            return "-" + wrap(e->args[0], 3);
        case Op::Call: {
            std::string s = e->text + "(";
            for (std::size_t i = 0; i < e->args.size(); ++i) {
                if (i) s += ", ";
                s += print(e->args[i], math);
            }
            return s + ")";
        }
        case Op::Vector: {
            std::string s = "(";
            for (std::size_t i = 0; i < e->args.size(); ++i) {
                if (i) s += ", ";
                s += print(e->args[i], math);
            }
            return s + ")";
        }
    }
    return "?";
}

} // namespace

std::string to_string(const ExprPtr& e) {
    return print(e, false);
}

std::string to_mathtext(const ExprPtr& e) {
    return print(e, true);
}

const char* relation_text(Relation r) {
    switch (r) {
        case Relation::Equal: return "=";
        case Relation::Less: return "<";
        case Relation::LessEqual: return "<=";
        case Relation::Greater: return ">";
        case Relation::GreaterEqual: return ">=";
        default: return "";
    }
}

namespace {

enum class Tok {
    End,
    Number,
    Word,
    Plus,
    Minus,
    Star,
    Slash,
    Caret,
    LParen,
    RParen,
    LBracket,
    RBracket,
    Comma,
    Bar,
    Bang,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Equal,
    NotEqual,
    AngleOpen,
    AngleClose,
    Sqrt,
    Square,
    Cube,
    Bad
};

struct Token {
    Tok kind = Tok::End;
    std::string text;
    std::size_t at = 0;
};

struct Greek {
    const char* utf8;
    const char* name;
};

const Greek k_greek[] = {
    { "\xCE\xB1", "alpha" }, { "\xCE\xB2", "beta" }, { "\xCE\xB3", "gamma_" }, { "\xCE\xB4", "delta" },
    { "\xCE\xB5", "epsilon" }, { "\xCE\xB6", "zeta" }, { "\xCE\xB7", "eta" }, { "\xCE\xB8", "theta" },
    { "\xCE\xB9", "iota" }, { "\xCE\xBA", "kappa" }, { "\xCE\xBB", "lambda" }, { "\xCE\xBC", "mu" },
    { "\xCE\xBD", "nu" }, { "\xCE\xBE", "xi" }, { "\xCF\x80", "pi" }, { "\xCF\x81", "rho" },
    { "\xCF\x83", "sigma" }, { "\xCF\x84", "tau" }, { "\xCF\x85", "upsilon" }, { "\xCF\x86", "phi" },
    { "\xCF\x95", "phi" }, { "\xCF\x87", "chi" }, { "\xCF\x88", "psi" }, { "\xCF\x89", "omega" },
    { "\xCE\x93", "gamma" }, { "\xCE\x94", "Delta" }, { "\xCE\xA3", "Sigma" }, { "\xCE\xA9", "Omega" },
    { "\xCE\xA6", "Phi" }, { "\xCE\xA8", "Psi" }
};

struct Symbol {
    const char* utf8;
    Tok kind;
};

const Symbol k_symbols[] = {
    { "\xE2\x89\xA4", Tok::LessEqual }, { "\xE2\x89\xA5", Tok::GreaterEqual }, { "\xE2\x89\xA0", Tok::NotEqual },
    { "\xC2\xB7", Tok::Star }, { "\xC3\x97", Tok::Star }, { "\xE2\x8B\x85", Tok::Star }, { "\xE2\x88\x99", Tok::Star },
    { "\xC3\xB7", Tok::Slash }, { "\xE2\x88\x92", Tok::Minus },
    { "\xE2\x88\x9A", Tok::Sqrt }, { "\xC2\xB2", Tok::Square }, { "\xC2\xB3", Tok::Cube },
    { "\xE2\x9F\xA8", Tok::AngleOpen }, { "\xE2\x9F\xA9", Tok::AngleClose },
    { "\xE2\x8C\xA9", Tok::AngleOpen }, { "\xE2\x8C\xAA", Tok::AngleClose }
};

bool word_char(unsigned char c) {
    return std::isalnum(c) || c == '_';
}

std::vector<Token> lex(const std::string& s, std::string& error, std::size_t& error_at) {
    std::vector<Token> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (std::isspace(c)) {
            ++i;
            continue;
        }
        Token t;
        t.at = i;
        if (std::isdigit(c) || (c == '.' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
            std::size_t j = i;
            while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
            if (j < s.size() && s[j] == '.') {
                ++j;
                while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
            }
            if (j + 1 < s.size() && (s[j] == 'e' || s[j] == 'E') && (std::isdigit(static_cast<unsigned char>(s[j + 1])) || ((s[j + 1] == '-' || s[j + 1] == '+') && j + 2 < s.size() && std::isdigit(static_cast<unsigned char>(s[j + 2]))))) {
                j += 2;
                while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
            }
            t.kind = Tok::Number;
            t.text = s.substr(i, j - i);
            out.push_back(t);
            i = j;
            continue;
        }
        bool matched = false;
        for (const Greek& g : k_greek) {
            const std::size_t n = std::char_traits<char>::length(g.utf8);
            if (s.compare(i, n, g.utf8) == 0) {
                t.kind = Tok::Word;
                t.text = std::string("\x01") + g.name;
                out.push_back(t);
                i += n;
                matched = true;
                break;
            }
        }
        if (matched) continue;
        if (s.compare(i, 3, "\xE2\x88\x9E") == 0) {
            t.kind = Tok::Word;
            t.text = "\x01inf";
            out.push_back(t);
            i += 3;
            continue;
        }
        for (const Symbol& sym : k_symbols) {
            const std::size_t n = std::char_traits<char>::length(sym.utf8);
            if (s.compare(i, n, sym.utf8) == 0) {
                t.kind = sym.kind;
                out.push_back(t);
                i += n;
                matched = true;
                break;
            }
        }
        if (matched) continue;
        if (std::isalpha(c)) {
            std::size_t j = i;
            std::string word;
            while (j < s.size()) {
                const unsigned char wc = static_cast<unsigned char>(s[j]);
                if (wc == '_' && j + 1 < s.size() && s[j + 1] == '{') {
                    const std::size_t close = s.find('}', j + 2);
                    bool plain = close != std::string::npos && close > j + 2;
                    for (std::size_t k = j + 2; plain && k < close; ++k) plain = std::isalnum(static_cast<unsigned char>(s[k])) != 0;
                    if (!plain) break;
                    word += "_" + s.substr(j + 2, close - j - 2);
                    j = close + 1;
                    continue;
                }
                if (!word_char(wc)) break;
                word += static_cast<char>(wc);
                ++j;
            }
            t.kind = Tok::Word;
            t.text = word;
            out.push_back(t);
            i = j;
            continue;
        }
        if (c == '\\') {
            std::size_t j = i + 1;
            while (j < s.size() && std::isalpha(static_cast<unsigned char>(s[j]))) ++j;
            t.kind = Tok::Word;
            t.text = std::string("\x01") + s.substr(i + 1, j - i - 1);
            if (j < s.size() && s[j] == '\\') ++j;
            out.push_back(t);
            i = j;
            continue;
        }
        switch (c) {
            case '+': t.kind = Tok::Plus; break;
            case '-': t.kind = Tok::Minus; break;
            case '*':
                if (i + 1 < s.size() && s[i + 1] == '*') {
                    t.kind = Tok::Caret;
                    ++i;
                } else {
                    t.kind = Tok::Star;
                }
                break;
            case '/': t.kind = Tok::Slash; break;
            case '^': t.kind = Tok::Caret; break;
            case '(': t.kind = Tok::LParen; break;
            case ')': t.kind = Tok::RParen; break;
            case '[': t.kind = Tok::LBracket; break;
            case ']': t.kind = Tok::RBracket; break;
            case '{': t.kind = Tok::LParen; break;
            case '}': t.kind = Tok::RParen; break;
            case ',': t.kind = Tok::Comma; break;
            case '|': t.kind = Tok::Bar; break;
            case '!':
                if (i + 1 < s.size() && s[i + 1] == '=') {
                    t.kind = Tok::NotEqual;
                    ++i;
                } else {
                    t.kind = Tok::Bang;
                }
                break;
            case '=':
                t.kind = Tok::Equal;
                if (i + 1 < s.size() && s[i + 1] == '=') ++i;
                break;
            case '<':
                if (i + 1 < s.size() && s[i + 1] == '=') {
                    t.kind = Tok::LessEqual;
                    ++i;
                } else {
                    t.kind = Tok::Less;
                }
                break;
            case '>':
                if (i + 1 < s.size() && s[i + 1] == '=') {
                    t.kind = Tok::GreaterEqual;
                    ++i;
                } else {
                    t.kind = Tok::Greater;
                }
                break;
            default:
                error = std::string("unexpected character '") + s[i] + "'";
                if (c >= 0x80) error = "unexpected symbol";
                error_at = i;
                return {};
        }
        out.push_back(t);
        ++i;
    }
    Token end;
    end.kind = Tok::End;
    end.at = s.size();
    out.push_back(end);
    return out;
}

const char* k_builtin_words[] = { "pi", "tau", "inf", "infinity", "theta", "phi", "rho", "alpha", "beta", "delta", "epsilon", "lambda", "mu", "sigma", "omega", "psi", "eta", "kappa", "zeta", "chi", "nu", "xi" };

struct Parser {
    std::vector<Token> toks;
    std::size_t pos = 0;
    const ParseOptions& options;
    std::string error;
    std::size_t error_at = 0;
    int bar_depth = 0;
    int angle_depth = 0;
    int condition_depth = 0;
    std::vector<std::string> names;

    explicit Parser(const ParseOptions& o) : options(o) {}

    const Token& peek(std::size_t k = 0) const {
        return toks[std::min(pos + k, toks.size() - 1)];
    }

    bool accept(Tok k) {
        if (peek().kind == k) {
            ++pos;
            return true;
        }
        return false;
    }

    void fail(const std::string& msg) {
        if (error.empty()) {
            error = msg;
            error_at = peek().at;
        }
    }

    bool is_function_name(const std::string& n) const {
        return find_function(n) != nullptr || options.function_names.count(n) > 0;
    }

    std::vector<std::string> split_word(const std::string& w) const {
        std::vector<std::string> out;
        if (!w.empty() && w[0] == '\x01') {
            std::string n = w.substr(1);
            if (n == "infinity") n = "inf";
            out.push_back(n);
            return out;
        }
        std::vector<std::string> known;
        for (const FunctionInfo& f : builtin_functions()) {
            if (!needs_paren_name(f.name)) known.push_back(f.name);
        }
        for (const Alias& a : k_aliases) known.push_back(a.from);
        for (const LibEntry& e : library()) {
            if (e.kind == LibKind::Constant && e.literal != "@i") {
                known.push_back(e.name);
                for (const std::string& a : e.aliases) known.push_back(a);
            } else if (e.kind == LibKind::Function && !e.needs_paren) {
                for (const std::string& a : e.aliases) known.push_back(a);
            }
        }
        for (const char* b : k_builtin_words) known.push_back(b);
        for (const std::string& n : options.known_names) known.push_back(n);
        for (const std::string& n : options.function_names) known.push_back(n);
        std::size_t i = 0;
        while (i < w.size()) {
            std::size_t best = 0;
            std::string best_name;
            for (const std::string& k : known) {
                if (k.size() > best && w.compare(i, k.size(), k) == 0) {
                    best = k.size();
                    best_name = k;
                }
            }
            if (best >= 2 || (best == 1 && !best_name.empty())) {
                std::string piece = best_name;
                i += best;
                if (i < w.size() && w[i] == '_') {
                    std::size_t j = i + 1;
                    while (j < w.size() && std::isalnum(static_cast<unsigned char>(w[j]))) ++j;
                    piece += w.substr(i, j - i);
                    i = j;
                }
                if (piece == "infinity") piece = "inf";
                out.push_back(piece);
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(w[i]))) {
                std::size_t j = i;
                while (j < w.size() && std::isdigit(static_cast<unsigned char>(w[j]))) ++j;
                out.push_back(w.substr(i, j - i));
                i = j;
                continue;
            }
            std::string piece(1, w[i]);
            ++i;
            if (i < w.size() && w[i] == '_') {
                std::size_t j = i + 1;
                while (j < w.size() && std::isalnum(static_cast<unsigned char>(w[j]))) ++j;
                piece += w.substr(i, j - i);
                i = j;
            }
            out.push_back(piece);
        }
        return out;
    }

    bool defines_word(std::size_t i) const {
        if (i != 0 || toks.size() < 2 || toks[0].kind != Tok::Word || toks[1].kind != Tok::Equal) return false;
        const std::string& w = toks[0].text;
        if (w.size() < 2 || find_function(w) || is_builtin_constant(w)) return false;
        for (char c : w) {
            if (!std::strchr("xyztuvwr", c)) return true;
        }
        return false;
    }

    void expand_words() {
        std::vector<Token> out;
        for (std::size_t i = 0; i < toks.size(); ++i) {
            const Token& t = toks[i];
            if (t.kind != Tok::Word || defines_word(i)) {
                out.push_back(t);
                continue;
            }
            if (i + 1 < toks.size() && toks[i + 1].kind == Tok::LParen && (find_function(t.text) || options.function_names.count(t.text))) {
                out.push_back(t);
                continue;
            }
            for (const std::string& piece : split_word(t.text)) {
                Token p;
                p.at = t.at;
                if (!piece.empty() && std::isdigit(static_cast<unsigned char>(piece[0]))) {
                    p.kind = Tok::Number;
                    p.text = piece;
                } else {
                    p.kind = Tok::Word;
                    p.text = piece;
                }
                out.push_back(p);
            }
        }
        toks = std::move(out);
    }

    bool starts_operand(const Token& t) const {
        switch (t.kind) {
            case Tok::Number:
            case Tok::Word:
                return !(condition_depth > 0 && (t.text == "and" || t.text == "or" || t.text == "not"));
            case Tok::LParen:
            case Tok::LBracket:
            case Tok::Sqrt:
            case Tok::AngleOpen:
                return true;
            case Tok::Bar:
                return bar_depth == 0;
            default:
                return false;
        }
    }

    ExprPtr parse_expr() {
        ExprPtr left = parse_term();
        while (left && error.empty()) {
            if (accept(Tok::Plus)) {
                ExprPtr r = parse_term();
                if (!r) return nullptr;
                left = make(Op::Add, { left, r });
            } else if (accept(Tok::Minus)) {
                ExprPtr r = parse_term();
                if (!r) return nullptr;
                left = make(Op::Sub, { left, r });
            } else {
                break;
            }
        }
        return left;
    }

    ExprPtr parse_term() {
        ExprPtr left = parse_unary();
        while (left && error.empty()) {
            if (accept(Tok::Star)) {
                ExprPtr r = parse_unary();
                if (!r) return nullptr;
                left = make(Op::Mul, { left, r });
            } else if (accept(Tok::Slash)) {
                ExprPtr r = parse_unary();
                if (!r) return nullptr;
                left = make(Op::Div, { left, r });
            } else if (peek().kind == Tok::Word && peek().text == "mod" && peek(1).kind != Tok::LParen) {
                ++pos;
                ExprPtr r = parse_unary();
                if (!r) return nullptr;
                left = make(Op::Call, { left, r }, "mod");
            } else if (starts_operand(peek())) {
                ExprPtr r = parse_power();
                if (!r) return nullptr;
                left = make(Op::Mul, { left, r });
            } else {
                break;
            }
        }
        return left;
    }

    ExprPtr parse_unary() {
        if (accept(Tok::Minus)) {
            ExprPtr a = parse_unary();
            return a ? make(Op::Neg, { a }) : nullptr;
        }
        if (accept(Tok::Plus)) return parse_unary();
        return parse_power();
    }

    ExprPtr parse_exponent() {
        if (accept(Tok::Minus)) {
            ExprPtr a = parse_exponent();
            return a ? make(Op::Neg, { a }) : nullptr;
        }
        return parse_power();
    }

    ExprPtr parse_power() {
        ExprPtr base = parse_postfix();
        if (!base) return nullptr;
        if (accept(Tok::Caret)) {
            ExprPtr ex = parse_exponent();
            if (!ex) return nullptr;
            return make(Op::Pow, { base, ex });
        }
        return base;
    }

    ExprPtr parse_postfix() {
        ExprPtr e = parse_primary();
        while (e && error.empty()) {
            if (accept(Tok::Bang)) e = make(Op::Call, { make(Op::Add, { e, number(1.0) }) }, "gamma");
            else if (accept(Tok::Square)) e = make(Op::Pow, { e, number(2.0) });
            else if (accept(Tok::Cube)) e = make(Op::Pow, { e, number(3.0) });
            else break;
        }
        return e;
    }

    std::vector<ExprPtr> parse_list(Tok close) {
        std::vector<ExprPtr> items;
        if (peek().kind == close) return items;
        const int saved_bar = bar_depth;
        bar_depth = 0;
        while (error.empty()) {
            ExprPtr e = parse_expr();
            if (!e) break;
            items.push_back(e);
            if (!accept(Tok::Comma)) break;
        }
        bar_depth = saved_bar;
        return items;
    }

    bool comparison_ahead() const {
        switch (peek().kind) {
            case Tok::Less:
            case Tok::LessEqual:
            case Tok::Greater:
            case Tok::GreaterEqual:
            case Tok::Equal:
            case Tok::NotEqual:
                return true;
            default:
                return false;
        }
    }

    ExprPtr parse_comparison() {
        ExprPtr left = parse_expr();
        if (!left) return nullptr;
        ExprPtr result;
        while (error.empty() && comparison_ahead()) {
            const Tok k = peek().kind;
            ++pos;
            ExprPtr right = parse_expr();
            if (!right) return nullptr;
            const char* name = k == Tok::Less ? "lt" : k == Tok::LessEqual ? "le" : k == Tok::Greater ? "gt" : k == Tok::GreaterEqual ? "ge" : k == Tok::Equal ? "eq" : "ne";
            ExprPtr c = make(Op::Call, { left, right }, name);
            result = result ? make(Op::Call, { result, c }, "and") : c;
            left = right;
        }
        return result ? result : left;
    }

    bool word_ahead(const char* w) const {
        return peek().kind == Tok::Word && peek().text == w;
    }

    ExprPtr parse_not_condition() {
        if (word_ahead("not")) {
            ++pos;
            ExprPtr inner = parse_not_condition();
            return inner ? make(Op::Call, { inner }, "not") : nullptr;
        }
        return parse_comparison();
    }

    ExprPtr parse_and_condition() {
        ExprPtr left = parse_not_condition();
        while (left && error.empty() && word_ahead("and")) {
            ++pos;
            ExprPtr r = parse_not_condition();
            if (!r) return nullptr;
            left = make(Op::Call, { left, r }, "and");
        }
        return left;
    }

    ExprPtr parse_condition() {
        ExprPtr left = parse_and_condition();
        while (left && error.empty() && word_ahead("or")) {
            ++pos;
            ExprPtr r = parse_and_condition();
            if (!r) return nullptr;
            left = make(Op::Call, { left, r }, "or");
        }
        return left;
    }

    ExprPtr parse_function_argument() {
        ExprPtr first = parse_power();
        if (!first) return nullptr;
        while (error.empty() && starts_operand(peek())) {
            if (peek().kind == Tok::Word && is_function_name(peek().text)) break;
            ExprPtr r = parse_power();
            if (!r) return nullptr;
            first = make(Op::Mul, { first, r });
        }
        return first;
    }

    std::string inverse_name(const std::string& n) const {
        static const char* trig[][2] = { { "sin", "asin" }, { "cos", "acos" }, { "tan", "atan" }, { "sec", "asec" }, { "csc", "acsc" }, { "cot", "acot" }, { "sinh", "asinh" }, { "cosh", "acosh" }, { "tanh", "atanh" }, { "sech", "asech" }, { "csch", "acsch" }, { "coth", "acoth" }, { "gd", "invgd" }, { "sinc", "invsinc" } };
        for (auto& p : trig) {
            if (n == p[0]) return p[1];
        }
        return std::string();
    }

    ExprPtr parse_primary() {
        const Token t = peek();
        switch (t.kind) {
            case Tok::Number:
                ++pos;
                return number_text(t.text);
            case Tok::LParen: {
                ++pos;
                std::vector<ExprPtr> items = parse_list(Tok::RParen);
                if (!accept(Tok::RParen)) {
                    fail("missing )");
                    return nullptr;
                }
                if (items.empty()) {
                    fail("empty ( )");
                    return nullptr;
                }
                return items.size() == 1 ? items[0] : make(Op::Vector, items);
            }
            case Tok::LBracket: {
                ++pos;
                std::vector<ExprPtr> items = parse_list(Tok::RBracket);
                if (!accept(Tok::RBracket)) {
                    fail("missing ]");
                    return nullptr;
                }
                if (items.empty()) {
                    fail("empty [ ]");
                    return nullptr;
                }
                return items.size() == 1 ? items[0] : make(Op::Vector, items);
            }
            case Tok::AngleOpen: {
                ++pos;
                std::vector<ExprPtr> items = parse_list(Tok::AngleClose);
                if (!accept(Tok::AngleClose)) {
                    fail("missing \xE2\x9F\xA9");
                    return nullptr;
                }
                return make(Op::Vector, items);
            }
            case Tok::Less: {
                ++pos;
                ++angle_depth;
                std::vector<ExprPtr> items = parse_list(Tok::Greater);
                --angle_depth;
                if (!accept(Tok::Greater)) {
                    fail("missing >");
                    return nullptr;
                }
                if (items.empty()) {
                    fail("empty < >");
                    return nullptr;
                }
                return make(Op::Vector, items);
            }
            case Tok::Bar: {
                ++pos;
                ++bar_depth;
                const int saved = bar_depth;
                ExprPtr inner = parse_expr();
                bar_depth = saved - 1;
                if (!inner) return nullptr;
                if (!accept(Tok::Bar)) {
                    fail("missing closing |");
                    return nullptr;
                }
                return make(Op::Call, { inner }, "abs");
            }
            case Tok::Sqrt: {
                ++pos;
                ExprPtr a = parse_power();
                return a ? make(Op::Call, { a }, "sqrt") : nullptr;
            }
            case Tok::Word: {
                ++pos;
                const std::string name = canonical_name(t.text);
                const bool paren_only = !options.function_names.count(t.text) && needs_paren_name(name) && peek().kind != Tok::LParen;
                if (!paren_only && (is_function_name(t.text) || is_function_name(name))) {
                    std::string fname = options.function_names.count(t.text) ? t.text : name;
                    if (fname == "if" && peek().kind == Tok::LParen) {
                        ++pos;
                        std::vector<ExprPtr> args;
                        const int saved_bar = bar_depth;
                        const int saved_angle = angle_depth;
                        bar_depth = 0;
                        angle_depth = 0;
                        ++condition_depth;
                        while (error.empty()) {
                            ExprPtr a = parse_condition();
                            if (!a) break;
                            args.push_back(a);
                            if (!accept(Tok::Comma)) break;
                        }
                        --condition_depth;
                        bar_depth = saved_bar;
                        angle_depth = saved_angle;
                        if (!error.empty()) return nullptr;
                        if (!accept(Tok::RParen)) {
                            fail("missing ) after if(");
                            return nullptr;
                        }
                        if (args.size() < 2 || args.size() > 3) {
                            fail("if takes 2 or 3 inputs: if(condition, value, otherwise)");
                            return nullptr;
                        }
                        return make(Op::Call, args, "if");
                    }
                    ExprPtr power;
                    if (peek().kind == Tok::Caret) {
                        ++pos;
                        ExprPtr ex = parse_exponent();
                        if (!ex) return nullptr;
                        if (ex->op == Op::Neg && is_number(ex->args[0], 1.0) && !inverse_name(fname).empty()) fname = inverse_name(fname);
                        else power = ex;
                    }
                    std::vector<ExprPtr> args;
                    if (peek().kind == Tok::LParen) {
                        ++pos;
                        args = parse_list(Tok::RParen);
                        if (!accept(Tok::RParen)) {
                            fail("missing ) after " + fname + "(");
                            return nullptr;
                        }
                    } else {
                        ExprPtr a = parse_function_argument();
                        if (!a) {
                            fail(fname + " needs an input");
                            return nullptr;
                        }
                        args.push_back(a);
                    }
                    if (const FunctionInfo* f = find_function(fname)) {
                        if (static_cast<int>(args.size()) < f->min_args || static_cast<int>(args.size()) > f->max_args) {
                            fail(fname + " takes " + std::to_string(f->min_args) + (f->max_args != f->min_args ? " to " + std::to_string(f->max_args) : std::string()) + (f->max_args == 1 ? " input" : " inputs"));
                            return nullptr;
                        }
                    }
                    ExprPtr c = make(Op::Call, args, fname);
                    return power ? make(Op::Pow, { c, power }) : c;
                }
                if (name == "i") return make(Op::Imag);
                names.push_back(name);
                return make(Op::Variable, {}, name);
            }
            case Tok::End:
                fail("the expression ends too soon");
                return nullptr;
            default:
                fail("unexpected " + describe(t));
                return nullptr;
        }
    }

    static std::string describe(const Token& t) {
        switch (t.kind) {
            case Tok::RParen: return ")";
            case Tok::RBracket: return "]";
            case Tok::Comma: return "comma";
            case Tok::Equal: return "=";
            case Tok::NotEqual: return "!=";
            case Tok::Star: return "*";
            case Tok::Slash: return "/";
            case Tok::Caret: return "^";
            case Tok::Plus: return "+";
            case Tok::Bar: return "|";
            case Tok::Greater: return ">";
            case Tok::Less: return "<";
            default: return "symbol";
        }
    }
};

Relation relation_of(Tok k) {
    switch (k) {
        case Tok::Equal: return Relation::Equal;
        case Tok::Less: return Relation::Less;
        case Tok::LessEqual: return Relation::LessEqual;
        case Tok::Greater: return Relation::Greater;
        case Tok::GreaterEqual: return Relation::GreaterEqual;
        default: return Relation::None;
    }
}

bool detect_function_definition(const std::vector<Token>& toks, std::string& name, std::vector<std::string>& params) {
    if (toks.size() < 5 || toks[0].kind != Tok::Word || toks[1].kind != Tok::LParen) return false;
    if (toks[0].text.empty() || toks[0].text[0] == '\x01') return false;
    if (find_function(toks[0].text)) return false;
    std::size_t i = 2;
    std::vector<std::string> ps;
    while (i < toks.size() && toks[i].kind == Tok::Word) {
        std::string p = toks[i].text;
        if (!p.empty() && p[0] == '\x01') p = p.substr(1);
        ps.push_back(p);
        ++i;
        if (toks[i].kind == Tok::Comma) {
            ++i;
            continue;
        }
        break;
    }
    if (ps.empty() || i + 1 >= toks.size() || toks[i].kind != Tok::RParen || toks[i + 1].kind != Tok::Equal) return false;
    name = toks[0].text;
    params = ps;
    return true;
}

ExprPtr finish(const ExprPtr& e) {
    if (t_as_typed) return e;
    return simplify(e);
}

} // namespace

ParseResult parse_statement(const std::string& text, const ParseOptions& options) {
    ParseResult result;
    std::string error;
    std::size_t error_at = 0;
    std::vector<Token> toks = lex(text, error, error_at);
    if (!error.empty()) {
        result.error = error;
        result.error_at = error_at;
        return result;
    }
    if (toks.size() == 1) {
        result.error = "empty";
        return result;
    }
    std::string fname;
    std::vector<std::string> fparams;
    ParseOptions opts = options;
    const bool is_def = detect_function_definition(toks, fname, fparams);
    if (is_def) {
        opts.function_names.erase(fname);
        for (const std::string& p : fparams) opts.known_names.insert(p);
    }
    Parser p(opts);
    p.toks = std::move(toks);
    if (is_def) {
        std::size_t i = 0;
        while (p.toks[i].kind != Tok::Equal) ++i;
        p.pos = i + 1;
        p.expand_words();
        std::size_t eq = 0;
        while (p.toks[eq].kind != Tok::Equal) ++eq;
        p.pos = eq + 1;
        ExprPtr body = p.parse_expr();
        if (!body || !p.error.empty() || p.peek().kind != Tok::End) {
            result.error = p.error.empty() ? "unexpected " + Parser::describe(p.peek()) : p.error;
            result.error_at = p.error.empty() ? p.peek().at : p.error_at;
            return result;
        }
        result.statement.defines = fname;
        result.statement.params = fparams;
        result.statement.is_function_definition = true;
        result.statement.relation = Relation::Equal;
        std::vector<ExprPtr> ps;
        for (const std::string& name : fparams) ps.push_back(variable(name));
        result.statement.lhs = make(Op::Call, ps, fname);
        result.statement.rhs = finish(body);
        result.ok = true;
        return result;
    }
    p.expand_words();
    ExprPtr lhs = p.parse_expr();
    if (!lhs || !p.error.empty()) {
        result.error = p.error;
        result.error_at = p.error_at;
        return result;
    }
    const Relation rel = relation_of(p.peek().kind);
    if (rel != Relation::None) {
        ++p.pos;
        ExprPtr rhs = p.parse_expr();
        if (!rhs || !p.error.empty()) {
            result.error = p.error.empty() ? "nothing after " + std::string(relation_text(rel)) : p.error;
            result.error_at = p.error_at;
            return result;
        }
        result.statement.rhs = finish(rhs);
    }
    if (p.peek().kind != Tok::End) {
        result.error = "unexpected " + Parser::describe(p.peek());
        result.error_at = p.peek().at;
        return result;
    }
    result.statement.lhs = finish(lhs);
    result.statement.relation = rel;
    if (rel == Relation::Equal && lhs->op == Op::Variable && !is_builtin_constant(lhs->text)) {
        result.statement.defines = lhs->text;
        result.statement.is_value_definition = true;
    }
    result.ok = true;
    return result;
}

ParseResult parse_statement_as_typed(const std::string& text, const ParseOptions& options) {
    const bool saved = t_as_typed;
    t_as_typed = true;
    ParseResult r;
    try {
        r = parse_statement(text, options);
    } catch (...) {
        t_as_typed = saved;
        throw;
    }
    t_as_typed = saved;
    return r;
}

namespace {

int display_precedence(const ExprPtr& e) {
    switch (e->op) {
        case Op::Add:
        case Op::Sub:
            return 1;
        case Op::Mul:
            return 2;
        case Op::Neg:
            return 3;
        case Op::Pow:
            return 4;
        default:
            return 5;
    }
}

std::string display_name(const std::string& name) {
    static const char* const greek[] = { "alpha", "beta", "gamma", "delta", "epsilon", "varepsilon", "zeta", "eta", "theta", "vartheta", "iota", "kappa", "lambda", "mu", "nu", "xi", "pi", "varpi", "rho", "varrho", "sigma", "varsigma", "tau", "upsilon", "phi", "varphi", "chi", "psi", "omega",
                                         "Gamma", "Delta", "Theta", "Lambda", "Xi", "Pi", "Sigma", "Upsilon", "Phi", "Psi", "Omega" };
    const std::size_t bar = name.find('_');
    if (bar != std::string::npos && bar > 0 && bar + 1 < name.size()) return display_name(name.substr(0, bar)) + "_{" + display_name(name.substr(bar + 1)) + "}";
    if (name == "inf" || name == "infinity") return "\\infty\\";
    if (name == "eulergamma") return "\\gamma\\";
    if (name == "golden") return "\\varphi\\";
    if (name == "catalan") return "G";
    if (name == "apery") return "\\zeta\\(3)";
    if (name == "silver") return "\\delta\\_{S}";
    if (name == "silverconstant") return "\\varsigma\\";
    if (name == "bronze") return "\\sigma\\_{3}";
    for (const char* g : greek) {
        if (name == g) return std::string("\\") + g + "\\";
    }
    return name;
}

std::string display_number(const ExprPtr& e) {
    std::string t = e->text.empty() ? format_number(e->value) : e->text;
    const std::size_t ex = t.find_first_of("eE");
    if (ex != std::string::npos && ex > 0 && ex + 1 < t.size()) {
        std::string power = t.substr(ex + 1);
        if (!power.empty() && power[0] == '+') power.erase(0, 1);
        return t.substr(0, ex) + " \\times\\ 10^{" + power + "}";
    }
    return t;
}

std::string display(const ExprPtr& e);

std::string display_wrapped(const ExprPtr& e, int min_prec) {
    const std::string s = display(e);
    return display_precedence(e) < min_prec ? "(" + s + ")" : s;
}

bool display_atom(const ExprPtr& e) {
    if (e->op == Op::Variable || e->op == Op::Imag) return true;
    if (e->op == Op::Number) return e->value >= 0.0 && e->text.find_first_of("eE") == std::string::npos;
    if (e->op == Op::Call) {
        static const char* const loose[] = { "sqrt", "cbrt", "nthroot", "exp", "gamma" };
        for (const char* n : loose) {
            if (e->text == n) return false;
        }
        return true;
    }
    return false;
}

bool starts_with_digit(const std::string& s) {
    return !s.empty() && (std::isdigit(static_cast<unsigned char>(s[0])) || s[0] == '.' || s[0] == '-');
}

std::string display_args(const std::vector<ExprPtr>& args) {
    std::string s;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i) s += ", ";
        s += display(args[i]);
    }
    return s;
}

std::string display_call(const ExprPtr& e) {
    const std::string& n = e->text;
    const std::vector<ExprPtr>& a = e->args;
    if (a.size() == 1) {
        if (n == "sqrt") return "sqrt(" + display(a[0]) + ")";
        if (n == "cbrt") return "root(3, " + display(a[0]) + ")";
        if (n == "exp") return "e^{" + display(a[0]) + "}";
        if (n == "abs" || n == "floor" || n == "ceil") return n + "(" + display(a[0]) + ")";
        if (n == "log10") return "log_{10}(" + display(a[0]) + ")";
        if (n == "log2") return "log_{2}(" + display(a[0]) + ")";
        if (n == "gamma") {
            const ExprPtr& x = a[0];
            if (x->op == Op::Add && x->args.size() == 2 && is_number(x->args[1], 1.0)) {
                const ExprPtr& base = x->args[0];
                return (display_atom(base) ? display(base) : "(" + display(base) + ")") + "!";
            }
            return "\\Gamma\\(" + display(x) + ")";
        }
        if (n == "re") return "Re(" + display(a[0]) + ")";
        if (n == "im") return "Im(" + display(a[0]) + ")";
    }
    if (n == "nthroot" && a.size() == 2) return "root(" + display(a[1]) + ", " + display(a[0]) + ")";
    static const std::pair<const char*, const char*> comparisons[] = { { "lt", " < " }, { "le", " \\leq\\ " }, { "gt", " > " }, { "ge", " \\geq\\ " }, { "eq", " = " }, { "ne", " \\neq\\ " }, { "and", " and " }, { "or", " or " } };
    for (const auto& c : comparisons) {
        if (n == c.first && a.size() == 2) return display(a[0]) + c.second + display(a[1]);
    }
    if (n == "not" && a.size() == 1) return "not " + display(a[0]);
    if (n == "if" && a.size() >= 2) return "if(" + display_args(a) + ")";
    if (a.size() == 1) {
        if (n == "zeta") return "\\zeta\\(" + display(a[0]) + ")";
        if (n == "digamma") return "\\psi\\(" + display(a[0]) + ")";
        if (n == "trigamma") return "\\psi\\_{1}(" + display(a[0]) + ")";
        if (n == "lgamma") return "ln \\Gamma\\(" + display(a[0]) + ")";
        if (n == "lambertw") return "LambertW(" + display(a[0]) + ")";
        if (n == "invsinc") return "InverseSinc(" + display(a[0]) + ")";
        if (n == "invgd") return "gd^{-1}(" + display(a[0]) + ")";
        if (n == "mag") return "abs(" + display(a[0]) + ")";
        if (n == "mag2") return "abs(" + display(a[0]) + ")^{2}";
        if (n == "lagamma") return "ln abs(\\Gamma\\(" + display(a[0]) + "))";
        if (n == "clausenintegral") return "Cl_{2}(" + display(a[0]) + ")";
        if (n == "bernoulli") return "B_{" + display(a[0]) + "}";
        if (n == "eulernumber") return "E_{" + display(a[0]) + "}";
        if (n == "tangentnumber") return "T_{" + display(a[0]) + "}";
        if (n == "secantnumber") return "S_{" + display(a[0]) + "}";
        if (n == "stieltjes") return "\\gamma\\_{" + display(a[0]) + "}";
        if (n == "metallic") return "\\sigma\\_{" + display(a[0]) + "}";
        if (n == "invmetallic") return "{(1)/(\\sigma\\_{" + display(a[0]) + "})}";
        if (n == "metallicmean") return "M_{" + display(a[0]) + "}";
        if (n == "fibonacci") return "F_{" + display(a[0]) + "}";
        if (n == "lucas") return "L_{" + display(a[0]) + "}";
        if (n == "trilog") return "Li_{3}(" + display(a[0]) + ")";
        if (n == "dirichletlambda") return "\\lambda\\(" + display(a[0]) + ")";
        if (n == "riemannchi") return "\\chi\\(" + display(a[0]) + ")";
        if (n == "inverfi") return "erfi^{-1}(" + display(a[0]) + ")";
        if (n == "nsinc") return "sinc(\\pi\\ " + display_wrapped(a[0], 3) + ")";
        if (n == "factorial") return (display_atom(a[0]) ? display(a[0]) : "(" + display(a[0]) + ")") + "!";
        if (n == "harmonic") return "H_{" + display(a[0]) + "}";
        if (n == "dilog") return "Li_{2}(" + display(a[0]) + ")";
        if (n == "dirichleteta") return "\\eta\\(" + display(a[0]) + ")";
        if (n == "dirichletbeta") return "\\beta\\(" + display(a[0]) + ")";
        if (n == "riemannxi") return "\\xi\\(" + display(a[0]) + ")";
        if (n == "inverf") return "erf^{-1}(" + display(a[0]) + ")";
        if (n == "inverfc") return "erfc^{-1}(" + display(a[0]) + ")";
        if (n == "ellipticK") return "K(" + display(a[0]) + ")";
        if (n == "ellipticE") return "E(" + display(a[0]) + ")";
        if (n == "fresnelS") return "S(" + display(a[0]) + ")";
        if (n == "fresnelC") return "C(" + display(a[0]) + ")";
        if (n == "clausen") return "Cl_{2}(" + display(a[0]) + ")";
        if (n == "gd") return "gd(" + display(a[0]) + ")";
    }
    if (a.size() == 2) {
        if (n == "zeta") return "\\zeta\\(" + display_args(a) + ")";
        if (n == "beta") return "B(" + display_args(a) + ")";
        if (n == "lambertw") return "LambertW_{" + display(a[1]) + "}(" + display(a[0]) + ")";
        if (n == "clausen") return "Cl_{" + display(a[1]) + "}(" + display(a[0]) + ")";
        if (n == "Ei") return "E_{" + display(a[1]) + "}(" + display(a[0]) + ")";
        if (n == "li") return "li_{" + display(a[1]) + "}(" + display(a[0]) + ")";
        if (n == "eulerian") return "A(" + display_args(a) + ")";
        if (n == "fibonacci") return "F_{" + display(a[1]) + "}(" + display(a[0]) + ")";
        if (n == "lucas") return "L_{" + display(a[1]) + "}(" + display(a[0]) + ")";
        if (n == "chebyshevT") return "T_{" + display(a[1]) + "}(" + display(a[0]) + ")";
        if (n == "chebyshevU") return "U_{" + display(a[1]) + "}(" + display(a[0]) + ")";
        if (n == "chebyshevV") return "V_{" + display(a[1]) + "}(" + display(a[0]) + ")";
        if (n == "chebyshevW") return "W_{" + display(a[1]) + "}(" + display(a[0]) + ")";
        if (n == "legendreP") return "P_{" + display(a[1]) + "}(" + display(a[0]) + ")";
        if (n == "legendreQ") return "Q_{" + display(a[1]) + "}(" + display(a[0]) + ")";
        if (n == "debye") return "D_{" + display(a[1]) + "}(" + display(a[0]) + ")";
        if (n == "legendrechi") return "\\chi\\_{" + display(a[1]) + "}(" + display(a[0]) + ")";
        if (n == "carlsonRC") return "R_{C}(" + display_args(a) + ")";
        if (n == "invgammaP") return "P^{-1}(" + display_args(a) + ")";
        if (n == "invgammaQ") return "Q^{-1}(" + display_args(a) + ")";
        if (n == "lbeta") return "ln abs(B(" + display_args(a) + "))";
        if (n == "polygamma") return "\\psi\\^{(" + display(a[0]) + ")}(" + display(a[1]) + ")";
        if (n == "polylog") return "Li_{" + display(a[0]) + "}(" + display(a[1]) + ")";
        if (n == "pochhammer") return "(" + display(a[0]) + ")_{" + display(a[1]) + "}";
        if (n == "binomial") return "binom(" + display_args(a) + ")";
        if (n == "ellipticE") return "E(" + display_args(a) + ")";
        if (n == "ellipticF") return "F(" + display_args(a) + ")";
        if (n == "ellipticPi") return "\\Pi\\(" + display_args(a) + ")";
        if (n == "gammaP") return "P(" + display_args(a) + ")";
        if (n == "gammaQ") return "Q(" + display_args(a) + ")";
        if (n == "lowergamma") return "\\gamma\\(" + display_args(a) + ")";
        if (n == "uppergamma") return "\\Gamma\\(" + display_args(a) + ")";
    }
    if (n == "log" && a.size() == 2) return "log_{" + display(a[1]) + "}(" + display(a[0]) + ")";
    if (a.size() == 3) {
        if (n == "ellipticPi") return "\\Pi\\(" + display_args(a) + ")";
        if (n == "carlsonRF") return "R_{F}(" + display_args(a) + ")";
        if (n == "carlsonRD") return "R_{D}(" + display_args(a) + ")";
        if (n == "carlsonRG") return "R_{G}(" + display_args(a) + ")";
        if (n == "lerch") return "\\Phi\\(" + display_args(a) + ")";
        const std::string rest = display(a[1]) + ", " + display(a[2]);
        if (n == "betainc") return "B_{" + display(a[0]) + "}(" + rest + ")";
        if (n == "betareg") return "I_{" + display(a[0]) + "}(" + rest + ")";
        if (n == "betaregc") return "(1 - I_{" + display(a[0]) + "}(" + rest + "))";
        if (n == "invbetareg") return "I^{-1}_{" + display(a[0]) + "}(" + rest + ")";
    }
    if (n == "carlsonRJ" && a.size() == 4) return "R_{J}(" + display_args(a) + ")";
    static const std::pair<const char*, const char*> renamed[] = {
        { "asin", "arcsin" }, { "acos", "arccos" }, { "atan", "arctan" }, { "asec", "arcsec" }, { "acsc", "arccsc" }, { "acot", "arccot" },
        { "asinh", "arcsinh" }, { "acosh", "arccosh" }, { "atanh", "arctanh" },
        { "asech", "arcsech" }, { "acsch", "arccsch" }, { "acoth", "arccoth" }
    };
    std::string shown = n;
    for (const auto& r : renamed) {
        if (n == r.first) shown = r.second;
    }
    return display_name(shown) + "(" + display_args(a) + ")";
}

std::string display(const ExprPtr& e) {
    if (!e) return "?";
    switch (e->op) {
        case Op::Number:
            return display_number(e);
        case Op::Variable:
            return display_name(e->text);
        case Op::Imag:
            return "i";
        case Op::Add: {
            const ExprPtr& b = e->args[1];
            if (b->op == Op::Neg) return display_wrapped(e->args[0], 1) + " - " + display_wrapped(b->args[0], 2);
            return display_wrapped(e->args[0], 1) + " + " + display_wrapped(b, 1);
        }
        case Op::Sub:
            if (e->args[1]->op == Op::Neg) return display_wrapped(e->args[0], 1) + " - (" + display(e->args[1]) + ")";
            return display_wrapped(e->args[0], 1) + " - " + display_wrapped(e->args[1], 2);
        case Op::Mul: {
            const std::string left = display_wrapped(e->args[0], 2);
            const ExprPtr& b = e->args[1];
            std::string right = display_wrapped(b, 3);
            if (b->op == Op::Neg) right = "(" + display(b) + ")";
            const bool dot = b->op == Op::Number || starts_with_digit(right) || (e->args[0]->op == Op::Number && b->op == Op::Pow && b->args[0]->op == Op::Number);
            return left + (dot ? " \\cdot\\ " : " ") + right;
        }
        case Op::Div:
            return "{(" + display(e->args[0]) + ")/(" + display(e->args[1]) + ")}";
        case Op::Pow: {
            const ExprPtr& base = e->args[0];
            const ExprPtr& power = e->args[1];
            if (base->op == Op::Call && base->args.size() == 1 && power->op == Op::Number && power->value > 0.0 && std::floor(power->value) == power->value && power->value < 100.0) {
                static const char* const trig[] = { "sin", "cos", "tan", "sec", "csc", "cot", "sinh", "cosh", "tanh", "sech", "csch", "coth", "ln" };
                for (const char* t : trig) {
                    if (base->text == t) return base->text + "^{" + display(power) + "}(" + display(base->args[0]) + ")";
                }
            }
            const std::string b = display_atom(base) ? display(base) : "(" + display(base) + ")";
            return b + "^{" + display(e->args[1]) + "}";
        }
        case Op::Neg:
            return "-" + display_wrapped(e->args[0], 3);
        case Op::Call:
            return display_call(e);
        case Op::Vector:
            return "(" + display_args(e->args) + ")";
    }
    return "?";
}

const char* display_relation(Relation r) {
    switch (r) {
        case Relation::Equal: return " = ";
        case Relation::Less: return " < ";
        case Relation::LessEqual: return " <= ";
        case Relation::Greater: return " > ";
        case Relation::GreaterEqual: return " >= ";
        default: return " ";
    }
}

} // namespace

std::string display_mathtext(const ExprPtr& e) {
    return display(e);
}

std::string display_mathtext(const Statement& s) {
    if (s.is_function_definition) {
        std::string head = display_name(s.defines) + "(";
        for (std::size_t i = 0; i < s.params.size(); ++i) {
            if (i) head += ", ";
            head += display_name(s.params[i]);
        }
        return head + ") = " + display(s.rhs);
    }
    if (s.relation == Relation::None || !s.rhs) return display(s.lhs);
    return display(s.lhs) + display_relation(s.relation) + display(s.rhs);
}

ParseResult parse_expression(const std::string& text, const ParseOptions& options) {
    ParseResult r = parse_statement(text, options);
    if (r.ok && r.statement.relation != Relation::None) {
        r.ok = false;
        r.error = "expected an expression, not an equation";
    }
    return r;
}

ParseResult parse_condition(const std::string& text, const ParseOptions& options) {
    ParseResult r = parse_expression("if(" + text + ", 1, 0)", options);
    if (r.ok && r.statement.lhs && r.statement.lhs->op == Op::Call && r.statement.lhs->text == "if" && !r.statement.lhs->args.empty()) {
        r.statement.lhs = r.statement.lhs->args[0];
    } else if (r.ok) {
        r.ok = false;
        r.error = "write a condition like n mod 2 = 0 or x < 1 and y > 0";
    }
    if (!r.ok && r.error.find("if") != std::string::npos) r.error = "write a condition like n mod 2 = 0 or x < 1 and y > 0";
    return r;
}

} // namespace arbor
