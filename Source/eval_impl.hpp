#ifndef ARBOR_EVAL_IMPL_HPP
#define ARBOR_EVAL_IMPL_HPP

#include "arbor/program.hpp"
#include "special_double.hpp"

#include <cmath>
#include <complex>
#include <string>
#include <vector>

namespace arbor {
namespace detail {

inline thread_local int t_branch = 0;

double fizmo_special_double(Fn fn, const double* args, int n);

inline bool truthy_double(double v) {
    return !std::isnan(v) && v != 0.0;
}

class BranchGuard {
public:
    explicit BranchGuard(int branch) : m_saved(t_branch) { t_branch = branch; }
    ~BranchGuard() { t_branch = m_saved; }
    BranchGuard(const BranchGuard&) = delete;
    BranchGuard& operator=(const BranchGuard&) = delete;

private:
    int m_saved;
};

inline bool branch_odd(int k) {
    return k % 2 != 0;
}

template <class T, class N>
T times_pi(int k) {
    return N::mul(N::from_double(static_cast<double>(k)), N::pi());
}

template <class T, class N>
T real_asin_branch(const T& principal) {
    const int k = t_branch;
    if (k == 0) return principal;
    return N::add(branch_odd(k) ? N::neg(principal) : principal, times_pi<T, N>(k));
}

template <class T, class N>
T real_acos_branch(const T& principal) {
    const int k = t_branch;
    if (k == 0) return principal;
    if (branch_odd(k)) return N::add(N::neg(principal), times_pi<T, N>(k + 1));
    return N::add(principal, times_pi<T, N>(k));
}

template <class T, class N>
T real_atan_branch(const T& principal) {
    const int k = t_branch;
    if (k == 0) return principal;
    return N::add(principal, times_pi<T, N>(k));
}

template <class T, class N>
T real_sign_branch(const T& principal) {
    return branch_odd(t_branch) ? N::neg(principal) : principal;
}

template <class T, class N>
T real_pow(const T& b, const T& x) {
    if (N::lt(b, N::zero())) {
        const double xd = N::to_double(x);
        if (std::floor(xd) == xd && std::fabs(xd) < 9.0e15) {
            T r = N::pow(N::neg(b), x);
            return std::fmod(std::fabs(xd), 2.0) == 1.0 ? N::neg(r) : r;
        }
        for (int q = 3; q <= 15; q += 2) {
            const double r = xd * q;
            const double ri = std::round(r);
            if (std::fabs(r - ri) < 1e-9) {
                T mag = N::pow(N::neg(b), x);
                return std::fmod(std::fabs(ri), 2.0) == 1.0 ? N::neg(mag) : mag;
            }
        }
        return N::nan();
    }
    if (N::is_zero(b)) {
        if (N::gt(x, N::zero())) return N::zero();
        if (N::is_zero(x)) return N::one();
        return N::inf();
    }
    return N::pow(b, x);
}

template <class T, class N>
T real_inv_sinc(const T& y) {
    const double yd = N::to_double(y);
    const double lowest = -0.21723362821122166;
    if (std::isnan(yd) || yd > 1.0 || yd < lowest) return N::nan();
    if (yd == 1.0) return N::zero();
    double lo = 0.0;
    double hi = 4.4934094579090642;
    for (int i = 0; i < 70; ++i) {
        const double mid = 0.5 * (lo + hi);
        const double v = std::sin(mid) / mid;
        if (v > yd) lo = mid;
        else hi = mid;
    }
    T x = N::from_double(0.5 * (lo + hi));
    if (yd < lowest + 1e-9) return x;
    for (int it = 0; it < 40; ++it) {
        const T s = N::sin(x);
        const T c = N::cos(x);
        const T f = N::sub(N::div(s, x), y);
        const T df = N::div(N::sub(N::mul(x, c), s), N::mul(x, x));
        if (N::is_zero(df) || N::is_nan(df)) break;
        const T step = N::div(f, df);
        x = N::sub(x, step);
        const double rel = std::fabs(N::to_double(step)) / std::max(1e-300, std::fabs(N::to_double(x)));
        if (rel < 1e-300 || N::is_zero(step)) break;
        if (it > 3 && rel == 0.0) break;
    }
    return x;
}

template <class T, class N>
T real_call1(Fn fn, const T& a) {
    switch (fn) {
        case Fn::Asech: return N::gt(a, N::zero()) && !N::gt(a, N::one()) ? N::acosh(N::div(N::one(), a)) : N::nan();
        case Fn::Acsch: return N::is_zero(a) ? N::nan() : N::asinh(N::div(N::one(), a));
        case Fn::Acoth: return N::gt(N::abs(a), N::one()) ? N::atanh(N::div(N::one(), a)) : N::nan();
        case Fn::Mag: return N::abs(a);
        case Fn::Mag2: return N::mul(a, a);
        case Fn::InvSinc: return real_inv_sinc<T, N>(a);
        case Fn::Sin: return N::sin(a);
        case Fn::Cos: return N::cos(a);
        case Fn::Tan: return N::tan(a);
        case Fn::Sec: return N::div(N::one(), N::cos(a));
        case Fn::Csc: return N::div(N::one(), N::sin(a));
        case Fn::Cot: return N::div(N::cos(a), N::sin(a));
        case Fn::Asin: return real_asin_branch<T, N>(N::asin(a));
        case Fn::Acos: return real_acos_branch<T, N>(N::acos(a));
        case Fn::Atan: return real_atan_branch<T, N>(N::atan(a));
        case Fn::Asec: return real_acos_branch<T, N>(N::acos(N::div(N::one(), a)));
        case Fn::Acsc: return real_asin_branch<T, N>(N::asin(N::div(N::one(), a)));
        case Fn::Acot: return real_atan_branch<T, N>(N::atan(N::div(N::one(), a)));
        case Fn::Sinh: return N::sinh(a);
        case Fn::Cosh: return N::cosh(a);
        case Fn::Tanh: return N::tanh(a);
        case Fn::Sech: return N::div(N::one(), N::cosh(a));
        case Fn::Csch: return N::div(N::one(), N::sinh(a));
        case Fn::Coth: return N::div(N::cosh(a), N::sinh(a));
        case Fn::Asinh: return N::asinh(a);
        case Fn::Acosh: return real_sign_branch<T, N>(N::acosh(a));
        case Fn::Atanh: return N::atanh(a);
        case Fn::Exp: return N::exp(a);
        case Fn::Ln: return N::gt(a, N::zero()) ? N::ln(a) : (N::is_zero(a) ? N::neg(N::inf()) : N::nan());
        case Fn::Log10: return N::gt(a, N::zero()) ? N::log10(a) : (N::is_zero(a) ? N::neg(N::inf()) : N::nan());
        case Fn::Log2: return N::gt(a, N::zero()) ? N::log2(a) : (N::is_zero(a) ? N::neg(N::inf()) : N::nan());
        case Fn::Sqrt: return N::lt(a, N::zero()) ? N::nan() : real_sign_branch<T, N>(N::sqrt(a));
        case Fn::Cbrt: return N::cbrt(a);
        case Fn::Abs: return N::abs(a);
        case Fn::Sign: return N::gt(a, N::zero()) ? N::one() : (N::lt(a, N::zero()) ? N::neg(N::one()) : N::zero());
        case Fn::Floor: return N::floor(a);
        case Fn::Ceil: return N::ceil(a);
        case Fn::Round: return N::floor(N::add(a, N::half()));
        case Fn::Gamma: return N::gamma(a);
        case Fn::Erf: return N::erf(a);
        case Fn::Re: return a;
        case Fn::Conj: return a;
        case Fn::Im: return N::zero();
        case Fn::Arg: return N::lt(a, N::zero()) ? N::pi() : N::zero();
        case Fn::Not: return N::is_nan(a) ? N::nan() : (N::is_zero(a) ? N::one() : N::zero());
        default: {
            const T args[1] = { a };
            return N::special(fn, args, 1);
        }
    }
}

template <class T, class N>
T real_call2(Fn fn, const T& a, const T& b) {
    switch (fn) {
        case Fn::Atan2: return N::atan2(a, b);
        case Fn::LogBase: return N::div(real_call1<T, N>(Fn::Ln, a), real_call1<T, N>(Fn::Ln, b));
        case Fn::NthRoot: {
            const T r = real_pow<T, N>(a, N::div(N::one(), b));
            const double n = N::to_double(b);
            if (std::floor(n) == n && std::fmod(std::fabs(n), 2.0) == 0.0) return real_sign_branch<T, N>(r);
            return r;
        }
        case Fn::Mod: return N::sub(a, N::mul(b, N::floor(N::div(a, b))));
        case Fn::Min: return N::lt(b, a) ? b : a;
        case Fn::Max: return N::gt(b, a) ? b : a;
        case Fn::Hypot: return N::sqrt(N::add(N::mul(a, a), N::mul(b, b)));
        case Fn::Lt: return N::is_nan(a) || N::is_nan(b) ? N::nan() : (N::lt(a, b) ? N::one() : N::zero());
        case Fn::Le: return N::is_nan(a) || N::is_nan(b) ? N::nan() : (!N::gt(a, b) ? N::one() : N::zero());
        case Fn::Gt: return N::is_nan(a) || N::is_nan(b) ? N::nan() : (N::gt(a, b) ? N::one() : N::zero());
        case Fn::Ge: return N::is_nan(a) || N::is_nan(b) ? N::nan() : (!N::lt(a, b) ? N::one() : N::zero());
        case Fn::Eq: return N::is_nan(a) || N::is_nan(b) ? N::nan() : (!N::lt(a, b) && !N::gt(a, b) ? N::one() : N::zero());
        case Fn::Ne: return N::is_nan(a) || N::is_nan(b) ? N::nan() : (N::lt(a, b) || N::gt(a, b) ? N::one() : N::zero());
        case Fn::And: return N::is_nan(a) || N::is_nan(b) ? N::nan() : (!N::is_zero(a) && !N::is_zero(b) ? N::one() : N::zero());
        case Fn::Or: return N::is_nan(a) || N::is_nan(b) ? N::nan() : (!N::is_zero(a) || !N::is_zero(b) ? N::one() : N::zero());
        default: {
            const T args[2] = { a, b };
            return N::special(fn, args, 2);
        }
    }
}

template <class T, class N>
T real_call3(Fn fn, const T& a, const T& b, const T& c) {
    if (fn == Fn::If) {
        if (N::is_nan(a)) return N::nan();
        return N::is_zero(a) ? c : b;
    }
    const T args[3] = { a, b, c };
    return N::special(fn, args, 3);
}

template <class T, class N>
T real_call4(Fn fn, const T& a, const T& b, const T& c, const T& d) {
    const T args[4] = { a, b, c, d };
    return N::special(fn, args, 4);
}

struct DoubleNum {
    using T = double;
    static T from_double(double v) { return v; }
    static T from_literal(const std::string& s, double v) {
        (void)s;
        return v;
    }
    static double to_double(T v) { return v; }
    static std::string text(T v, int digits) { return format_double(v, digits); }
    static T zero() { return 0.0; }
    static T one() { return 1.0; }
    static T half() { return 0.5; }
    static T nan() { return std::nan(""); }
    static T inf() { return INFINITY; }
    static T pi() { return 3.14159265358979323846; }
    static T e() { return 2.71828182845904523536; }
    static T tau() { return 6.28318530717958647692; }
    static bool lt(T a, T b) { return a < b; }
    static bool gt(T a, T b) { return a > b; }
    static bool is_zero(T a) { return a == 0.0; }
    static bool is_nan(T a) { return std::isnan(a); }
    static T add(T a, T b) { return a + b; }
    static T sub(T a, T b) { return a - b; }
    static T mul(T a, T b) { return a * b; }
    static T div(T a, T b) { return a / b; }
    static T neg(T a) { return -a; }
    static T pow(T a, T b) { return std::pow(a, b); }
    static T sin(T a) { return std::sin(a); }
    static T cos(T a) { return std::cos(a); }
    static T tan(T a) { return std::tan(a); }
    static T asin(T a) { return std::asin(a); }
    static T acos(T a) { return std::acos(a); }
    static T atan(T a) { return std::atan(a); }
    static T atan2(T a, T b) { return std::atan2(a, b); }
    static T sinh(T a) { return std::sinh(a); }
    static T cosh(T a) { return std::cosh(a); }
    static T tanh(T a) { return std::tanh(a); }
    static T asinh(T a) { return std::asinh(a); }
    static T acosh(T a) { return std::acosh(a); }
    static T atanh(T a) { return std::atanh(a); }
    static T exp(T a) { return std::exp(a); }
    static T ln(T a) { return std::log(a); }
    static T log10(T a) { return std::log10(a); }
    static T log2(T a) { return std::log2(a); }
    static T sqrt(T a) { return std::sqrt(a); }
    static T cbrt(T a) { return std::cbrt(a); }
    static T abs(T a) { return std::fabs(a); }
    static T floor(T a) { return std::floor(a); }
    static T ceil(T a) { return std::ceil(a); }
    static T gamma(T a) { return std::tgamma(a); }
    static T erf(T a) { return std::erf(a); }
    static T named(const std::string& literal) {
        if (literal == "@golden") return 1.6180339887498948482;
        if (literal == "@euler") return 0.57721566490153286061;
        if (literal == "@catalan") return 0.91596559417721901505;
        if (literal == "@apery") return 1.2020569031595942854;
        if (literal == "@silver") return 2.4142135623730950488;
        if (literal == "@sqrt2") return 1.4142135623730950488;
        if (literal == "@ln2") return 0.69314718055994530942;
        if (literal == "@psi") return 0.61803398874989484820;
        if (literal == "@silverconstant") return 3.2469796037174670611;
        if (literal == "@bronze") return 3.3027756377319946466;
        return std::nan("");
    }
    static T special(Fn fn, const T* a, int n) {
        switch (fn) {
            case Fn::Sinc: return a[0] == 0.0 ? 1.0 : std::sin(a[0]) / a[0];
            case Fn::Gd: return std::atan(std::sinh(a[0]));
            case Fn::Expm1: return std::expm1(a[0]);
            case Fn::Log1p: return a[0] < -1.0 ? std::nan("") : std::log1p(a[0]);
            case Fn::LambertW: return sd::lambertw(a[0], t_branch);
            case Fn::LambertWk: return sd::lambertw(a[0], static_cast<int>(std::lround(a[1])));
            case Fn::LogGamma: {
                if (a[0] <= 0.0 && std::floor(a[0]) == a[0]) return INFINITY;
                double sign = 1.0;
                const double v = sd::log_abs_gamma_sign(a[0], sign);
                return sign < 0.0 ? std::nan("") : v;
            }
            case Fn::LogAbsGamma: return (a[0] <= 0.0 && std::floor(a[0]) == a[0]) ? INFINITY : std::lgamma(a[0]);
            case Fn::InvGd: return std::fabs(a[0]) > 1.5707963267948966 ? std::nan("") : std::atanh(std::sin(a[0]));
            case Fn::NSinc: return a[0] == 0.0 ? 1.0 : std::sin(3.14159265358979323846 * a[0]) / (3.14159265358979323846 * a[0]);
            case Fn::Factorial: return std::tgamma(a[0] + 1.0);
            case Fn::Digamma: return sd::digamma(a[0]);
            case Fn::Trigamma: return sd::trigamma(a[0]);
            case Fn::Polygamma:
                if (a[0] == 0.0) return sd::digamma(a[1]);
                if (a[0] == 1.0) return sd::trigamma(a[1]);
                break;
            case Fn::Beta: return sd::beta(a[0], a[1]);
            case Fn::Binomial: return sd::binomial(a[0], a[1]);
            case Fn::Pochhammer: return sd::pochhammer(a[0], a[1]);
            case Fn::Harmonic: return sd::digamma(a[0] + 1.0) + sd::k_euler;
            case Fn::Zeta: return sd::zeta(a[0]);
            case Fn::DirichletEta: return sd::dirichlet_eta(sd::cd(a[0], 0.0)).real();
            case Fn::Erfc: return std::erfc(a[0]);
            default: break;
        }
        return fizmo_special_double(fn, a, n);
    }
};

template <class N>
struct RealOps {
    using V = typename N::T;
    static V constant(const Constant& c) {
        if (c.literal == "@pi") return N::pi();
        if (c.literal == "@e") return N::e();
        if (c.literal == "@tau") return N::tau();
        if (c.literal == "@inf") return N::inf();
        if (!c.literal.empty() && c.literal[0] == '@') return N::named(c.literal);
        if (!c.literal.empty()) return N::from_literal(c.literal, c.value);
        return N::from_double(c.value);
    }
    static V imag() { return N::nan(); }
    static V add(const V& a, const V& b) { return N::add(a, b); }
    static V sub(const V& a, const V& b) { return N::sub(a, b); }
    static V mul(const V& a, const V& b) { return N::mul(a, b); }
    static V div(const V& a, const V& b) { return N::div(a, b); }
    static V pow(const V& a, const V& b) { return real_pow<V, N>(a, b); }
    static V neg(const V& a) { return N::neg(a); }
    static V call1(Fn f, const V& a) { return real_call1<V, N>(f, a); }
    static V call2(Fn f, const V& a, const V& b) { return real_call2<V, N>(f, a, b); }
    static V call3(Fn f, const V& a, const V& b, const V& c) { return real_call3<V, N>(f, a, b, c); }
    static V call4(Fn f, const V& a, const V& b, const V& c, const V& d) { return real_call4<V, N>(f, a, b, c, d); }
};

template <class T>
struct Cx {
    T re;
    T im;
};

template <class N>
struct ComplexOps {
    using T = typename N::T;
    using V = Cx<T>;
    static V make(const T& r, const T& i) { return V{ r, i }; }
    static V real(const T& r) { return V{ r, N::zero() }; }
    static V constant(const Constant& c) { return real(RealOps<N>::constant(c)); }
    static V imag() { return V{ N::zero(), N::one() }; }
    static V nan() { return V{ N::nan(), N::nan() }; }
    static V add(const V& a, const V& b) { return V{ N::add(a.re, b.re), N::add(a.im, b.im) }; }
    static V sub(const V& a, const V& b) { return V{ N::sub(a.re, b.re), N::sub(a.im, b.im) }; }
    static V mul(const V& a, const V& b) {
        return V{ N::sub(N::mul(a.re, b.re), N::mul(a.im, b.im)), N::add(N::mul(a.re, b.im), N::mul(a.im, b.re)) };
    }
    static V div(const V& a, const V& b) {
        const T d = N::add(N::mul(b.re, b.re), N::mul(b.im, b.im));
        return V{ N::div(N::add(N::mul(a.re, b.re), N::mul(a.im, b.im)), d), N::div(N::sub(N::mul(a.im, b.re), N::mul(a.re, b.im)), d) };
    }
    static V neg(const V& a) { return V{ N::neg(a.re), N::neg(a.im) }; }
    static T modulus(const V& a) { return N::sqrt(N::add(N::mul(a.re, a.re), N::mul(a.im, a.im))); }
    static T argument(const V& a) { return N::atan2(a.im, a.re); }
    static bool is_real(const V& a) { return N::is_zero(a.im); }
    static V exp(const V& a) {
        const T m = N::exp(a.re);
        return V{ N::mul(m, N::cos(a.im)), N::mul(m, N::sin(a.im)) };
    }
    static V ln(const V& a) {
        const T m = modulus(a);
        if (N::is_zero(m)) return V{ N::neg(N::inf()), N::zero() };
        if (t_branch != 0) return V{ N::ln(m), N::add(argument(a), N::mul(N::from_double(static_cast<double>(t_branch)), N::tau())) };
        return V{ N::ln(m), argument(a) };
    }
    static V principal_sqrt(const V& a) {
        if (is_real(a) && !N::lt(a.re, N::zero())) return real(N::sqrt(a.re));
        if (is_real(a)) return V{ N::zero(), N::sqrt(N::neg(a.re)) };
        const T m = modulus(a);
        const T r = N::sqrt(N::mul(N::half(), N::add(m, a.re)));
        T i = N::sqrt(N::mul(N::half(), N::sub(m, a.re)));
        if (N::lt(a.im, N::zero())) i = N::neg(i);
        return V{ r, i };
    }
    static V sqrt(const V& a) {
        const V r = principal_sqrt(a);
        return branch_odd(t_branch) ? neg(r) : r;
    }
    static V plus_pi(const V& a, int k, bool imaginary) {
        const T shift = times_pi<T, N>(k);
        return imaginary ? V{ a.re, N::add(a.im, shift) } : V{ N::add(a.re, shift), a.im };
    }
    static V odd_branch(const V& principal, bool imaginary) {
        const int k = t_branch;
        if (k == 0) return principal;
        return plus_pi(branch_odd(k) ? neg(principal) : principal, k, imaginary);
    }
    static V shift_branch(const V& principal, bool imaginary) {
        const int k = t_branch;
        if (k == 0) return principal;
        return plus_pi(principal, k, imaginary);
    }
    static V pair_branch(const V& principal, bool imaginary) {
        const int k = t_branch;
        if (k == 0) return principal;
        if (branch_odd(k)) return plus_pi(neg(principal), k + 1, imaginary);
        return plus_pi(principal, k, imaginary);
    }
    static V int_pow(V base, long long n) {
        bool inv = n < 0;
        unsigned long long k = static_cast<unsigned long long>(inv ? -n : n);
        V r = real(N::one());
        while (k) {
            if (k & 1u) r = mul(r, base);
            base = mul(base, base);
            k >>= 1u;
        }
        return inv ? div(real(N::one()), r) : r;
    }
    static V pow(const V& a, const V& b) {
        if (is_real(b)) {
            const double bd = N::to_double(b.re);
            if (std::floor(bd) == bd && std::fabs(bd) <= 64.0) return int_pow(a, static_cast<long long>(bd));
            if (t_branch == 0 && is_real(a) && !N::lt(a.re, N::zero())) return real(N::is_zero(a.re) ? (N::gt(b.re, N::zero()) ? N::zero() : N::inf()) : N::pow(a.re, b.re));
        }
        if (N::is_zero(a.re) && N::is_zero(a.im)) return N::gt(b.re, N::zero()) ? real(N::zero()) : nan();
        return exp(mul(b, ln(a)));
    }
    static V sin(const V& a) { return V{ N::mul(N::sin(a.re), N::cosh(a.im)), N::mul(N::cos(a.re), N::sinh(a.im)) }; }
    static V cos(const V& a) { return V{ N::mul(N::cos(a.re), N::cosh(a.im)), N::neg(N::mul(N::sin(a.re), N::sinh(a.im))) }; }
    static V sinh(const V& a) { return V{ N::mul(N::sinh(a.re), N::cos(a.im)), N::mul(N::cosh(a.re), N::sin(a.im)) }; }
    static V cosh(const V& a) { return V{ N::mul(N::cosh(a.re), N::cos(a.im)), N::mul(N::sinh(a.re), N::sin(a.im)) }; }
    static V one() { return real(N::one()); }
    static V i_times(const V& a) { return V{ N::neg(a.im), a.re }; }
    static V principal_asin(const V& z) {
        BranchGuard g(0);
        return neg(i_times(ln(add(i_times(z), sqrt(sub(one(), mul(z, z)))))));
    }
    static V principal_acos(const V& z) { return sub(real(N::mul(N::half(), N::pi())), principal_asin(z)); }
    static V principal_atan(const V& z) {
        BranchGuard g(0);
        const V a = ln(sub(one(), i_times(z)));
        const V b = ln(add(one(), i_times(z)));
        return mul(V{ N::zero(), N::half() }, sub(a, b));
    }
    static V asin(const V& z) { return odd_branch(principal_asin(z), false); }
    static V acos(const V& z) { return pair_branch(principal_acos(z), false); }
    static V atan(const V& z) { return shift_branch(principal_atan(z), false); }
    static V asinh(const V& z) {
        V w;
        {
            BranchGuard g(0);
            w = ln(add(z, sqrt(add(mul(z, z), one()))));
        }
        return odd_branch(w, true);
    }
    static V acosh(const V& z) {
        V w;
        {
            BranchGuard g(0);
            w = ln(add(z, mul(sqrt(add(z, one())), sqrt(sub(z, one())))));
        }
        return pair_branch(w, true);
    }
    static V atanh(const V& z) {
        V w;
        {
            BranchGuard g(0);
            w = mul(real(N::half()), sub(ln(add(one(), z)), ln(sub(one(), z))));
        }
        return shift_branch(w, true);
    }
    static V recip(const V& a) { return div(one(), a); }
    static V gamma(const V& z) {
        BranchGuard g(0);
        if (is_real(z)) return real(N::gamma(z.re));
        return complex_gamma(z);
    }
    static V complex_gamma(V z) {
        static const double g = 7.0;
        static const double coef[9] = { 0.99999999999980993, 676.5203681218851, -1259.1392167224028, 771.32342877765313, -176.61502916214059, 12.507343278686905, -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7 };
        if (N::to_double(z.re) < 0.5) {
            const V pz = mul(real(N::pi()), z);
            return div(real(N::pi()), mul(sin(pz), complex_gamma(sub(one(), z))));
        }
        z = sub(z, one());
        V x = real(N::from_double(coef[0]));
        for (int i = 1; i < 9; ++i) x = add(x, div(real(N::from_double(coef[i])), add(z, real(N::from_double(static_cast<double>(i))))));
        const V t = add(z, real(N::from_double(g + 0.5)));
        const V sqrt2pi = real(N::sqrt(N::mul(N::from_double(2.0), N::pi())));
        return mul(mul(sqrt2pi, pow(t, add(z, real(N::half())))), mul(exp(neg(t)), x));
    }
    static V call1(Fn f, const V& a) {
        switch (f) {
            case Fn::Sin: return sin(a);
            case Fn::Cos: return cos(a);
            case Fn::Tan: return div(sin(a), cos(a));
            case Fn::Sec: return recip(cos(a));
            case Fn::Csc: return recip(sin(a));
            case Fn::Cot: return div(cos(a), sin(a));
            case Fn::Asin: return asin(a);
            case Fn::Acos: return acos(a);
            case Fn::Atan: return atan(a);
            case Fn::Asec: return acos(recip(a));
            case Fn::Acsc: return asin(recip(a));
            case Fn::Acot: return atan(recip(a));
            case Fn::Sinh: return sinh(a);
            case Fn::Cosh: return cosh(a);
            case Fn::Tanh: return div(sinh(a), cosh(a));
            case Fn::Sech: return recip(cosh(a));
            case Fn::Csch: return recip(sinh(a));
            case Fn::Coth: return div(cosh(a), sinh(a));
            case Fn::Asinh: return asinh(a);
            case Fn::Acosh: return acosh(a);
            case Fn::Atanh: return atanh(a);
            case Fn::Asech: return acosh(recip(a));
            case Fn::Acsch: return asinh(recip(a));
            case Fn::Acoth: return atanh(recip(a));
            case Fn::Mag: return real(modulus(a));
            case Fn::Mag2: return real(N::add(N::mul(a.re, a.re), N::mul(a.im, a.im)));
            case Fn::InvGd: {
                if (is_real(a)) {
                    const T args[1] = { a.re };
                    const T r = N::special(Fn::InvGd, args, 1);
                    if (!N::is_nan(r)) return real(r);
                }
                BranchGuard g(0);
                return atanh(sin(a));
            }
            case Fn::Exp: return exp(a);
            case Fn::Ln: return ln(a);
            case Fn::Log10: return div(ln(a), real(N::ln(N::from_double(10.0))));
            case Fn::Log2: return div(ln(a), real(N::ln(N::from_double(2.0))));
            case Fn::Sqrt: return sqrt(a);
            case Fn::Cbrt: {
                V r;
                {
                    BranchGuard g(0);
                    r = is_real(a) ? real(N::cbrt(a.re)) : pow(a, real(N::div(N::one(), N::from_double(3.0))));
                }
                const int turn = ((t_branch % 3) + 3) % 3;
                if (turn == 0) return r;
                const T angle = N::div(N::mul(N::from_double(static_cast<double>(turn)), N::tau()), N::from_double(3.0));
                return mul(r, V{ N::cos(angle), N::sin(angle) });
            }
            case Fn::Abs: return real(modulus(a));
            case Fn::Sign: {
                const T m = modulus(a);
                return N::is_zero(m) ? real(N::zero()) : V{ N::div(a.re, m), N::div(a.im, m) };
            }
            case Fn::Floor: return V{ N::floor(a.re), N::floor(a.im) };
            case Fn::Ceil: return V{ N::ceil(a.re), N::ceil(a.im) };
            case Fn::Round: return V{ N::floor(N::add(a.re, N::half())), N::floor(N::add(a.im, N::half())) };
            case Fn::Gamma: return gamma(a);
            case Fn::Erf: return is_real(a) ? real(N::erf(a.re)) : nan();
            case Fn::Re: return real(a.re);
            case Fn::Im: return real(a.im);
            case Fn::Arg: return real(argument(a));
            case Fn::Conj: return V{ a.re, N::neg(a.im) };
            case Fn::Not: return real(real_call1<T, N>(Fn::Not, a.re));
            default: {
                const V args[1] = { a };
                return special(f, args, 1);
            }
        }
    }
    static bool complex_capable(Fn f) {
        switch (f) {
            case Fn::Sinc: case Fn::Gd: case Fn::Expm1: case Fn::Log1p: case Fn::LambertW: case Fn::LambertWk:
            case Fn::LogGamma: case Fn::Factorial: case Fn::Digamma: case Fn::Beta: case Fn::Binomial: case Fn::Harmonic:
            case Fn::Zeta: case Fn::DirichletEta: case Fn::LogAbsGamma:
                return true;
            default:
                return false;
        }
    }
    static sd::cd to_cd(const V& v) { return sd::cd(N::to_double(v.re), N::to_double(v.im)); }
    static V from_cd(const sd::cd& c) { return V{ N::from_double(c.real()), N::from_double(c.imag()) }; }
    static V special(Fn f, const V* a, int n) {
        bool all_real = true;
        for (int k = 0; k < n; ++k) all_real = all_real && N::is_zero(a[k].im);
        if (all_real) {
            T ra[4];
            for (int k = 0; k < n && k < 4; ++k) ra[k] = a[k].re;
            const T r = N::special(f, ra, n);
            if (!N::is_nan(r) || !complex_capable(f)) return real(r);
        }
        if (!complex_capable(f)) return nan();
        const sd::cd z = to_cd(a[0]);
        const sd::cd w = n > 1 ? to_cd(a[1]) : sd::cd(0.0, 0.0);
        sd::cd r = sd::cnan();
        switch (f) {
            case Fn::Sinc: r = sd::sinc(z); break;
            case Fn::Gd: r = 2.0 * std::atan(std::tanh(0.5 * z)); break;
            case Fn::Expm1: r = std::exp(z) - 1.0; break;
            case Fn::Log1p: r = std::log(1.0 + z); break;
            case Fn::LambertW: r = sd::lambertw(z, t_branch); break;
            case Fn::LambertWk: r = sd::lambertw(z, static_cast<int>(std::lround(w.real()))); break;
            case Fn::LogGamma: r = sd::lgamma(z); break;
            case Fn::LogAbsGamma: r = sd::cd(sd::lgamma(z).real(), 0.0); break;
            case Fn::Factorial: r = sd::gamma(z + 1.0); break;
            case Fn::Digamma: r = sd::digamma(z); break;
            case Fn::Beta: r = std::exp(sd::lgamma(z) + sd::lgamma(w) - sd::lgamma(z + w)); break;
            case Fn::Binomial: r = std::exp(sd::lgamma(z + 1.0) - sd::lgamma(w + 1.0) - sd::lgamma(z - w + 1.0)); break;
            case Fn::Harmonic: r = sd::digamma(z + 1.0) + sd::k_euler; break;
            case Fn::Zeta: r = sd::zeta(z); break;
            case Fn::DirichletEta: r = sd::dirichlet_eta(z); break;
            default: break;
        }
        return from_cd(r);
    }
    static V call3(Fn f, const V& a, const V& b, const V& c) {
        if (f == Fn::If) {
            if (N::is_nan(a.re)) return nan();
            return N::is_zero(a.re) && N::is_zero(a.im) ? c : b;
        }
        const V args[3] = { a, b, c };
        return special(f, args, 3);
    }
    static V call4(Fn f, const V& a, const V& b, const V& c, const V& d) {
        const V args[4] = { a, b, c, d };
        return special(f, args, 4);
    }
    static V call2(Fn f, const V& a, const V& b) {
        switch (f) {
            case Fn::Atan2: return real(N::atan2(a.re, b.re));
            case Fn::LogBase: {
                V base;
                {
                    BranchGuard g(0);
                    base = ln(b);
                }
                return div(ln(a), base);
            }
            case Fn::NthRoot: return pow(a, div(one(), b));
            case Fn::Mod: return real(real_call2<T, N>(Fn::Mod, a.re, b.re));
            case Fn::Min: return N::lt(b.re, a.re) ? b : a;
            case Fn::Max: return N::gt(b.re, a.re) ? b : a;
            case Fn::Hypot: {
                BranchGuard g(0);
                return sqrt(add(mul(a, a), mul(b, b)));
            }
            case Fn::Lt: case Fn::Le: case Fn::Gt: case Fn::Ge: case Fn::Eq: case Fn::Ne: case Fn::And: case Fn::Or:
                return real(real_call2<T, N>(f, a.re, b.re));
            default: {
                const V args[2] = { a, b };
                return special(f, args, 2);
            }
        }
    }
};

template <class Ops>
class Machine {
public:
    using V = typename Ops::V;

