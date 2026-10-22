#include "arbor/program.hpp"
#include "arbor/library.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>

namespace arbor {

namespace {

struct FnName {
    const char* name;
    int args;
    Fn fn;
};

const FnName k_fns[] = {
    { "sin", 1, Fn::Sin }, { "cos", 1, Fn::Cos }, { "tan", 1, Fn::Tan },
    { "sec", 1, Fn::Sec }, { "csc", 1, Fn::Csc }, { "cot", 1, Fn::Cot },
    { "asin", 1, Fn::Asin }, { "acos", 1, Fn::Acos }, { "atan", 1, Fn::Atan },
    { "asec", 1, Fn::Asec }, { "acsc", 1, Fn::Acsc }, { "acot", 1, Fn::Acot },
    { "atan", 2, Fn::Atan2 }, { "atan2", 2, Fn::Atan2 },
    { "sinh", 1, Fn::Sinh }, { "cosh", 1, Fn::Cosh }, { "tanh", 1, Fn::Tanh },
    { "sech", 1, Fn::Sech }, { "csch", 1, Fn::Csch }, { "coth", 1, Fn::Coth },
    { "asinh", 1, Fn::Asinh }, { "acosh", 1, Fn::Acosh }, { "atanh", 1, Fn::Atanh },
    { "exp", 1, Fn::Exp }, { "ln", 1, Fn::Ln }, { "log", 1, Fn::Log10 }, { "log10", 1, Fn::Log10 },
    { "log2", 1, Fn::Log2 }, { "log", 2, Fn::LogBase },
    { "sqrt", 1, Fn::Sqrt }, { "cbrt", 1, Fn::Cbrt }, { "nthroot", 2, Fn::NthRoot },
    { "abs", 1, Fn::Abs }, { "sign", 1, Fn::Sign }, { "floor", 1, Fn::Floor },
    { "ceil", 1, Fn::Ceil }, { "round", 1, Fn::Round },
    { "mod", 2, Fn::Mod }, { "min", 2, Fn::Min }, { "max", 2, Fn::Max },
    { "hypot", 2, Fn::Hypot }, { "gamma", 1, Fn::Gamma }, { "erf", 1, Fn::Erf },
    { "re", 1, Fn::Re }, { "im", 1, Fn::Im }, { "arg", 1, Fn::Arg }, { "conj", 1, Fn::Conj },
    { "if", 3, Fn::If }, { "lt", 2, Fn::Lt }, { "le", 2, Fn::Le }, { "gt", 2, Fn::Gt }, { "ge", 2, Fn::Ge },
    { "eq", 2, Fn::Eq }, { "ne", 2, Fn::Ne }, { "and", 2, Fn::And }, { "or", 2, Fn::Or }, { "not", 1, Fn::Not },
    { "sinc", 1, Fn::Sinc }, { "gd", 1, Fn::Gd }, { "expm1", 1, Fn::Expm1 }, { "log1p", 1, Fn::Log1p },
    { "lambertw", 1, Fn::LambertW }, { "lambertw", 2, Fn::LambertWk },
    { "lgamma", 1, Fn::LogGamma }, { "factorial", 1, Fn::Factorial }, { "digamma", 1, Fn::Digamma }, { "trigamma", 1, Fn::Trigamma },
    { "polygamma", 2, Fn::Polygamma }, { "beta", 2, Fn::Beta }, { "binomial", 2, Fn::Binomial }, { "pochhammer", 2, Fn::Pochhammer },
    { "harmonic", 1, Fn::Harmonic }, { "gammaP", 2, Fn::GammaP }, { "gammaQ", 2, Fn::GammaQ }, { "lowergamma", 2, Fn::LowerGamma },
    { "uppergamma", 2, Fn::UpperGamma }, { "zeta", 1, Fn::Zeta }, { "zeta", 2, Fn::HurwitzZeta }, { "dirichleteta", 1, Fn::DirichletEta },
    { "dirichletbeta", 1, Fn::DirichletBeta }, { "riemannxi", 1, Fn::RiemannXi }, { "polylog", 2, Fn::Polylog }, { "dilog", 1, Fn::Dilog },
    { "clausen", 1, Fn::Clausen }, { "erfc", 1, Fn::Erfc }, { "erfi", 1, Fn::Erfi }, { "inverf", 1, Fn::InvErf }, { "inverfc", 1, Fn::InvErfc },
    { "Si", 1, Fn::Si }, { "Ci", 1, Fn::Ci }, { "Shi", 1, Fn::Shi }, { "Chi", 1, Fn::Chi }, { "Ei", 1, Fn::Ei }, { "li", 1, Fn::Li },
    { "fresnelS", 1, Fn::FresnelS }, { "fresnelC", 1, Fn::FresnelC }, { "Ai", 1, Fn::AiryAi }, { "Bi", 1, Fn::AiryBi },
    { "ellipticK", 1, Fn::EllipticK }, { "ellipticE", 1, Fn::EllipticE }, { "ellipticE", 2, Fn::EllipticE2 }, { "ellipticF", 2, Fn::EllipticF },
    { "ellipticPi", 2, Fn::EllipticPi }, { "sn", 2, Fn::JacobiSn }, { "cn", 2, Fn::JacobiCn }, { "dn", 2, Fn::JacobiDn }, { "am", 2, Fn::JacobiAm },
    { "asech", 1, Fn::Asech }, { "acsch", 1, Fn::Acsch }, { "acoth", 1, Fn::Acoth }, { "invgd", 1, Fn::InvGd }, { "invsinc", 1, Fn::InvSinc },
    { "mag", 1, Fn::Mag }, { "mag2", 1, Fn::Mag2 }, { "lagamma", 1, Fn::LogAbsGamma }, { "clausen", 2, Fn::ClausenS }, { "clausenintegral", 1, Fn::Clausen },
    { "Ei", 2, Fn::ExpIntN }, { "li", 2, Fn::LogIntN }, { "Hi", 1, Fn::ScorerHi }, { "Gi", 1, Fn::ScorerGi },
    { "cd", 2, Fn::JacobiCd }, { "cs", 2, Fn::JacobiCs }, { "dc", 2, Fn::JacobiDc }, { "ds", 2, Fn::JacobiDs }, { "nc", 2, Fn::JacobiNc },
    { "nd", 2, Fn::JacobiNd }, { "ns", 2, Fn::JacobiNs }, { "sc", 2, Fn::JacobiSc }, { "sd", 2, Fn::JacobiSd },
    { "ellipticPi", 3, Fn::EllipticPi3 }, { "carlsonRC", 2, Fn::CarlsonRC }, { "carlsonRD", 3, Fn::CarlsonRD }, { "carlsonRF", 3, Fn::CarlsonRF },
    { "carlsonRG", 3, Fn::CarlsonRG }, { "carlsonRJ", 4, Fn::CarlsonRJ },
    { "bernoulli", 1, Fn::Bernoulli }, { "eulernumber", 1, Fn::EulerNumber }, { "tangentnumber", 1, Fn::TangentNumber }, { "secantnumber", 1, Fn::SecantNumber },
    { "eulerian", 2, Fn::Eulerian }, { "stieltjes", 1, Fn::Stieltjes }, { "metallic", 1, Fn::Metallic }, { "invmetallic", 1, Fn::InvMetallic },
    { "metallicmean", 1, Fn::MetallicMean }, { "beraha", 1, Fn::Beraha },
    { "fibonacci", 1, Fn::Fibonacci }, { "lucas", 1, Fn::Lucas }, { "fibonacci", 2, Fn::FibonacciPoly }, { "lucas", 2, Fn::LucasPoly },
    { "chebyshevT", 2, Fn::ChebyshevT }, { "chebyshevU", 2, Fn::ChebyshevU }, { "chebyshevV", 2, Fn::ChebyshevV }, { "chebyshevW", 2, Fn::ChebyshevW },
    { "legendreP", 2, Fn::LegendreP }, { "legendreQ", 2, Fn::LegendreQ },
    { "dirichletlambda", 1, Fn::DirichletLambda }, { "riemannchi", 1, Fn::RiemannChi }, { "legendrechi", 2, Fn::LegendreChi }, { "trilog", 1, Fn::Trilog },
    { "rogersL", 1, Fn::RogersL }, { "spence", 1, Fn::Spence }, { "lerch", 3, Fn::Lerch }, { "debye", 2, Fn::Debye },
    { "betainc", 3, Fn::BetaInc }, { "betareg", 3, Fn::BetaReg }, { "betaregc", 3, Fn::BetaRegC }, { "invbetareg", 3, Fn::InvBetaReg },
    { "invgammaP", 2, Fn::InvGammaP }, { "invgammaQ", 2, Fn::InvGammaQ }, { "falling", 2, Fn::Falling }, { "lbeta", 2, Fn::LogBeta },
    { "nsinc", 1, Fn::NSinc }, { "inverfi", 1, Fn::InvErfi }
};

struct Compiler {
    Program& p;
    std::string& error;
    std::size_t depth = 0;

