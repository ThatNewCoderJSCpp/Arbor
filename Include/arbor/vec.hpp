#ifndef ARBOR_VEC_HPP
#define ARBOR_VEC_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace arbor {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return { a.x + b.x, a.y + b.y }; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return { a.x - b.x, a.y - b.y }; }
inline Vec2 operator*(Vec2 a, double s) { return { a.x * s, a.y * s }; }
inline Vec2 operator*(double s, Vec2 a) { return { a.x * s, a.y * s }; }
inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline double length(Vec2 a) { return std::sqrt(dot(a, a)); }
inline Vec2 normalized(Vec2 a) {
    const double l = length(a);
    return l > 0.0 ? a * (1.0 / l) : Vec2{ 0.0, 0.0 };
}
inline bool finite(Vec2 a) { return std::isfinite(a.x) && std::isfinite(a.y); }

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 operator-(Vec3 a) { return { -a.x, -a.y, -a.z }; }
inline Vec3 operator*(Vec3 a, double s) { return { a.x * s, a.y * s, a.z * s }; }
inline Vec3 operator*(double s, Vec3 a) { return { a.x * s, a.y * s, a.z * s }; }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
inline double length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalized(Vec3 a) {
    const double l = length(a);
    return l > 0.0 ? a * (1.0 / l) : Vec3{ 0.0, 0.0, 0.0 };
}
inline bool finite(Vec3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

struct Rgba {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

inline Rgba rgba(int r, int g, int b, int a = 255) {
    auto c = [](int v) { return static_cast<std::uint8_t>(std::clamp(v, 0, 255)); };
    return { c(r), c(g), c(b), c(a) };
}

inline Rgba mix(Rgba a, Rgba b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    auto m = [t](std::uint8_t x, std::uint8_t y) { return static_cast<int>(std::lround(x + (y - x) * t)); };
    return rgba(m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a));
}

inline Rgba with_alpha(Rgba c, double alpha) {
    c.a = static_cast<std::uint8_t>(std::clamp(alpha, 0.0, 1.0) * 255.0 + 0.5);
    return c;
}

inline Rgba scale(Rgba c, double k) {
    return rgba(static_cast<int>(c.r * k), static_cast<int>(c.g * k), static_cast<int>(c.b * k), c.a);
}

bool parse_color(const std::string& text, Rgba& out);
std::string color_hex(Rgba c);
Rgba hsv(double h, double s, double v);
Rgba colormap(const std::string& name, double t);

} // namespace arbor

#endif // ARBOR_VEC_HPP
