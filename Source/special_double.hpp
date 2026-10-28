#ifndef ARBOR_SPECIAL_DOUBLE_HPP
#define ARBOR_SPECIAL_DOUBLE_HPP

#include <cmath>
#include <complex>
#include <limits>

namespace arbor {
namespace sd {

using cd = std::complex<double>;

inline constexpr double k_pi = 3.14159265358979323846;
inline constexpr double k_euler = 0.57721566490153286061;

inline double nan() {
    return std::numeric_limits<double>::quiet_NaN();
}

inline cd cnan() {
    return cd(nan(), nan());
}

inline bool finite(cd z) {
    return std::isfinite(z.real()) && std::isfinite(z.imag());
}

inline cd gamma(cd z) {
    static const double g = 7.0;
    static const double coef[9] = { 0.99999999999980993, 676.5203681218851, -1259.1392167224028, 771.32342877765313, -176.61502916214059, 12.507343278686905, -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7 };
    if (z.imag() == 0.0 && z.real() <= 0.0 && std::floor(z.real()) == z.real()) return cd(std::numeric_limits<double>::infinity(), 0.0);
    if (z.real() < 0.5) return k_pi / (std::sin(k_pi * z) * gamma(1.0 - z));
    z -= 1.0;
    cd x = coef[0];
    for (int i = 1; i < 9; ++i) x += coef[i] / (z + static_cast<double>(i));
    const cd t = z + (g + 0.5);
    return std::sqrt(2.0 * k_pi) * std::pow(t, z + 0.5) * std::exp(-t) * x;
}

inline cd lgamma(cd z) {
    if (z.imag() == 0.0 && z.real() > 0.0) return cd(std::lgamma(z.real()), 0.0);
    if (z.imag() == 0.0 && z.real() <= 0.0 && std::floor(z.real()) == z.real()) return cd(std::numeric_limits<double>::infinity(), 0.0);
    if (z.real() < 0.5) return std::log(k_pi) - std::log(std::sin(k_pi * z)) - lgamma(1.0 - z);
    cd shift = 0.0;
    while (std::abs(z) < 12.0) {
        shift += std::log(z);
        z += 1.0;
    }
    const cd zi = 1.0 / z;
    const cd zi2 = zi * zi;
    const cd series = zi * (1.0 / 12.0 - zi2 * (1.0 / 360.0 - zi2 * (1.0 / 1260.0 - zi2 * (1.0 / 1680.0 - zi2 / 1188.0))));
    return (z - 0.5) * std::log(z) - z + 0.5 * std::log(2.0 * k_pi) + series - shift;
}

inline double digamma(double x) {
    if (x <= 0.0 && std::floor(x) == x) return nan();
    double acc = 0.0;
    if (x < 0.5) {
        acc -= k_pi / std::tan(k_pi * x);
        x = 1.0 - x;
    }
    while (x < 20.0) {
        acc -= 1.0 / x;
        x += 1.0;
    }
    const double f = 1.0 / (x * x);
    const double series = f * (1.0 / 12.0 - f * (1.0 / 120.0 - f * (1.0 / 252.0 - f * (1.0 / 240.0 - f * (1.0 / 132.0)))));
    return acc + std::log(x) - 0.5 / x - series;
}

inline cd digamma(cd z) {
    if (z.imag() == 0.0) return cd(digamma(z.real()), 0.0);
    cd acc = 0.0;
    if (z.real() < 0.5) {
        acc -= k_pi / std::tan(k_pi * z);
        z = 1.0 - z;
    }
    while (std::abs(z) < 20.0) {
        acc -= 1.0 / z;
        z += 1.0;
    }
    const cd f = 1.0 / (z * z);
    const cd series = f * (1.0 / 12.0 - f * (1.0 / 120.0 - f * (1.0 / 252.0 - f * (1.0 / 240.0 - f * (1.0 / 132.0)))));
    return acc + std::log(z) - 0.5 / z - series;
}

inline double trigamma(double x) {
    if (x <= 0.0 && std::floor(x) == x) return nan();
    if (x < 0.5) {
        const double s = std::sin(k_pi * x);
        return k_pi * k_pi / (s * s) - trigamma(1.0 - x);
    }
    double acc = 0.0;
    while (x < 20.0) {
        acc += 1.0 / (x * x);
        x += 1.0;
    }
    const double f = 1.0 / (x * x);
    return acc + 1.0 / x + 0.5 * f + (1.0 / x) * f * (1.0 / 6.0 - f * (1.0 / 30.0 - f * (1.0 / 42.0 - f / 30.0)));
}

inline cd zeta(cd s) {
    if (s == cd(1.0, 0.0)) return cd(std::numeric_limits<double>::infinity(), 0.0);
    if (s == cd(0.0, 0.0)) return cd(-0.5, 0.0);
    if (s.real() < 0.5) {
        if (s.imag() == 0.0 && s.real() < 0.0 && std::fmod(-s.real(), 2.0) == 0.0) return 0.0;
        const cd one_minus = 1.0 - s;
        return std::pow(2.0, s) * std::pow(k_pi, s - 1.0) * std::sin(0.5 * k_pi * s) * gamma(one_minus) * zeta(one_minus);
    }
    const int n = 40 + static_cast<int>(std::min(200.0, std::fabs(s.imag()) * 1.2));
    static thread_local int cached_n = 0;
    static thread_local double d[256];
    if (cached_n != n) {
        double term = 1.0 / n;
        double sum = term;
        d[0] = n * sum;
        for (int i = 1; i <= n; ++i) {
            term *= 4.0 * static_cast<double>(n + i - 1) * static_cast<double>(n - i + 1) / ((2.0 * i - 1.0) * (2.0 * i));
            sum += term;
            d[i] = n * sum;
        }
        cached_n = n;
    }
    cd total = 0.0;
    for (int k = 0; k < n; ++k) {
        const cd t = (d[k] - d[n]) * std::exp(-s * std::log(static_cast<double>(k + 1)));
        total += (k % 2 == 0) ? t : -t;
    }
    const cd eta = -total / d[n];
    return eta / (1.0 - std::exp((1.0 - s) * std::log(2.0)));
}

inline double zeta(double s) {
    if (s == 1.0) return std::numeric_limits<double>::infinity();
    if (s < -60.0) {
        if (std::fmod(-s, 2.0) == 0.0) return 0.0;
        const double sn = std::sin(0.5 * k_pi * std::fmod(s, 4.0));
        if (sn == 0.0) return 0.0;
        const double log_mag = s * std::log(2.0) + (s - 1.0) * std::log(k_pi) + std::log(std::fabs(sn)) + std::lgamma(1.0 - s);
        if (log_mag > 709.0) return sn > 0 ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity();
        return (sn > 0 ? 1.0 : -1.0) * std::exp(log_mag);
    }
    const cd r = zeta(cd(s, 0.0));
    return r.real();
}

inline cd dirichlet_eta(cd s) {
    if (s == cd(1.0, 0.0)) return std::log(2.0);
    return (1.0 - std::exp((1.0 - s) * std::log(2.0))) * zeta(s);
}

inline cd lambertw(cd z, int k) {
    if (z == cd(0.0, 0.0)) return k == 0 ? cd(0.0, 0.0) : cd(-std::numeric_limits<double>::infinity(), 0.0);
    const double inv_e = 0.36787944117144233;
    cd w;
    const cd p = std::sqrt(2.0 * (std::exp(1.0) * z + 1.0));
    if ((k == 0 && std::abs(z + inv_e) < 0.3) || ((k == -1 || k == 1) && std::abs(z + inv_e) < 0.3 && ((k == -1 && z.imag() >= 0.0) || (k == 1 && z.imag() < 0.0)))) {
        const cd q = k == 0 ? p : -p;
        w = -1.0 + q - q * q / 3.0 + 11.0 / 72.0 * q * q * q;
    } else if (k == 0 && std::abs(z) < 0.5) {
        w = z - z * z + 1.5 * z * z * z;
    } else if (k == 0 && std::abs(z) < 4.0) {
        w = 0.6 * std::log(1.0 + z);
    } else {
        const cd l1 = std::log(z) + cd(0.0, 2.0 * k_pi * k);
        w = l1 - std::log(l1);
    }
    for (int it = 0; it < 60; ++it) {
        const cd ew = std::exp(w);
        const cd f = w * ew - z;
        const cd wp1 = w + 1.0;
        const cd denom = ew * wp1 - (w + 2.0) * f / (2.0 * wp1);
        if (denom == cd(0.0, 0.0) || !finite(denom)) break;
        const cd step = f / denom;
        w -= step;
        if (std::abs(step) <= 1e-15 * (1.0 + std::abs(w))) break;
    }
    return w;
}

inline double lambertw(double x, int k) {
    const double inv_e = 0.36787944117144233;
    if (std::isnan(x)) return x;
    if (k == 0) {
        if (x < -inv_e) return nan();
        if (x == std::numeric_limits<double>::infinity()) return x;
    } else if (k == -1) {
        if (x < -inv_e || x >= 0.0) return x == 0.0 ? -std::numeric_limits<double>::infinity() : nan();
    } else {
        return nan();
    }
    if (std::fabs(x + inv_e) < 1e-300) return -1.0;
    const cd r = lambertw(cd(x, 0.0), k);
    return r.real();
}

inline cd sinc(cd z) {
    if (std::abs(z) < 1e-8) return 1.0 - z * z / 6.0;
    return std::sin(z) / z;
}

inline double log_abs_gamma_sign(double x, double& sign) {
    sign = 1.0;
    if (x <= 0.0 && std::floor(x) == x) {
        sign = 0.0;
        return std::numeric_limits<double>::infinity();
    }
    if (x < 0.0 && std::fmod(std::floor(-x), 2.0) == 0.0) sign = -1.0;
    return std::lgamma(x);
}

inline double beta(double a, double b) {
    double sa, sb, sab;
    const double la = log_abs_gamma_sign(a, sa);
    const double lb = log_abs_gamma_sign(b, sb);
    const double lab = log_abs_gamma_sign(a + b, sab);
    if (sab == 0.0) return 0.0;
    if (sa == 0.0 || sb == 0.0) return std::numeric_limits<double>::infinity();
    return sa * sb * sab * std::exp(la + lb - lab);
}

inline double binomial(double n, double k) {
    if (std::floor(n) == n && std::floor(k) == k && n >= 0.0 && n <= 1000.0) {
        if (k < 0.0 || k > n) return 0.0;
        double r = 1.0;
        const double kk = std::min(k, n - k);
        for (double i = 1.0; i <= kk; i += 1.0) r = r * (n - kk + i) / i;
        return std::round(r);
    }
    double s1, s2, s3;
    const double l1 = log_abs_gamma_sign(n + 1.0, s1);
    const double l2 = log_abs_gamma_sign(k + 1.0, s2);
    const double l3 = log_abs_gamma_sign(n - k + 1.0, s3);
    if (s2 == 0.0 || s3 == 0.0) return 0.0;
    if (s1 == 0.0) return std::numeric_limits<double>::infinity();
    return s1 * s2 * s3 * std::exp(l1 - l2 - l3);
}

inline double pochhammer(double x, double n) {
    if (std::floor(n) == n && n >= 0.0 && n <= 200.0) {
        double r = 1.0;
        for (double i = 0.0; i < n; i += 1.0) r *= x + i;
        return r;
    }
    double s1, s2;
    const double l1 = log_abs_gamma_sign(x + n, s1);
    const double l2 = log_abs_gamma_sign(x, s2);
    if (s2 == 0.0) return s1 == 0.0 ? nan() : 0.0;
    if (s1 == 0.0) return std::numeric_limits<double>::infinity();
    return s1 * s2 * std::exp(l1 - l2);
}

} // namespace sd
} // namespace arbor

#endif // ARBOR_SPECIAL_DOUBLE_HPP
