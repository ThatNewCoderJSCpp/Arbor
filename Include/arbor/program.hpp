#ifndef ARBOR_PROGRAM_HPP
#define ARBOR_PROGRAM_HPP

#include "expr.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace arbor {

enum class Fn : std::uint8_t {
    Sin, Cos, Tan, Sec, Csc, Cot,
    Asin, Acos, Atan, Asec, Acsc, Acot, Atan2,
    Sinh, Cosh, Tanh, Sech, Csch, Coth,
    Asinh, Acosh, Atanh,
    Exp, Ln, Log10, Log2, LogBase,
    Sqrt, Cbrt, NthRoot,
    Abs, Sign, Floor, Ceil, Round,
    Mod, Min, Max, Hypot, Gamma, Erf,
    Re, Im, Arg, Conj,
    If, Lt, Le, Gt, Ge, Eq, Ne, And, Or, Not,
    Sinc, Gd, Expm1, Log1p,
    LambertW, LambertWk,
    LogGamma, Factorial, Digamma, Trigamma, Polygamma, Beta, Binomial, Pochhammer, Harmonic,
    GammaP, GammaQ, LowerGamma, UpperGamma,
    Zeta, HurwitzZeta, DirichletEta, DirichletBeta, RiemannXi, Polylog, Dilog, Clausen,
    Erfc, Erfi, InvErf, InvErfc, Si, Ci, Shi, Chi, Ei, Li,
    FresnelS, FresnelC, AiryAi, AiryBi,
    EllipticK, EllipticE, EllipticE2, EllipticF, EllipticPi, JacobiSn, JacobiCn, JacobiDn, JacobiAm,
    Asech, Acsch, Acoth, InvGd, InvSinc, Mag, Mag2, LogAbsGamma, ClausenS, ExpIntN, LogIntN, ScorerHi, ScorerGi,
    JacobiCd, JacobiCs, JacobiDc, JacobiDs, JacobiNc, JacobiNd, JacobiNs, JacobiSc, JacobiSd,
    EllipticPi3, CarlsonRC, CarlsonRD, CarlsonRF, CarlsonRG, CarlsonRJ,
    Bernoulli, EulerNumber, TangentNumber, SecantNumber, Eulerian, Stieltjes, Metallic, InvMetallic, MetallicMean, Beraha,
    Fibonacci, Lucas, FibonacciPoly, LucasPoly, ChebyshevT, ChebyshevU, ChebyshevV, ChebyshevW, LegendreP, LegendreQ,
    DirichletLambda, RiemannChi, LegendreChi, Trilog, RogersL, Spence, Lerch, Debye,
    BetaInc, BetaReg, BetaRegC, InvBetaReg, InvGammaP, InvGammaQ, Falling, LogBeta, NSinc, InvErfi
};

enum class Code : std::uint8_t {
    Const,
    Load,
    Imag,
    Add,
    Sub,
    Mul,
    Div,
    Pow,
    Neg,
    Call1,
    Call2,
    Call3,
    Call4,
    Reduce
};

struct Instr {
    Code code = Code::Const;
    Fn fn = Fn::Sin;
    std::uint32_t arg = 0;
};

struct Constant {
    double value = 0.0;
    std::string literal;
};

enum class ReduceKind : std::uint8_t {
    Sum,
    Prod,
    Integral,
    Max,
    Min,
    ArgMax,
    ArgMin
};

struct Program;

struct Reduction {
    ReduceKind kind = ReduceKind::Sum;
    std::shared_ptr<Program> body;
};

struct Program {
    std::vector<Reduction> reductions;
    std::vector<Instr> code;
    std::vector<Constant> constants;
    std::vector<std::string> slots;
    std::size_t stack = 0;
    bool complex = false;
    bool valid = false;
    int branch = 0;
};

Program compile(const ExprPtr& e, const std::vector<std::string>& slots, bool complex, std::string& error);

void set_compile_branch(int branch);
int compile_branch();

class CompileBranch {
public:
    explicit CompileBranch(int branch) : m_saved(compile_branch()) { set_compile_branch(branch); }
    ~CompileBranch() { set_compile_branch(m_saved); }
    CompileBranch(const CompileBranch&) = delete;
    CompileBranch& operator=(const CompileBranch&) = delete;

private:
    int m_saved;
};

enum class NumberType {
    Hardware,
    ShortShortFloat,
    ShortFloat,
    Float,
    LongFloat,
    LongLongFloat,
    LongDouble,
    LongLongDouble
};

const char* number_type_id(NumberType t);
const char* number_type_label(NumberType t);
NumberType number_type_from_id(const std::string& id);
int number_type_digits(NumberType t);
const std::vector<NumberType>& all_number_types();
const std::vector<NumberType>& selectable_number_types();

class Evaluator {
public:
    virtual ~Evaluator() = default;
    virtual void real(std::size_t n, const double* const* in, double* out) = 0;
    virtual void complex(std::size_t n, const double* const* in_re, const double* const* in_im, double* out_re, double* out_im) = 0;
    virtual std::string text(const std::vector<double>& in, int digits) = 0;
    virtual std::string complex_text(const std::vector<double>& in_re, const std::vector<double>& in_im, int digits) = 0;

    double real1(std::initializer_list<double> in);
};

std::unique_ptr<Evaluator> make_evaluator(const Program& p, NumberType type);
std::unique_ptr<Evaluator> make_hardware_evaluator(const Program& p);
std::unique_ptr<Evaluator> make_fizmo_evaluator(const Program& p, NumberType type);

std::string format_double(double v, int digits = 6);

} // namespace arbor

#endif // ARBOR_PROGRAM_HPP