    void push(Instr i, int delta) {
        p.code.push_back(i);
        if (delta > 0) {
            depth += static_cast<std::size_t>(delta);
            p.stack = std::max(p.stack, depth);
        } else {
            depth -= static_cast<std::size_t>(-delta);
        }
    }

    std::uint32_t constant(double v, const std::string& literal) {
        for (std::size_t i = 0; i < p.constants.size(); ++i) {
            if (p.constants[i].literal == literal && (p.constants[i].value == v || (std::isnan(v) && std::isnan(p.constants[i].value)))) return static_cast<std::uint32_t>(i);
        }
        p.constants.push_back(Constant{ v, literal });
        return static_cast<std::uint32_t>(p.constants.size() - 1);
    }

    bool emit(const ExprPtr& e) {
        if (!e) {
            error = "missing expression";
            return false;
        }
        switch (e->op) {
            case Op::Number:
                push(Instr{ Code::Const, Fn::Sin, constant(e->value, e->text) }, 1);
                return true;
            case Op::Imag:
                if (!p.complex) {
                    error = "i (the imaginary unit) needs a complex graph";
                    return false;
                }
                push(Instr{ Code::Imag, Fn::Sin, 0 }, 1);
                return true;
            case Op::Variable: {
                const auto it = std::find(p.slots.begin(), p.slots.end(), e->text);
                if (it != p.slots.end()) {
                    push(Instr{ Code::Load, Fn::Sin, static_cast<std::uint32_t>(it - p.slots.begin()) }, 1);
                    return true;
                }
                if (e->text == "pi") {
                    push(Instr{ Code::Const, Fn::Sin, constant(3.14159265358979323846, "@pi") }, 1);
                    return true;
                }
                if (e->text == "tau") {
                    push(Instr{ Code::Const, Fn::Sin, constant(6.28318530717958647692, "@tau") }, 1);
                    return true;
                }
                if (e->text == "e") {
                    push(Instr{ Code::Const, Fn::Sin, constant(2.71828182845904523536, "@e") }, 1);
                    return true;
                }
                if (e->text == "inf") {
                    push(Instr{ Code::Const, Fn::Sin, constant(INFINITY, "@inf") }, 1);
                    return true;
                }
                if (const LibEntry* k = library_constant(e->text)) {
                    if (k->literal != "@i") {
                        push(Instr{ Code::Const, Fn::Sin, constant(std::atof(k->value.c_str()), k->literal) }, 1);
                        return true;
                    }
                }
                error = e->text + " isn't defined";
                return false;
            }
            case Op::Neg:
                if (!emit(e->args[0])) return false;
                push(Instr{ Code::Neg, Fn::Sin, 0 }, 0);
                return true;
            case Op::Add:
            case Op::Sub:
            case Op::Mul:
            case Op::Div:
            case Op::Pow: {
                if (!emit(e->args[0]) || !emit(e->args[1])) return false;
                Code c = e->op == Op::Add ? Code::Add : e->op == Op::Sub ? Code::Sub : e->op == Op::Mul ? Code::Mul : e->op == Op::Div ? Code::Div : Code::Pow;
                push(Instr{ c, Fn::Sin, 0 }, -1);
                return true;
            }
            case Op::Call: {
                if (e->text == "if" && e->args.size() == 2) {
                    if (!emit(e->args[0]) || !emit(e->args[1])) return false;
                    push(Instr{ Code::Const, Fn::Sin, constant(std::nan(""), std::string()) }, 1);
                    push(Instr{ Code::Call3, Fn::If, 0 }, -2);
                    return true;
                }
                const int n = static_cast<int>(e->args.size());
                const FnName* found = nullptr;
                for (const FnName& f : k_fns) {
                    if (e->text == f.name && (f.args == n || ((e->text == "min" || e->text == "max") && n >= 2))) {
                        found = &f;
                        break;
                    }
                }
                if (!found) {
                    error = e->text + " isn't a function Arbor knows";
                    return false;
                }
                if (!p.complex && (found->fn == Fn::Re || found->fn == Fn::Im || found->fn == Fn::Arg || found->fn == Fn::Conj)) {
                    if (found->fn == Fn::Re || found->fn == Fn::Conj) return emit(e->args[0]);
                    if (found->fn == Fn::Im) {
                        push(Instr{ Code::Const, Fn::Sin, constant(0.0, std::string()) }, 1);
                        return true;
                    }
                }
                if (found->args == 1) {
                    if (!emit(e->args[0])) return false;
                    push(Instr{ Code::Call1, found->fn, 0 }, 0);
                    return true;
                }
                if (found->args == 3) {
                    for (int i = 0; i < 3; ++i) {
                        if (!emit(e->args[static_cast<std::size_t>(i)])) return false;
                    }
                    push(Instr{ Code::Call3, found->fn, 0 }, -2);
                    return true;
                }
                if (found->args == 4) {
                    for (int i = 0; i < 4; ++i) {
                        if (!emit(e->args[static_cast<std::size_t>(i)])) return false;
                    }
                    push(Instr{ Code::Call4, found->fn, 0 }, -3);
                    return true;
                }
                if (!emit(e->args[0])) return false;
                for (int i = 1; i < n; ++i) {
                    if (!emit(e->args[static_cast<std::size_t>(i)])) return false;
                    push(Instr{ Code::Call2, found->fn, 0 }, -1);
                }
                return true;
            }
            case Op::Vector:
                error = "a vector can't be used here; give one value";
                return false;
        }
        error = "unknown expression";
        return false;
    }
};

} // namespace

namespace {

thread_local int t_compile_branch = 0;

} // namespace

void set_compile_branch(int branch) {
    t_compile_branch = branch;
}

int compile_branch() {
    return t_compile_branch;
}

Program compile(const ExprPtr& e, const std::vector<std::string>& slots, bool complex, std::string& error) {
    Program p;
    p.slots = slots;
    p.complex = complex;
    p.branch = t_compile_branch;
    Compiler c{ p, error };
    p.valid = c.emit(e);
    if (!p.valid) p.code.clear();
    return p;
}

const std::vector<NumberType>& all_number_types() {
    static const std::vector<NumberType> all = { NumberType::Hardware, NumberType::ShortShortFloat, NumberType::ShortFloat, NumberType::Float, NumberType::LongFloat, NumberType::LongLongFloat, NumberType::LongDouble, NumberType::LongLongDouble };
    return all;
}

const std::vector<NumberType>& selectable_number_types() {
    static const std::vector<NumberType> all = { NumberType::ShortShortFloat, NumberType::ShortFloat, NumberType::Float, NumberType::LongFloat, NumberType::LongLongFloat, NumberType::LongDouble, NumberType::LongLongDouble };
    return all;
}

const char* number_type_id(NumberType t) {
    switch (t) {
        case NumberType::Hardware: return "hardware";
        case NumberType::ShortShortFloat: return "short-short-float";
        case NumberType::ShortFloat: return "short-float";
        case NumberType::Float: return "float";
        case NumberType::LongFloat: return "long-float";
        case NumberType::LongLongFloat: return "long-long-float";
        case NumberType::LongDouble: return "long-double";
        case NumberType::LongLongDouble: return "long-long-double";
    }
    return "hardware";
}

const char* number_type_label(NumberType t) {
    switch (t) {
        case NumberType::Hardware: return "double (hardware 64-bit)";
        case NumberType::ShortShortFloat: return "short short float (32-bit)";
        case NumberType::ShortFloat: return "short float (64-bit)";
        case NumberType::Float: return "float / short short double (128-bit)";
        case NumberType::LongFloat: return "long float / short double (256-bit)";
        case NumberType::LongLongFloat: return "long long float / double (512-bit)";
        case NumberType::LongDouble: return "long double (1024-bit)";
        case NumberType::LongLongDouble: return "long long double (2048-bit)";
    }
    return "";
}

NumberType number_type_from_id(const std::string& id) {
    for (NumberType t : all_number_types()) {
        if (id == number_type_id(t)) return t;
    }
    if (id == "double") return NumberType::LongLongFloat;
    if (id == "short-double" || id == "short-short-double") return id == "short-double" ? NumberType::LongFloat : NumberType::Float;
    return NumberType::Hardware;
}

int number_type_digits(NumberType t) {
    switch (t) {
        case NumberType::Hardware: return 15;
        case NumberType::ShortShortFloat: return 8;
        case NumberType::ShortFloat: return 16;
        case NumberType::Float: return 34;
        case NumberType::LongFloat: return 71;
        case NumberType::LongLongFloat: return 148;
        case NumberType::LongDouble: return 301;
        case NumberType::LongLongDouble: return 607;
    }
    return 15;
}

double Evaluator::real1(std::initializer_list<double> in) {
    std::vector<double> vals(in);
    std::vector<const double*> ptrs;
    for (const double& v : vals) ptrs.push_back(&v);
    double out = 0.0;
    real(1, ptrs.data(), &out);
    return out;
}

std::unique_ptr<Evaluator> make_evaluator(const Program& p, NumberType type) {
    if (type == NumberType::Hardware) return make_hardware_evaluator(p);
    return make_fizmo_evaluator(p, type);
}

std::string format_double(double v, int digits) {
    if (std::isnan(v)) return "undefined";
    if (std::isinf(v)) return v > 0 ? "\xE2\x88\x9E" : "-\xE2\x88\x9E";
    if (v == 0.0) return "0";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*g", digits, v);
    std::string s = buf;
    const std::size_t e = s.find('e');
    std::string mant = e == std::string::npos ? s : s.substr(0, e);
    std::string ex = e == std::string::npos ? std::string() : s.substr(e);
    if (mant.find('.') != std::string::npos) {
        while (!mant.empty() && mant.back() == '0') mant.pop_back();
        if (!mant.empty() && mant.back() == '.') mant.pop_back();
    }
    if (!ex.empty()) {
        int power = std::atoi(ex.c_str() + 1);
        ex = "e" + std::to_string(power);
    }
    return mant + ex;
}

} // namespace arbor