    void prepare(const Program& p) {
        m_consts.clear();
        for (const Constant& c : p.constants) m_consts.push_back(Ops::constant(c));
    }

    void run(const Program& p, std::size_t n, const std::vector<const V*>& inputs, V* out) {
        BranchGuard branch(p.branch);
        if (m_stack.size() < p.stack) m_stack.resize(p.stack);
        for (std::size_t s = 0; s < p.stack; ++s) {
            if (m_stack[s].size() < n) m_stack[s].resize(n);
        }
        std::size_t sp = 0;
        for (const Instr& ins : p.code) {
            switch (ins.code) {
                case Code::Const: {
                    V* d = m_stack[sp].data();
                    const V c = m_consts[ins.arg];
                    for (std::size_t i = 0; i < n; ++i) d[i] = c;
                    ++sp;
                    break;
                }
                case Code::Load: {
                    V* d = m_stack[sp].data();
                    const V* s = inputs[ins.arg];
                    for (std::size_t i = 0; i < n; ++i) d[i] = s[i];
                    ++sp;
                    break;
                }
                case Code::Imag: {
                    V* d = m_stack[sp].data();
                    const V c = Ops::imag();
                    for (std::size_t i = 0; i < n; ++i) d[i] = c;
                    ++sp;
                    break;
                }
                case Code::Neg: {
                    V* d = m_stack[sp - 1].data();
                    for (std::size_t i = 0; i < n; ++i) d[i] = Ops::neg(d[i]);
                    break;
                }
                case Code::Add: binary(sp, n, [](const V& a, const V& b) { return Ops::add(a, b); }); break;
                case Code::Sub: binary(sp, n, [](const V& a, const V& b) { return Ops::sub(a, b); }); break;
                case Code::Mul: binary(sp, n, [](const V& a, const V& b) { return Ops::mul(a, b); }); break;
                case Code::Div: binary(sp, n, [](const V& a, const V& b) { return Ops::div(a, b); }); break;
                case Code::Pow: binary(sp, n, [](const V& a, const V& b) { return Ops::pow(a, b); }); break;
                case Code::Call1: {
                    V* d = m_stack[sp - 1].data();
                    const Fn f = ins.fn;
                    for (std::size_t i = 0; i < n; ++i) d[i] = Ops::call1(f, d[i]);
                    break;
                }
                case Code::Call2: {
                    const Fn f = ins.fn;
                    binary(sp, n, [f](const V& a, const V& b) { return Ops::call2(f, a, b); });
                    break;
                }
                case Code::Call3: {
                    const Fn f = ins.fn;
                    V* a = m_stack[sp - 3].data();
                    const V* b = m_stack[sp - 2].data();
                    const V* c = m_stack[sp - 1].data();
                    for (std::size_t i = 0; i < n; ++i) a[i] = Ops::call3(f, a[i], b[i], c[i]);
                    sp -= 2;
                    break;
                }
                case Code::Call4: {
                    const Fn f = ins.fn;
                    V* a = m_stack[sp - 4].data();
                    const V* b = m_stack[sp - 3].data();
                    const V* c = m_stack[sp - 2].data();
                    const V* d = m_stack[sp - 1].data();
                    for (std::size_t i = 0; i < n; ++i) a[i] = Ops::call4(f, a[i], b[i], c[i], d[i]);
                    sp -= 3;
                    break;
                }
            }
        }
        const V* r = m_stack[0].data();
        for (std::size_t i = 0; i < n; ++i) out[i] = r[i];
    }

private:
    template <class F>
    void binary(std::size_t& sp, std::size_t n, F f) {
        V* a = m_stack[sp - 2].data();
        const V* b = m_stack[sp - 1].data();
        for (std::size_t i = 0; i < n; ++i) a[i] = f(a[i], b[i]);
        --sp;
    }

