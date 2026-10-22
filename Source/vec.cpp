#include "arbor/vec.hpp"

#include <array>
#include <cctype>
#include <cstdio>

namespace arbor {

bool parse_color(const std::string& text_in, Rgba& out) {
    std::string text;
    for (char c : text_in) {
        if (!std::isspace(static_cast<unsigned char>(c))) text += c;
    }
    if (text.empty() || text[0] != '#') return false;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string h = text.substr(1);
    if (h.size() == 3 || h.size() == 4) {
        std::string wide;
        for (char c : h) {
            wide += c;
            wide += c;
        }
        h = wide;
    }
    if (h.size() != 6 && h.size() != 8) return false;
    int v[4] = { 0, 0, 0, 255 };
    for (std::size_t i = 0; i < h.size() / 2; ++i) {
        const int a = hex(h[2 * i]);
        const int b = hex(h[2 * i + 1]);
        if (a < 0 || b < 0) return false;
        v[i] = a * 16 + b;
    }
    out = rgba(v[0], v[1], v[2], v[3]);
    return true;
}

std::string color_hex(Rgba c) {
    char buf[16];
    if (c.a == 255) std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c.r, c.g, c.b);
    else std::snprintf(buf, sizeof buf, "#%02x%02x%02x%02x", c.r, c.g, c.b, c.a);
    return buf;
}

Rgba hsv(double h, double s, double v) {
    h = h - std::floor(h);
    s = std::clamp(s, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);
    const double hh = h * 6.0;
    const int i = static_cast<int>(std::floor(hh)) % 6;
    const double f = hh - std::floor(hh);
    const double p = v * (1.0 - s);
    const double q = v * (1.0 - s * f);
    const double t = v * (1.0 - s * (1.0 - f));
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    switch (i) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    return rgba(static_cast<int>(r * 255.0 + 0.5), static_cast<int>(g * 255.0 + 0.5), static_cast<int>(b * 255.0 + 0.5));
}

namespace {

using Stop = std::array<double, 3>;

Rgba ramp(const Stop* stops, std::size_t n, double t) {
    if (!std::isfinite(t)) return rgba(128, 128, 128);
    t = std::clamp(t, 0.0, 1.0) * static_cast<double>(n - 1);
    const std::size_t i = std::min(static_cast<std::size_t>(t), n - 2);
    const double f = t - static_cast<double>(i);
    const Stop& a = stops[i];
    const Stop& b = stops[i + 1];
    auto c = [&](int k) { return static_cast<int>(std::lround((a[k] + (b[k] - a[k]) * f) * 255.0)); };
    return rgba(c(0), c(1), c(2));
}

const Stop k_viridis[] = {
    { 0.267, 0.005, 0.329 }, { 0.283, 0.141, 0.458 }, { 0.254, 0.265, 0.530 }, { 0.207, 0.372, 0.553 },
    { 0.164, 0.471, 0.558 }, { 0.128, 0.567, 0.551 }, { 0.135, 0.659, 0.518 }, { 0.267, 0.749, 0.441 },
    { 0.478, 0.821, 0.318 }, { 0.741, 0.873, 0.150 }, { 0.993, 0.906, 0.144 }
};

const Stop k_turbo[] = {
    { 0.190, 0.072, 0.232 }, { 0.255, 0.355, 0.784 }, { 0.163, 0.613, 0.983 }, { 0.106, 0.829, 0.761 },
    { 0.350, 0.968, 0.420 }, { 0.700, 0.973, 0.218 }, { 0.950, 0.792, 0.207 }, { 0.988, 0.522, 0.143 },
    { 0.863, 0.255, 0.051 }, { 0.600, 0.067, 0.008 }
};

const Stop k_magma[] = {
    { 0.001, 0.000, 0.014 }, { 0.130, 0.069, 0.317 }, { 0.372, 0.092, 0.500 }, { 0.592, 0.168, 0.505 },
    { 0.819, 0.255, 0.442 }, { 0.973, 0.442, 0.375 }, { 0.995, 0.700, 0.522 }, { 0.987, 0.991, 0.750 }
};

const Stop k_coolwarm[] = {
    { 0.230, 0.299, 0.754 }, { 0.483, 0.615, 0.968 }, { 0.865, 0.865, 0.865 }, { 0.958, 0.603, 0.482 }, { 0.706, 0.016, 0.150 }
};

const Stop k_ocean[] = {
    { 0.106, 0.227, 0.490 }, { 0.137, 0.494, 0.706 }, { 0.196, 0.722, 0.706 }, { 0.565, 0.855, 0.459 }, { 0.953, 0.890, 0.420 }
};

} // namespace

Rgba colormap(const std::string& name, double t) {
    if (name == "turbo") return ramp(k_turbo, sizeof k_turbo / sizeof k_turbo[0], t);
    if (name == "magma") return ramp(k_magma, sizeof k_magma / sizeof k_magma[0], t);
    if (name == "coolwarm") return ramp(k_coolwarm, sizeof k_coolwarm / sizeof k_coolwarm[0], t);
    if (name == "ocean") return ramp(k_ocean, sizeof k_ocean / sizeof k_ocean[0], t);
    return ramp(k_viridis, sizeof k_viridis / sizeof k_viridis[0], t);
}

} // namespace arbor
