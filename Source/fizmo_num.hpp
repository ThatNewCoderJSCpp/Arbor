#ifndef ARBOR_FIZMO_NUM_HPP
#define ARBOR_FIZMO_NUM_HPP

#ifndef FIZMO
#define FIZMO
#endif
#include <fizmo/includes.hpp>

#include "eval_impl.hpp"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <system_error>

namespace arbor {
namespace detail {

namespace mp = fizmo::multiprecision;

template <class F>
double fizmo_to_double(const F& v) {
    if (v.is_unordered()) return std::nan("");
    if (v.is_infinite()) return v.is_negative() ? -INFINITY : INFINITY;
    if (v.is_zero()) return v.is_negative() ? -0.0 : 0.0;
    const auto parts = v.frexp();
    constexpr std::size_t wide = static_cast<std::size_t>(F::mantissa_bits) + 1;
    constexpr std::size_t shift = wide > 64 ? wide - 64 : 0;
    const auto top = shift ? (parts.first >> shift) : parts.first;
    const double significand = static_cast<double>(static_cast<unsigned long long>(top.get_lowest_bits()));
    const long long exponent = static_cast<long long>(parts.second.get_lowest_bits());
    const double magnitude = std::ldexp(significand, static_cast<int>(parts.second.is_negative() ? -exponent : exponent) - static_cast<int>(F::mantissa_bits) + static_cast<int>(shift));
    return v.is_negative() ? -magnitude : magnitude;
}

#if defined(__APPLE__)
inline bool shortest_decimal(double v, char* buf, std::size_t size, std::size_t& length) {
    int precision = 0;
    char sci[40];
    for (int p = 1; p <= 17 && precision == 0; ++p) {
        const int n = std::snprintf(sci, sizeof(sci), "%.*e", p - 1, v);
        if (n > 0 && n < static_cast<int>(sizeof(sci)) && std::strtod(sci, nullptr) == v) precision = p;
    }
    if (precision == 0) return false;
    const char* e = sci;
    while (*e && *e != 'e') ++e;
    const int x = std::atoi(e + 1);
    const int ax = x < 0 ? -x : x;
    const int fixed_digits = x >= 0 ? (precision > x + 1 ? precision : x + 1) : precision - x;
    const int fixed_chars = fixed_digits + ((x >= 0 ? precision > x + 1 : true) ? 1 : 0);
    const int sci_chars = precision + (precision > 1 ? 1 : 0) + 2 + (ax >= 100 ? 3 : 2);
    if ((fixed_chars <= sci_chars ? fixed_digits : precision) > 15) return false;
    const int n = std::snprintf(buf, size, "%.*g", precision, v);
    if (n <= 0 || n >= static_cast<int>(size)) return false;
    length = static_cast<std::size_t>(n);
    return true;
}
#endif

template <class F>
struct FizmoNum {
    using T = F;
    static T from_double(double v) {
        if (std::isnan(v)) return T::nan();
        if (std::isinf(v)) return v > 0 ? T::positive_infinity() : T::negative_infinity();
        if (F::mantissa_bits <= 53 || v == 0.0 || (std::fabs(v) < 9007199254740992.0 && std::floor(v) == v)) return T(v);
        char buf[40];
#if defined(__APPLE__)
        std::size_t length = 0;
        if (!shortest_decimal(v, buf, sizeof(buf), length)) return T(v);
        T parsed(std::string(buf, length));
#else
        const auto r = std::to_chars(buf, buf + sizeof(buf), v);
        if (r.ec != std::errc()) return T(v);
        int digits = 0;
        for (const char* q = buf; q != r.ptr && *q != 'e' && *q != 'E'; ++q) digits += *q >= '0' && *q <= '9' ? 1 : 0;
        if (digits > 15) return T(v);
        T parsed(std::string(buf, r.ptr));
#endif
        if (parsed.is_unordered()) return T(v);
        return parsed;
    }
    static T from_literal(const std::string& s, double v) {
        T parsed(s);
        if (parsed.is_unordered()) return from_double(v);
        return parsed;
    }
    static double to_double(const T& v) { return fizmo_to_double(v); }
    static std::string text(const T& v, int digits) {
        if (v.is_unordered()) return "undefined";
        if (v.is_infinite()) return v.is_negative() ? "-\xE2\x88\x9E" : "\xE2\x88\x9E";
        if (v.is_zero()) return "0";
        std::string s = v.to_string(digits);
        const std::size_t e = s.find_first_of("eE");
        std::string mant = e == std::string::npos ? s : s.substr(0, e);
        const std::string ex = e == std::string::npos ? std::string() : s.substr(e);
        if (mant.find('.') != std::string::npos) {
            while (!mant.empty() && mant.back() == '0') mant.pop_back();
            if (!mant.empty() && mant.back() == '.') mant.pop_back();
        }
        return mant + ex;
    }
    static T zero() { return T(0.0); }
    static T one() { return T(1.0); }
    static T half() { return T(0.5); }
    static T nan() { return T::nan(); }
    static T inf() { return T::positive_infinity(); }
    static T pi() { return mp::constants::pi<T>(); }
    static T e() { return mp::constants::e<T>(); }
    static T tau() { return mp::constants::pi<T>() * T(2.0); }
    static bool lt(const T& a, const T& b) { return !a.is_unordered() && !b.is_unordered() && a < b; }
    static bool gt(const T& a, const T& b) { return !a.is_unordered() && !b.is_unordered() && b < a; }
    static bool is_zero(const T& a) { return a.is_zero(); }
    static bool is_nan(const T& a) { return a.is_unordered(); }
    static T add(const T& a, const T& b) { return a + b; }
    static T sub(const T& a, const T& b) { return a - b; }
    static T mul(const T& a, const T& b) { return a * b; }
    static T div(const T& a, const T& b) { return a / b; }
    static T neg(const T& a) { return -a; }
    static T pow(const T& a, const T& b) { return fizmo::math::pow(a, b); }
    static T sin(const T& a) { return fizmo::math::sin(a); }
    static T cos(const T& a) { return fizmo::math::cos(a); }
    static T tan(const T& a) { return fizmo::math::tan(a); }
    static T atan2(const T& a, const T& b) { return fizmo::math::atan2(a, b); }
    static T atan(const T& a) { return atan2(a, one()); }
    static T asin(const T& a) {
        if (gt(abs(a), one())) return nan();
        return atan2(a, sqrt(sub(one(), mul(a, a))));
    }
    static T acos(const T& a) {
        if (gt(abs(a), one())) return nan();
        return atan2(sqrt(sub(one(), mul(a, a))), a);
    }
    static T sinh(const T& a) { return fizmo::math::sinh(a); }
    static T cosh(const T& a) { return fizmo::math::cosh(a); }
    static T tanh(const T& a) { return fizmo::math::tanh(a); }
    static T asinh(const T& a) {
        const T m = abs(a);
        const T r = ln(add(m, sqrt(add(mul(m, m), one()))));
        return lt(a, zero()) ? neg(r) : r;
    }
    static T acosh(const T& a) {
        if (lt(a, one())) return nan();
        return ln(add(a, sqrt(sub(mul(a, a), one()))));
    }
    static T atanh(const T& a) {
        if (!lt(abs(a), one())) return is_zero(sub(abs(a), one())) ? (lt(a, zero()) ? neg(inf()) : inf()) : nan();
        return mul(half(), ln(div(add(one(), a), sub(one(), a))));
    }
    static T exp(const T& a) { return fizmo::math::exp(a); }
    static T ln(const T& a) { return fizmo::math::ln(a); }
    static T log10(const T& a) { return fizmo::math::log10(a); }
    static T log2(const T& a) { return fizmo::math::log2(a); }
    static T sqrt(const T& a) { return fizmo::math::sqrt(a); }
    static T cbrt(const T& a) { return fizmo::math::cbrt(a); }
    static T abs(const T& a) { return a.is_negative() ? -a : a; }
    static T floor(const T& a) { return fizmo::math::floor(a); }
    static T ceil(const T& a) { return neg(fizmo::math::floor(neg(a))); }
    static T gamma(const T& a) { return fizmo::math::gamma(a); }
    static T erf(const T& a) { return fizmo::math::erf(a); }
    static T named(const std::string& literal) {
        if (literal == "@golden") return mp::constants::phi<T>();
        if (literal == "@euler") return mp::constants::euler_mascheroni<T>();
        if (literal == "@catalan") return mp::constants::catalan<T>();
        if (literal == "@apery") return mp::constants::apery<T>();
        if (literal == "@silver") return mp::constants::silver_ratio<T>();
        if (literal == "@sqrt2") return mp::constants::sqrt2<T>();
        if (literal == "@ln2") return mp::constants::ln2<T>();
        if (literal == "@psi") return mp::constants::inv_phi<T>();
        if (literal == "@silverconstant") return mp::constants::silver_constant<T>();
        if (literal == "@bronze") return mp::constants::bronze_ratio<T>();
        return nan();
    }
    static long long whole(const T& v) {
        return static_cast<long long>(std::llround(to_double(v)));
    }
    static long long index(const T& v) {
        const double d = to_double(v);
        if (!(d >= 0.0) || std::floor(d) != d || d > 1e15) return -1;
        return static_cast<long long>(d);
    }
    static long long integer(const T& v, bool& ok) {
        const double d = to_double(v);
        ok = std::isfinite(d) && std::floor(d) == d && std::fabs(d) < 1e15;
        return ok ? static_cast<long long>(d) : 0;
    }
    static T special(Fn fn, const T* a, int n) {
        namespace m = fizmo::math;
        namespace k = fizmo::multiprecision::constants;
        (void)n;
        bool ok = true;
        try {
            switch (fn) {
                case Fn::Sinc: return m::sinc(a[0]);
                case Fn::Gd: return m::gudermannian(a[0]);
                case Fn::Expm1: return m::expm1(a[0]);
                case Fn::Log1p: return lt(a[0], neg(one())) ? nan() : m::log1p(a[0]);
                case Fn::LambertW:
                case Fn::LambertWk: {
                    const long long k = fn == Fn::LambertWk ? whole(a[1]) : t_branch;
                    if (k == 0) return m::lambert_w0(a[0]);
                    if (k == -1) return lt(a[0], zero()) ? m::lambert_wm1(a[0]) : (is_zero(a[0]) ? neg(inf()) : nan());
                    return nan();
                }
                case Fn::LogGamma: {
                    const T g = m::gamma(a[0]);
                    if (g.is_negative()) return nan();
                    return m::log_abs_gamma(a[0]);
                }
                case Fn::LogAbsGamma: return m::log_abs_gamma(a[0]);
                case Fn::Factorial: return m::gamma(add(a[0], one()));
                case Fn::Digamma: return m::digamma(a[0]);
                case Fn::Trigamma: return m::trigamma(a[0]);
                case Fn::Polygamma: return m::polygamma(a[1], static_cast<int>(whole(a[0])));
                case Fn::Beta: return m::beta(a[0], a[1]);
                case Fn::Binomial: return m::combination(a[0], a[1]);
                case Fn::Pochhammer: return m::rising_pochhammer(a[0], a[1]);
                case Fn::Harmonic: return m::harmonic(a[0]);
                case Fn::GammaP: return m::regularized_gamma_p(a[0], a[1]);
                case Fn::GammaQ: return m::regularized_gamma_q(a[0], a[1]);
                case Fn::LowerGamma: return m::lower_incomplete_gamma(a[0], a[1]);
                case Fn::UpperGamma: return m::upper_incomplete_gamma(a[0], a[1]);
                case Fn::Zeta: return m::riemann_zeta(a[0]);
                case Fn::HurwitzZeta: return m::hurwitz_zeta(a[0], a[1]);
                case Fn::DirichletEta: return m::dirichlet_eta(a[0]);
                case Fn::DirichletBeta: return m::dirichlet_beta(a[0]);
                case Fn::RiemannXi: return m::riemann_xi(a[0]);
                case Fn::Polylog: return m::polylog(a[1], a[0]);
                case Fn::Dilog: return m::dilogarithm(a[0]);
                case Fn::Clausen: return m::clausen_integral(a[0]);
                case Fn::Erfc: return m::erfc(a[0]);
                case Fn::Erfi: return m::erfi(a[0]);
                case Fn::InvErf: return m::inv_erf(a[0]);
                case Fn::InvErfc: return m::inv_erfc(a[0]);
                case Fn::Si: return m::sin_integral(a[0]);
                case Fn::Ci: return m::cos_integral(a[0]);
                case Fn::Shi: return m::sinh_integral(a[0]);
                case Fn::Chi: return m::cosh_integral(a[0]);
                case Fn::Ei: return m::exp_integral(a[0]);
                case Fn::Li: return m::log_integral(a[0]);
                case Fn::FresnelS: return m::fresnel_s(a[0]);
                case Fn::FresnelC: return m::fresnel_c(a[0]);
                case Fn::AiryAi: return m::airy_ai(a[0]);
                case Fn::AiryBi: return m::airy_bi(a[0]);
                case Fn::EllipticK: return m::elliptic_k(a[0]);
                case Fn::EllipticE: return m::elliptic_e(a[0]);
                case Fn::EllipticE2: return m::elliptic_e(a[0], a[1]);
                case Fn::EllipticF: return m::elliptic_f(a[0], a[1]);
                case Fn::EllipticPi: return m::elliptic_pi(a[0], a[1]);
                case Fn::JacobiSn: return m::jacobi_sn(a[0], a[1]);
                case Fn::JacobiCn: return m::jacobi_cn(a[0], a[1]);
                case Fn::JacobiDn: return m::jacobi_dn(a[0], a[1]);
                case Fn::JacobiAm: return m::jacobi_am(a[0], a[1]);
                case Fn::InvGd: return m::inv_gudermannian(a[0]);
                case Fn::ClausenS: {
                    const long long s = integer(a[1], ok);
                    return ok ? m::clausen(a[0], static_cast<std::int64_t>(s)) : nan();
                }
                case Fn::ExpIntN: {
                    const long long s = integer(a[1], ok);
                    return ok ? m::exp_integral_generalized(a[0], static_cast<std::int64_t>(s)) : nan();
                }
                case Fn::LogIntN: {
                    const long long s = integer(a[1], ok);
                    return ok ? m::log_integral_generalized(a[0], static_cast<std::int64_t>(s)) : nan();
                }
                case Fn::ScorerHi: return m::scorer_hi(a[0]);
                case Fn::ScorerGi: return m::scorer_gi(a[0]);
                case Fn::JacobiCd: return m::jacobi_cd(a[0], a[1]);
                case Fn::JacobiCs: return m::jacobi_cs(a[0], a[1]);
                case Fn::JacobiDc: return m::jacobi_dc(a[0], a[1]);
                case Fn::JacobiDs: return m::jacobi_ds(a[0], a[1]);
                case Fn::JacobiNc: return m::jacobi_nc(a[0], a[1]);
                case Fn::JacobiNd: return m::jacobi_nd(a[0], a[1]);
                case Fn::JacobiNs: return m::jacobi_ns(a[0], a[1]);
                case Fn::JacobiSc: return m::jacobi_sc(a[0], a[1]);
                case Fn::JacobiSd: return m::jacobi_sd(a[0], a[1]);
                case Fn::EllipticPi3: return m::elliptic_pi(a[0], a[1], a[2]);
                case Fn::CarlsonRC: return m::carlson_rc(a[0], a[1]);
                case Fn::CarlsonRD: return m::carlson_rd(a[0], a[1], a[2]);
                case Fn::CarlsonRF: return m::carlson_rf(a[0], a[1], a[2]);
                case Fn::CarlsonRG: return m::carlson_rg(a[0], a[1], a[2]);
                case Fn::CarlsonRJ: return m::carlson_rj(a[0], a[1], a[2], a[3]);
                case Fn::Bernoulli: {
                    const long long i = index(a[0]);
                    if (i < 0) return nan();
                    if (i > 1 && i % 2 == 1) return zero();
                    return k::bernoulli<T>(static_cast<std::size_t>(i));
                }
                case Fn::EulerNumber: {
                    const long long i = index(a[0]);
                    if (i < 0) return nan();
                    if (i % 2 == 1) return zero();
                    return k::euler_number<T>(static_cast<std::size_t>(i));
                }
                case Fn::TangentNumber: {
                    const long long i = index(a[0]);
                    return i < 0 ? nan() : k::tangent_number<T>(static_cast<std::size_t>(i));
                }
                case Fn::SecantNumber: {
                    const long long i = index(a[0]);
                    return i < 0 ? nan() : k::secant_number<T>(static_cast<std::size_t>(i));
                }
                case Fn::Eulerian: {
                    const long long i = index(a[0]);
                    const long long j = index(a[1]);
                    return i < 0 || j < 0 ? nan() : k::eulerian_number<T>(static_cast<std::size_t>(i), static_cast<std::size_t>(j));
                }
                case Fn::Stieltjes: {
                    const long long i = index(a[0]);
                    return i < 0 ? nan() : k::stieltjes<T>(static_cast<std::size_t>(i));
                }
                case Fn::Metallic: {
                    const long long i = index(a[0]);
                    return i < 0 ? nan() : k::metallic_ratio<T>(static_cast<std::uint64_t>(i));
                }
                case Fn::InvMetallic: {
                    const long long i = index(a[0]);
                    return i < 0 ? nan() : k::inv_metallic_ratio<T>(static_cast<std::uint64_t>(i));
                }
                case Fn::MetallicMean: {
                    const long long i = index(a[0]);
                    return i < 0 ? nan() : k::metallic_mean<T>(static_cast<std::uint64_t>(i));
                }
                case Fn::Beraha: {
                    const long long i = index(a[0]);
                    return i < 1 ? nan() : k::beraha<T>(static_cast<std::uint64_t>(i));
                }
                case Fn::Fibonacci: return m::fibonacci(a[0]);
                case Fn::Lucas: return m::lucas(a[0]);
                case Fn::FibonacciPoly: {
                    const long long i = integer(a[1], ok);
                    return ok ? m::fibonacci_polynomial(a[0], static_cast<std::int64_t>(i)) : nan();
                }
                case Fn::LucasPoly: {
                    const long long i = integer(a[1], ok);
                    return ok ? m::lucas_polynomial(a[0], static_cast<std::int64_t>(i)) : nan();
                }
                case Fn::ChebyshevT:
                case Fn::ChebyshevU:
                case Fn::ChebyshevV:
                case Fn::ChebyshevW:
                case Fn::LegendreP:
                case Fn::LegendreQ:
                case Fn::Debye: {
                    const long long i = integer(a[1], ok);
                    if (!ok) return nan();
                    const std::int64_t d = static_cast<std::int64_t>(i);
                    switch (fn) {
                        case Fn::ChebyshevT: return m::chebyshev_t(a[0], d);
                        case Fn::ChebyshevU: return m::chebyshev_u(a[0], d);
                        case Fn::ChebyshevV: return m::chebyshev_v(a[0], d);
                        case Fn::ChebyshevW: return m::chebyshev_w(a[0], d);
                        case Fn::LegendreP: return m::legendre_p(a[0], d);
                        case Fn::LegendreQ: return m::legendre_q(a[0], d);
                        default: return m::debye(a[0], d);
                    }
                }
                case Fn::DirichletLambda: return m::dirichlet_lambda(a[0]);
                case Fn::RiemannChi: return m::riemann_chi(a[0]);
                case Fn::LegendreChi: return m::legendre_chi(a[0], a[1]);
                case Fn::Trilog: return m::trilogarithm(a[0]);
                case Fn::RogersL: return m::rogers_l(a[0]);
                case Fn::Spence: return m::spence_function(a[0]);
                case Fn::Lerch: return m::lerch_transcendent(a[0], a[1], a[2]);
                case Fn::BetaInc: return m::incomplete_beta(a[0], a[1], a[2]);
                case Fn::BetaReg: return m::regularized_beta(a[0], a[1], a[2]);
                case Fn::BetaRegC: return m::regularized_beta_complement(a[0], a[1], a[2]);
                case Fn::InvBetaReg: return m::inverse_regularized_beta(a[0], a[1], a[2]);
                case Fn::InvGammaP: return m::inverse_regularized_gamma_p(a[0], a[1]);
                case Fn::InvGammaQ: return m::inverse_regularized_gamma_q(a[0], a[1]);
                case Fn::Falling: return m::falling_pochhammer(a[0], a[1]);
                case Fn::LogBeta: return m::log_abs_beta(a[0], a[1]);
                case Fn::NSinc: return m::normalized_sinc(a[0]);
                case Fn::InvErfi: return m::inv_erfi(a[0]);
                default: return nan();
            }
        } catch (...) {
            return nan();
        }
    }
};

} // namespace detail
} // namespace arbor

#endif // ARBOR_FIZMO_NUM_HPP