    std::vector<std::vector<V>> m_stack;
    std::vector<V> m_consts;
};

template <class N>
class GenericEvaluator : public Evaluator {
public:
    using T = typename N::T;
    using C = Cx<T>;

    explicit GenericEvaluator(const Program& p) : m_program(p) {
        m_real.prepare(p);
        m_complex.prepare(p);
    }

    void real(std::size_t n, const double* const* in, double* out) override {
        if (!m_program.valid) {
            for (std::size_t i = 0; i < n; ++i) out[i] = std::nan("");
            return;
        }
        const std::size_t k = m_program.slots.size();
        if (m_program.complex) {
            m_cin.resize(k);
            m_cptr.resize(k);
            for (std::size_t s = 0; s < k; ++s) {
                m_cin[s].resize(n);
                for (std::size_t i = 0; i < n; ++i) m_cin[s][i] = C{ N::from_double(in[s][i]), N::zero() };
                m_cptr[s] = m_cin[s].data();
            }
            m_cout.resize(n);
            m_complex.run(m_program, n, m_cptr, m_cout.data());
            for (std::size_t i = 0; i < n; ++i) {
                const double re = N::to_double(m_cout[i].re);
                const double im = N::to_double(m_cout[i].im);
                out[i] = std::fabs(im) <= 1e-9 * std::max(1.0, std::fabs(re)) ? re : std::nan("");
            }
            return;
        }
        m_in.resize(k);
        m_ptr.resize(k);
        for (std::size_t s = 0; s < k; ++s) {
            m_in[s].resize(n);
            for (std::size_t i = 0; i < n; ++i) m_in[s][i] = N::from_double(in[s][i]);
            m_ptr[s] = m_in[s].data();
        }
        m_out.resize(n);
        m_real.run(m_program, n, m_ptr, m_out.data());
        for (std::size_t i = 0; i < n; ++i) out[i] = N::to_double(m_out[i]);
    }

    void complex(std::size_t n, const double* const* in_re, const double* const* in_im, double* out_re, double* out_im) override {
        if (!m_program.valid) {
            for (std::size_t i = 0; i < n; ++i) out_re[i] = out_im[i] = std::nan("");
            return;
        }
        const std::size_t k = m_program.slots.size();
        m_cin.resize(k);
        m_cptr.resize(k);
        for (std::size_t s = 0; s < k; ++s) {
            m_cin[s].resize(n);
            for (std::size_t i = 0; i < n; ++i) m_cin[s][i] = C{ N::from_double(in_re[s][i]), in_im ? N::from_double(in_im[s][i]) : N::zero() };
            m_cptr[s] = m_cin[s].data();
        }
        m_cout.resize(n);
        m_complex.run(m_program, n, m_cptr, m_cout.data());
        for (std::size_t i = 0; i < n; ++i) {
            out_re[i] = N::to_double(m_cout[i].re);
            out_im[i] = N::to_double(m_cout[i].im);
        }
    }

    std::string text(const std::vector<double>& in, int digits) override {
        if (!m_program.valid) return "undefined";
        const std::size_t k = m_program.slots.size();
        if (m_program.complex) {
            std::vector<double> zeros(in.size(), 0.0);
            return complex_text(in, zeros, digits);
        }
        std::vector<std::vector<T>> vals(k, std::vector<T>(1));
        std::vector<const T*> ptrs(k);
        for (std::size_t s = 0; s < k; ++s) {
            vals[s][0] = N::from_double(s < in.size() ? in[s] : 0.0);
            ptrs[s] = vals[s].data();
        }
        T out[1];
        m_real.run(m_program, 1, ptrs, out);
        return N::text(out[0], digits);
    }

    std::string complex_text(const std::vector<double>& in_re, const std::vector<double>& in_im, int digits) override {
        if (!m_program.valid) return "undefined";
        const std::size_t k = m_program.slots.size();
        std::vector<std::vector<C>> vals(k, std::vector<C>(1));
        std::vector<const C*> ptrs(k);
        for (std::size_t s = 0; s < k; ++s) {
            vals[s][0] = C{ N::from_double(s < in_re.size() ? in_re[s] : 0.0), N::from_double(s < in_im.size() ? in_im[s] : 0.0) };
            ptrs[s] = vals[s].data();
        }
        C out[1];
        m_complex.run(m_program, 1, ptrs, out);
        const std::string re = N::text(out[0].re, digits);
        if (N::is_zero(out[0].im)) return re;
        std::string im = N::text(N::lt(out[0].im, N::zero()) ? N::neg(out[0].im) : out[0].im, digits);
        if (N::is_zero(out[0].re)) return (N::lt(out[0].im, N::zero()) ? "-" : "") + im + "i";
        return re + (N::lt(out[0].im, N::zero()) ? " - " : " + ") + im + "i";
    }

private:
    Program m_program;
    Machine<RealOps<N>> m_real;
    Machine<ComplexOps<N>> m_complex;
    std::vector<std::vector<T>> m_in;
    std::vector<const T*> m_ptr;
    std::vector<T> m_out;
    std::vector<std::vector<C>> m_cin;
    std::vector<const C*> m_cptr;
    std::vector<C> m_cout;
};

} // namespace detail
} // namespace arbor

#endif // ARBOR_EVAL_IMPL_HPP
