#include "arbor/render.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <sstream>

namespace arbor {

void Image::resize(int width, int height) {
    w = std::max(1, width);
    h = std::max(1, height);
    px.assign(static_cast<std::size_t>(w) * h * 4, 0);
}

void Image::clear(Rgba c) {
    for (std::size_t i = 0; i + 3 < px.size(); i += 4) {
        px[i] = c.r;
        px[i + 1] = c.g;
        px[i + 2] = c.b;
        px[i + 3] = 255;
    }
}

void Image::blend(int x, int y, Rgba c, double coverage) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    const double a = std::clamp(coverage, 0.0, 1.0) * c.a / 255.0;
    if (a <= 0.0) return;
    std::uint8_t* p = &px[(static_cast<std::size_t>(y) * w + x) * 4];
    p[0] = static_cast<std::uint8_t>(p[0] + (c.r - p[0]) * a + 0.5);
    p[1] = static_cast<std::uint8_t>(p[1] + (c.g - p[1]) * a + 0.5);
    p[2] = static_cast<std::uint8_t>(p[2] + (c.b - p[2]) * a + 0.5);
    p[3] = 255;
}

Rgba Image::at(int x, int y) const {
    x = std::clamp(x, 0, w - 1);
    y = std::clamp(y, 0, h - 1);
    const std::uint8_t* p = &px[(static_cast<std::size_t>(y) * w + x) * 4];
    return Rgba{ p[0], p[1], p[2], p[3] };
}

std::vector<std::uint8_t> downsample_rgba(const Image& img, int width, int height) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * height * 4, 255);
    const int sx = std::max(1, img.w / std::max(1, width));
    const int sy = std::max(1, img.h / std::max(1, height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int r = 0, g = 0, b = 0, n = 0;
            for (int j = 0; j < sy; ++j) {
                for (int i = 0; i < sx; ++i) {
                    const int X = x * sx + i;
                    const int Y = y * sy + j;
                    if (X >= img.w || Y >= img.h) continue;
                    const std::uint8_t* p = &img.px[(static_cast<std::size_t>(Y) * img.w + X) * 4];
                    r += p[0];
                    g += p[1];
                    b += p[2];
                    ++n;
                }
            }
            std::uint8_t* o = &out[(static_cast<std::size_t>(y) * width + x) * 4];
            if (n == 0) n = 1;
            o[0] = static_cast<std::uint8_t>(r / n);
            o[1] = static_cast<std::uint8_t>(g / n);
            o[2] = static_cast<std::uint8_t>(b / n);
            o[3] = 255;
        }
    }
    return out;
}

namespace {

const double k_nan = std::numeric_limits<double>::quiet_NaN();

struct LayerBuffers {
    std::vector<float> cov;
    std::vector<Rgba> col;
};

std::vector<std::unique_ptr<LayerBuffers>>& layer_pool() {
    thread_local std::vector<std::unique_ptr<LayerBuffers>> pool;
    return pool;
}

std::unique_ptr<LayerBuffers> take_layer_buffers(std::size_t n) {
    auto& pool = layer_pool();
    for (auto it = pool.begin(); it != pool.end(); ++it) {
        if ((*it)->cov.size() != n) continue;
        std::unique_ptr<LayerBuffers> b = std::move(*it);
        pool.erase(it);
        return b;
    }
    auto b = std::make_unique<LayerBuffers>();
    b->cov.assign(n, 0.0f);
    b->col.resize(n);
    return b;
}

void give_layer_buffers(std::unique_ptr<LayerBuffers> b) {
    auto& pool = layer_pool();
    pool.erase(std::remove_if(pool.begin(), pool.end(), [&](const std::unique_ptr<LayerBuffers>& o) { return o->cov.size() != b->cov.size(); }), pool.end());
    if (pool.size() < 4) pool.push_back(std::move(b));
}

class StrokeLayer {
public:
    explicit StrokeLayer(Image& img) : m_img(img), m_buf(take_layer_buffers(static_cast<std::size_t>(img.w) * img.h)), m_cov(m_buf->cov), m_col(m_buf->col) {}

    ~StrokeLayer() {
        if (m_x0 <= m_x1) {
            for (int y = m_y0; y <= m_y1; ++y) std::fill(m_cov.begin() + static_cast<std::ptrdiff_t>(y) * m_img.w + m_x0, m_cov.begin() + static_cast<std::ptrdiff_t>(y) * m_img.w + m_x1 + 1, 0.0f);
        }
        give_layer_buffers(std::move(m_buf));
    }

    StrokeLayer(const StrokeLayer&) = delete;
    StrokeLayer& operator=(const StrokeLayer&) = delete;

    bool visible_part(Vec2& a, Vec2& b, double width) const {
        if (!finite(a) || !finite(b)) return false;
        const double pad = std::max(0.5, 0.5 * width) + 1.5;
        return clip(a, b, -pad, -pad, m_img.w + pad, m_img.h + pad);
    }

    void segment(Vec2 a, Vec2 b, double width, Rgba ca, Rgba cb) {
        if (!finite(a) || !finite(b)) return;
        const double r = std::max(0.5, 0.5 * width);
        const double pad = r + 1.5;
        const double big = 4.0 * (m_img.w + m_img.h);
        if (std::fabs(a.x) > big || std::fabs(a.y) > big || std::fabs(b.x) > big || std::fabs(b.y) > big) {
            if (!clip(a, b, -pad, -pad, m_img.w + pad, m_img.h + pad)) return;
        }
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min(a.x, b.x) - pad)));
        const int x1 = std::min(m_img.w - 1, static_cast<int>(std::ceil(std::max(a.x, b.x) + pad)));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min(a.y, b.y) - pad)));
        const int y1 = std::min(m_img.h - 1, static_cast<int>(std::ceil(std::max(a.y, b.y) + pad)));
        if (x0 > x1 || y0 > y1) return;
        const Vec2 d = b - a;
        const double len2 = dot(d, d);
        const bool same = ca.r == cb.r && ca.g == cb.g && ca.b == cb.b && ca.a == cb.a;
        for (int y = y0; y <= y1; ++y) {
            const double py = y + 0.5;
            int sx0 = x0;
            int sx1 = x1;
            if (std::fabs(d.y) > 1e-9) {
                const double tA = (py - pad - a.y) / d.y;
                const double tB = (py + pad - a.y) / d.y;
                double xa = a.x + d.x * std::clamp(std::min(tA, tB), 0.0, 1.0);
                double xb = a.x + d.x * std::clamp(std::max(tA, tB), 0.0, 1.0);
                if (xa > xb) std::swap(xa, xb);
                sx0 = std::max(x0, static_cast<int>(std::floor(xa - pad)));
                sx1 = std::min(x1, static_cast<int>(std::ceil(xb + pad)));
            }
            for (int x = sx0; x <= sx1; ++x) {
                const Vec2 p{ x + 0.5, py };
                double t = len2 > 0.0 ? dot(p - a, d) / len2 : 0.0;
                t = std::clamp(t, 0.0, 1.0);
                const Vec2 q = a + d * t;
                const double dist = length(p - q);
                const double cov = std::clamp(r + 0.5 - dist, 0.0, 1.0);
                if (cov <= 0.0) continue;
                const std::size_t k = static_cast<std::size_t>(y) * m_img.w + x;
                if (cov > m_cov[k]) {
                    if (m_cov[k] == 0.0f) touch(x, y);
                    m_cov[k] = static_cast<float>(cov);
                    m_col[k] = same ? ca : mix(ca, cb, t);
                }
            }
        }
    }

    void flush() {
        if (m_x0 > m_x1) return;
        for (int y = m_y0; y <= m_y1; ++y) {
            for (int x = m_x0; x <= m_x1; ++x) {
                const std::size_t k = static_cast<std::size_t>(y) * m_img.w + x;
                if (m_cov[k] > 0.0f) {
                    m_img.blend(x, y, m_col[k], m_cov[k]);
                    m_cov[k] = 0.0f;
                }
            }
        }
        m_x0 = m_y0 = std::numeric_limits<int>::max();
        m_x1 = m_y1 = std::numeric_limits<int>::min();
    }

    static bool clip(Vec2& a, Vec2& b, double xmin, double ymin, double xmax, double ymax) {
        double t0 = 0.0, t1 = 1.0;
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double p[4] = { -dx, dx, -dy, dy };
        const double q[4] = { a.x - xmin, xmax - a.x, a.y - ymin, ymax - a.y };
        for (int i = 0; i < 4; ++i) {
            if (p[i] == 0.0) {
                if (q[i] < 0.0) return false;
                continue;
            }
            const double t = q[i] / p[i];
            if (p[i] < 0.0) t0 = std::max(t0, t);
            else t1 = std::min(t1, t);
            if (t0 > t1) return false;
        }
        const Vec2 A = a;
        a = Vec2{ std::clamp(A.x + dx * t0, xmin, xmax), std::clamp(A.y + dy * t0, ymin, ymax) };
        b = Vec2{ std::clamp(A.x + dx * t1, xmin, xmax), std::clamp(A.y + dy * t1, ymin, ymax) };
        return true;
    }

private:
    void touch(int x, int y) {
        m_x0 = std::min(m_x0, x);
        m_x1 = std::max(m_x1, x);
        m_y0 = std::min(m_y0, y);
        m_y1 = std::max(m_y1, y);
    }

    Image& m_img;
    std::unique_ptr<LayerBuffers> m_buf;
    std::vector<float>& m_cov;
    std::vector<Rgba>& m_col;
    int m_x0 = std::numeric_limits<int>::max();
    int m_y0 = std::numeric_limits<int>::max();
    int m_x1 = std::numeric_limits<int>::min();
    int m_y1 = std::numeric_limits<int>::min();
};

void polyline(StrokeLayer& layer, const std::vector<Vec2>& pts, const std::vector<Rgba>& colors, Rgba color, double width, bool dashed, double ui) {
    const bool per = colors.size() == pts.size();
    const double on = 7.0 * ui;
    const double off = 5.0 * ui;
    double phase = 0.0;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        Vec2 a = pts[i - 1];
        Vec2 b = pts[i];
        if (!finite(a) || !finite(b)) {
            phase = 0.0;
            continue;
        }
        const Rgba ca = per ? colors[i - 1] : color;
        const Rgba cb = per ? colors[i] : color;
        if (!dashed) {
            layer.segment(a, b, width, ca, cb);
            continue;
        }
        const Vec2 a0 = a;
        const Vec2 b0 = b;
        Vec2 ac = a;
        Vec2 bc = b;
        if (!layer.visible_part(ac, bc, width)) {
            phase = 0.0;
            continue;
        }
        const double full = length(b0 - a0);
        const double t0 = full > 0.0 ? length(ac - a0) / full : 0.0;
        const double t1 = full > 0.0 ? length(bc - a0) / full : 1.0;
        const Rgba cac = mix(ca, cb, t0);
        const Rgba cbc = mix(ca, cb, t1);
        a = ac;
        b = bc;
        const double len = length(b - a);
        if (!(len < 1e7)) continue;
        double s = 0.0;
        while (s < len) {
            const double cycle = std::fmod(phase, on + off);
            const double left = cycle < on ? on - cycle : on + off - cycle;
            const double e = std::min(len, s + std::max(left, 1e-3));
            if (cycle < on) layer.segment(a + (b - a) * (s / len), a + (b - a) * (e / len), width, mix(cac, cbc, s / len), mix(cac, cbc, e / len));
            phase = std::fmod(phase + (e - s), on + off);
            s = e;
        }
    }
}

void fill_triangle(Image& img, Vec2 a, Vec2 b, Vec2 c, Rgba color) {
    if (!finite(a) || !finite(b) || !finite(c)) return;
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min({ a.x, b.x, c.x }))));
    const int x1 = std::min(img.w - 1, static_cast<int>(std::ceil(std::max({ a.x, b.x, c.x }))));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min({ a.y, b.y, c.y }))));
    const int y1 = std::min(img.h - 1, static_cast<int>(std::ceil(std::max({ a.y, b.y, c.y }))));
    if (x0 > x1 || y0 > y1) return;
    const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::fabs(area) < 1e-12) return;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double px = x + 0.5, py = y + 0.5;
            double w0 = (b.x - px) * (c.y - py) - (b.y - py) * (c.x - px);
            double w1 = (c.x - px) * (a.y - py) - (c.y - py) * (a.x - px);
            double w2 = (a.x - px) * (b.y - py) - (a.y - py) * (b.x - px);
            if (area < 0) {
                w0 = -w0;
                w1 = -w1;
                w2 = -w2;
            }
            if (w0 >= 0 && w1 >= 0 && w2 >= 0) img.blend(x, y, color, 1.0);
        }
    }
}

void fill_circle(Image& img, Vec2 c, double r, Rgba color, bool hollow = false, double ring = 1.5) {
    if (!finite(c)) return;
    const int x0 = std::max(0, static_cast<int>(std::floor(c.x - r - 1)));
    const int x1 = std::min(img.w - 1, static_cast<int>(std::ceil(c.x + r + 1)));
    const int y0 = std::max(0, static_cast<int>(std::floor(c.y - r - 1)));
    const int y1 = std::min(img.h - 1, static_cast<int>(std::ceil(c.y + r + 1)));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double d = std::hypot(x + 0.5 - c.x, y + 0.5 - c.y);
            double cov = std::clamp(r + 0.5 - d, 0.0, 1.0);
            if (hollow) cov = std::min(cov, std::clamp(d - (r - ring) + 0.5, 0.0, 1.0));
            if (cov > 0.0) img.blend(x, y, color, cov);
        }
    }
}

void fill_rect(Image& img, int x0, int y0, int x1, int y1, Rgba color) {
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(img.w, x1);
    y1 = std::min(img.h, y1);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) img.blend(x, y, color, 1.0);
    }
}

void hline(Image& img, int y, Rgba c) {
    if (y < 0 || y >= img.h) return;
    for (int x = 0; x < img.w; ++x) img.blend(x, y, c, 1.0);
}

void vline(Image& img, int x, Rgba c) {
    if (x < 0 || x >= img.w) return;
    for (int y = 0; y < img.h; ++y) img.blend(x, y, c, 1.0);
}

const std::map<std::string, std::vector<std::vector<Vec2>>>& glyphs() {
    static const std::map<std::string, std::vector<std::vector<Vec2>>> g = [] {
        std::map<std::string, std::string> src = {
            { "0", "1,0 3,0 4,1 4,5 3,6 1,6 0,5 0,1 1,0" },
            { "1", "1,1 2,0 2,6|1,6 3,6" },
            { "2", "0,1 1,0 3,0 4,1 4,2 0,6 4,6" },
            { "3", "0,0 4,0 2,2.5 3,2.5 4,3.5 4,5 3,6 1,6 0,5" },
            { "4", "3,6 3,0 0,4 4,4" },
            { "5", "4,0 0,0 0,2.8 3,2.8 4,3.8 4,5 3,6 0,6" },
            { "6", "3.5,0 1,0 0,1 0,5 1,6 3,6 4,5 4,4 3,3 0,3" },
            { "7", "0,0 4,0 1.2,6" },
            { "8", "1,0 3,0 4,1 4,2 3,3 1,3 0,4 0,5 1,6 3,6 4,5 4,4 3,3|1,3 0,2 0,1 1,0" },
            { "9", "4,3 1,3 0,2 0,1 1,0 3,0 4,1 4,5 3,6 0.5,6" },
            { ".", "1.6,5.6 2.4,5.6 2.4,6 1.6,6 1.6,5.6" },
            { "-", "0.6,3.5 3.4,3.5" },
            { "+", "0.5,3.5 3.5,3.5|2,2 2,5" },
            { "=", "0.5,2.6 3.5,2.6|0.5,4.4 3.5,4.4" },
            { "e", "0,4 4,4 4,3 3,2 1,2 0,3 0,5 1,6 3.8,6" },
            { "E", "4,0 0,0 0,6 4,6|0,3 3,3" },
            { "x", "0.3,2 3.7,6|3.7,2 0.3,6" },
            { "y", "0.3,2 2,5.6|3.7,2 1.6,7.6" },
            { "z", "0.3,2 3.7,2 0.3,6 3.7,6" },
            { "u", "0.3,2 0.3,5 1.2,6 2.8,6 3.7,5|3.7,2 3.7,6" },
            { "v", "0.3,2 2,6 3.7,2" },
            { "w", "0,2 1,6 2,3.2 3,6 4,2" },
            { "t", "1.5,0.4 1.5,5 2.4,6 3.6,6|0.3,2 3.4,2" },
            { "r", "0.6,2 0.6,6|0.6,4 2.2,2.2 3.8,2.2" },
            { "i", "2,2.2 2,6|2,0.6 2,1.1" },
            { "n", "0.4,6 0.4,2|0.4,3 1.4,2 2.8,2 3.6,3 3.6,6" },
            { "f", "3.6,0.4 2.6,0 1.8,0.6 1.8,6|0.4,2.4 3.2,2.4" },
            { "s", "3.6,2.2 1,2.2 0.4,3 1,4 3,4 3.6,5 3,6 0.4,6" },
            { "a", "0.6,2.2 3,2.2 3.6,3 3.6,6|3.6,4 1,4 0.4,5 1,6 3.6,6" },
            { "(", "3,-0.4 1.6,1.4 1.6,4.6 3,6.4" },
            { ")", "1,-0.4 2.4,1.4 2.4,4.6 1,6.4" },
            { ",", "2.2,5.2 2.2,6.2 1.4,7.2" },
            { ":", "2,2.2 2,2.8|2,5.4 2,6" },
            { "/", "3.6,0 0.4,6" },
            { "|", "2,-0.4 2,6.6" },
            { "T", "0,0 4,0|2,0 2,6" },
            { "N", "0.2,6 0.2,0 3.8,6 3.8,0" },
            { "B", "0.3,0 0.3,6 3,6 4,5 4,4 3,3 0.3,3|0.3,0 3,0 3.8,0.8 3.8,2.2 3,3" },
            { "R", "0.3,6 0.3,0 3,0 4,1 4,2 3,3 0.3,3|2,3 4,6" },
            { "I", "0.8,0 3.2,0|2,0 2,6|0.8,6 3.2,6" },
            { "m", "0.2,6 0.2,2|0.2,3 1,2 1.8,3 1.8,6|1.8,3 2.8,2 3.8,3 3.8,6" },
            { "\xCF\x80", "0,2 4,2|1.2,2 1.2,6|2.9,2 2.9,6" },
            { "o", "1,2.2 3,2.2 3.8,3 3.8,5 3,6 1,6 0.2,5 0.2,3 1,2.2" },
            { "l", "2,0 2,6" },
            { "c", "3.8,2.6 3,2.2 1,2.2 0.2,3 0.2,5 1,6 3,6 3.8,5.6" },
            { "d", "3.8,0 3.8,6|3.8,3 3,2.2 1,2.2 0.2,3 0.2,5 1,6 3,6 3.8,5" },
            { "b", "0.2,0 0.2,6|0.2,3 1,2.2 3,2.2 3.8,3 3.8,5 3,6 1,6 0.2,5" },
            { "g", "3.8,2.2 3.8,7 3,7.8 1,7.8|3.8,3 3,2.2 1,2.2 0.2,3 0.2,5 1,6 3,6 3.8,5" },
            { "h", "0.4,0 0.4,6|0.4,3 1.4,2.2 2.8,2.2 3.6,3 3.6,6" },
            { "k", "0.6,0 0.6,6|3.6,2.2 0.6,4.4|1.6,3.6 3.8,6" },
            { "p", "0.4,7.8 0.4,2.2|0.4,3 1.2,2.2 3,2.2 3.8,3 3.8,5 3,6 1.2,6 0.4,5" },
            { "q", "3.6,7.8 3.6,2.2|3.6,3 2.8,2.2 1,2.2 0.2,3 0.2,5 1,6 2.8,6 3.6,5" },
            { "j", "2.6,2.2 2.6,7 1.8,7.8 0.6,7.8|2.6,0.6 2.6,1.1" },
            { "A", "0,6 2,0 4,6|0.8,4 3.2,4" },
            { "C", "4,0.6 3,0 1,0 0,1 0,5 1,6 3,6 4,5.4" },
            { "D", "0.3,0 0.3,6 2.6,6 4,4.6 4,1.4 2.6,0 0.3,0" },
            { "F", "4,0 0.3,0 0.3,6|0.3,3 3,3" },
            { "J", "1,0 4,0|3,0 3,5 2,6 1,6 0.2,5" },
            { "K", "0.3,0 0.3,6|4,0 0.3,3.6|1.6,2.6 4,6" },
            { "L", "0.3,0 0.3,6 4,6" },
            { "M", "0,6 0,0 2,3 4,0 4,6" },
            { "O", "1,0 3,0 4,1 4,5 3,6 1,6 0,5 0,1 1,0" },
            { "P", "0.3,6 0.3,0 3,0 4,1 4,2 3,3 0.3,3" },
            { "S", "4,0.8 3,0 1,0 0,1 0,2 1,3 3,3 4,4 4,5 3,6 1,6 0,5.2" },
            { "X", "0,0 4,6|4,0 0,6" },
            { "Y", "0,0 2,3 4,0|2,3 2,6" },
            { "Z", "0,0 4,0 0,6 4,6" },
            { "H", "0.3,0 0.3,6|3.7,0 3.7,6|0.3,3 3.7,3" },
            { "\xE2\x88\x87", "0,0.4 4,0.4 2,6 0,0.4" },
            { "\xE2\x80\xB2", "2.6,0 1.8,2" },
            { "\xE2\x82\x80", "1.4,5 2.6,5 3,5.6 3,6.6 2.6,7.2 1.4,7.2 1,6.6 1,5.6 1.4,5" },
            { "\xCE\x93", "4,0 0.3,0 0.3,6" },
            { "[", "3,-0.4 1.6,-0.4 1.6,6.6 3,6.6" },
            { "]", "1,-0.4 2.4,-0.4 2.4,6.6 1,6.6" },
            { "*", "2,1.6 2,4.4|0.8,2.3 3.2,3.7|3.2,2.3 0.8,3.7" },
            { "^", "0.8,2 2,0.4 3.2,2" },

            { "\xCE\xB8", "1,0 3,0 4,1 4,5 3,6 1,6 0,5 0,1 1,0|0,3 4,3" },
            { "\xCF\x86", "1,2.2 0.2,3.2 0.2,5 1,6 3,6 3.8,5 3.8,3.2 3,2.2 1,2.2|2,0.6 2,7.6" },
            { "\xCF\x81", "0.8,7.6 0.8,3 1.6,2.2 3,2.2 3.8,3 3.8,5 3,6 0.8,6" },
            { "\xCE\xBA", "0.6,2 0.6,6|3.6,2 0.6,4.4|1.6,3.6 3.8,6" },
            { "\xCF\x84", "0.2,2.2 3.8,2.2|2,2.2 2,6 3,6" },
        };
        std::map<std::string, std::vector<std::vector<Vec2>>> out;
        for (const auto& [ch, text] : src) {
            std::vector<std::vector<Vec2>> strokes;
            std::stringstream parts(text);
            std::string part;
            while (std::getline(parts, part, '|')) {
                std::vector<Vec2> pts;
                std::stringstream ps(part);
                std::string pair;
                while (ps >> pair) {
                    const std::size_t c = pair.find(',');
                    pts.push_back(Vec2{ std::stod(pair.substr(0, c)), std::stod(pair.substr(c + 1)) });
                }
                strokes.push_back(pts);
            }
            out[ch] = strokes;
        }
        return out;
    }();
    return g;
}

std::vector<std::string> utf8_chars(const std::string& s) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        n = std::min(n, s.size() - i);
        out.push_back(s.substr(i, n));
        i += n;
    }
    return out;
}

const double k_advance = 5.4;

} // namespace

double text_width(const std::string& text, double size) {
    const double k = size / 8.0;
    return utf8_chars(text).size() * k_advance * k;
}

void draw_text(Image& img, double x, double y, const std::string& text, double size, Rgba color, int align) {
    const double k = size / 8.0;
    const double w = text_width(text, size);
    double ox = align < 0 ? x : align == 0 ? x - 0.5 * w : x - w;
    const double oy = y - 6.0 * k;
    StrokeLayer halo(img);
    StrokeLayer layer(img);
    const double lw = std::max(1.0, 1.15 * k);
    const Rgba back = color.r + color.g + color.b > 384 ? rgba(0, 0, 0, 110) : rgba(255, 255, 255, 150);
    std::vector<std::pair<std::vector<Vec2>, double>> all;
    for (const std::string& ch : utf8_chars(text)) {
        auto it = glyphs().find(ch);
        if (it != glyphs().end()) {
            for (const std::vector<Vec2>& s : it->second) {
                std::vector<Vec2> pts;
                for (const Vec2& p : s) pts.push_back(Vec2{ ox + (p.x + 0.4) * k, oy + p.y * k });
                all.push_back({ pts, lw });
            }
        } else if (ch != " ") {
            all.push_back({ { Vec2{ ox + 0.6 * k, oy }, Vec2{ ox + 3.8 * k, oy }, Vec2{ ox + 3.8 * k, oy + 6 * k }, Vec2{ ox + 0.6 * k, oy + 6 * k }, Vec2{ ox + 0.6 * k, oy } }, lw * 0.7 });
        }
        ox += k_advance * k;
    }
    for (const auto& [pts, width] : all) {
        for (std::size_t i = 1; i < pts.size(); ++i) halo.segment(pts[i - 1], pts[i], width + 2.4, back, back);
    }
    halo.flush();
    for (const auto& [pts, width] : all) {
        for (std::size_t i = 1; i < pts.size(); ++i) layer.segment(pts[i - 1], pts[i], width, color, color);
    }
    layer.flush();
}

namespace {

double nice_step(double raw) {
    if (!(raw > 0.0) || !std::isfinite(raw)) return 1.0;
    const double p = std::pow(10.0, std::floor(std::log10(raw)));
    const double m = raw / p;
    if (m < 1.5) return p;
    if (m < 3.5) return 2.0 * p;
    if (m < 7.5) return 5.0 * p;
    return 10.0 * p;
}

std::string tick_text(double v, double step) {
    if (std::fabs(v) < step * 1e-6) return "0";
    const double a = std::fabs(v);
    if (a >= 1e6 || a < 1e-4) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.3g", v);
        std::string s = buf;
        const std::size_t e = s.find('e');
        if (e != std::string::npos) {
            std::string mant = s.substr(0, e);
            std::string ex = s.substr(e + 1);
            if (!ex.empty() && ex[0] == '+') ex = ex.substr(1);
            while (ex.size() > 1 && ex[0] == '0') ex = ex.substr(1);
            if (ex.size() > 2 && ex[0] == '-' && ex[1] == '0') ex = "-" + ex.substr(2);
            s = mant + "e" + ex;
        }
        return s;
    }
    int decimals = std::max(0, static_cast<int>(std::ceil(-std::log10(step) - 1e-9)));
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f", std::min(decimals, 10), v);
    return buf;
}

struct Map2 {
    WorldBox2 box;
    int w = 1;
    int h = 1;
    Vec2 operator()(Vec2 p) const {
        return Vec2{ (p.x - box.xmin) / (box.xmax - box.xmin) * w, (box.ymax - p.y) / (box.ymax - box.ymin) * h };
    }
    double sx(double x) const { return (x - box.xmin) / (box.xmax - box.xmin) * w; }
    double sy(double y) const { return (box.ymax - y) / (box.ymax - box.ymin) * h; }
};

void draw_grid2(Image& img, const Map2& m, const RenderOptions& o) {
    const double ui = o.ui_scale;
    const double ppx = m.w / (m.box.xmax - m.box.xmin);
    const double ppy = m.h / (m.box.ymax - m.box.ymin);
    const double sxs = nice_step(90.0 * ui / ppx);
    const double sys = o.view2.equal_axes ? sxs : nice_step(90.0 * ui / ppy);
    auto minor_of = [](double s) {
        const double p = std::pow(10.0, std::floor(std::log10(s) + 1e-9));
        return std::fabs(s / p - 2.0) < 1e-6 ? s / 4.0 : s / 5.0;
    };
    if (o.grid) {
        const double mx = minor_of(sxs);
        const double my = minor_of(sys);
        for (double x = std::ceil(m.box.xmin / mx) * mx; x <= m.box.xmax; x += mx) vline(img, static_cast<int>(std::floor(m.sx(x))), o.theme.grid_minor);
        for (double y = std::ceil(m.box.ymin / my) * my; y <= m.box.ymax; y += my) hline(img, static_cast<int>(std::floor(m.sy(y))), o.theme.grid_minor);
        for (double x = std::ceil(m.box.xmin / sxs) * sxs; x <= m.box.xmax; x += sxs) vline(img, static_cast<int>(std::floor(m.sx(x))), o.theme.grid_major);
        for (double y = std::ceil(m.box.ymin / sys) * sys; y <= m.box.ymax; y += sys) hline(img, static_cast<int>(std::floor(m.sy(y))), o.theme.grid_major);
    }
    if (!o.axes) return;
    const int ax = static_cast<int>(std::floor(m.sx(0.0)));
    const int ay = static_cast<int>(std::floor(m.sy(0.0)));
    for (int k = 0; k < static_cast<int>(std::max(1.0, std::round(1.5 * ui))); ++k) {
        vline(img, ax + k, o.theme.axis);
        hline(img, ay + k, o.theme.axis);
    }
    if (!o.labels) return;
    const double size = 11.0 * ui;
    const double ly = std::clamp(static_cast<double>(ay) + 6.0 * ui + size * 0.75, size + 2.0, m.h - 4.0 * ui);
    for (double x = std::ceil(m.box.xmin / sxs) * sxs; x <= m.box.xmax; x += sxs) {
        if (std::fabs(x) < sxs * 1e-6) continue;
        const double px = m.sx(x);
        if (px < 14 * ui || px > m.w - 14 * ui) continue;
        draw_text(img, px, ly, tick_text(x, sxs), size, o.theme.text, 0);
    }
    const double lx = std::clamp(static_cast<double>(ax) - 6.0 * ui, 4.0 * ui + 40.0 * ui, static_cast<double>(m.w) - 4.0 * ui);
    for (double y = std::ceil(m.box.ymin / sys) * sys; y <= m.box.ymax; y += sys) {
        if (std::fabs(y) < sys * 1e-6) continue;
        const double py = m.sy(y);
        if (py < 12 * ui || py > m.h - 12 * ui) continue;
        const std::string t = tick_text(y, sys);
        const bool left_side = ax > 46 * ui;
        if (left_side) draw_text(img, lx, py + size * 0.35, t, size, o.theme.text, 1);
        else draw_text(img, std::max(ax, 0) + 6.0 * ui, py + size * 0.35, t, size, o.theme.text, -1);
    }
    if (m.box.xmin < 0 && m.box.xmax > 0 && m.box.ymin < 0 && m.box.ymax > 0) draw_text(img, ax - 5.0 * ui, ay + size * 1.2, "0", size, o.theme.text, 1);
    draw_text(img, m.w - 8.0 * ui, std::clamp(static_cast<double>(ay) - 6.0 * ui, size + 2.0, m.h - 4.0), "x", size * 1.2, o.theme.text, 1);
    draw_text(img, std::clamp(static_cast<double>(ax) + 8.0 * ui, 4.0, m.w - 20.0), size * 1.4, "y", size * 1.2, o.theme.text, -1);
}

void draw_arrow_head(Image& img, Vec2 from, Vec2 to, double width, Rgba color, StrokeLayer& layer) {
    const Vec2 d = to - from;
    const double len = length(d);
    if (!(len > 0.5)) return;
    const Vec2 u = d * (1.0 / len);
    const Vec2 n{ -u.y, u.x };
    const double hl = std::min(len * 0.6, 3.5 * width + 5.0);
    const double hw = hl * 0.45;
    const Vec2 base = to - u * hl;
    layer.segment(from, base + u * 0.5, width, color, color);
    layer.flush();
    fill_triangle(img, to, base + n * hw, base - n * hw, color);
    StrokeLayer edge(img);
    edge.segment(to, base + n * hw, 1.0, color, color);
    edge.segment(to, base - n * hw, 1.0, color, color);
    edge.flush();
}

void draw_insets(Image& img, const Scene2& s, const RenderOptions& o) {
    const double ui = o.ui_scale;
    const int pw = static_cast<int>(std::max(190.0 * ui, img.w * 0.3));
    const int ph = static_cast<int>(std::max(110.0 * ui, img.h * 0.24));
    int bottom = img.h - static_cast<int>(10 * ui);
    for (const Inset2& in : s.insets) {
        const int x0 = img.w - pw - static_cast<int>(10 * ui);
        const int y0 = bottom - ph;
        if (y0 < 0) break;
        fill_rect(img, x0, y0, x0 + pw, y0 + ph, with_alpha(o.theme.background, 0.92));
        StrokeLayer border(img);
        const Rgba bc = o.theme.box;
        border.segment(Vec2{ double(x0), double(y0) }, Vec2{ double(x0 + pw), double(y0) }, 1.0, bc, bc);
        border.segment(Vec2{ double(x0 + pw), double(y0) }, Vec2{ double(x0 + pw), double(y0 + ph) }, 1.0, bc, bc);
        border.segment(Vec2{ double(x0 + pw), double(y0 + ph) }, Vec2{ double(x0), double(y0 + ph) }, 1.0, bc, bc);
        border.segment(Vec2{ double(x0), double(y0 + ph) }, Vec2{ double(x0), double(y0) }, 1.0, bc, bc);
        border.flush();
        double xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300;
        for (const Vec2& p : in.pts) {
            if (!finite(p)) continue;
            xmin = std::min(xmin, p.x);
            xmax = std::max(xmax, p.x);
            ymin = std::min(ymin, p.y);
            ymax = std::max(ymax, p.y);
        }
        const double size = 10.0 * ui;
        draw_text(img, x0 + 6 * ui, y0 + 14 * ui, in.title, size, o.theme.text, -1);
        if (xmax > xmin) {
            if (!(ymax > ymin)) {
                ymin -= 1.0;
                ymax += 1.0;
            }
            const double pad = 0.08 * (ymax - ymin);
            ymin -= pad;
            ymax += pad;
            const double ix0 = x0 + 8 * ui, ix1 = x0 + pw - 8 * ui;
            const double iy0 = y0 + 22 * ui, iy1 = y0 + ph - 16 * ui;
            auto map = [&](Vec2 p) { return Vec2{ ix0 + (p.x - xmin) / (xmax - xmin) * (ix1 - ix0), iy1 - (p.y - ymin) / (ymax - ymin) * (iy1 - iy0) }; };
            StrokeLayer ax(img);
            if (ymin < 0 && ymax > 0) ax.segment(map(Vec2{ xmin, 0 }), map(Vec2{ xmax, 0 }), 1.0, o.theme.axis, o.theme.axis);
            ax.flush();
            if (in.has_marker && std::isfinite(in.marker)) {
                StrokeLayer mk(img);
                mk.segment(map(Vec2{ in.marker, ymin }), map(Vec2{ in.marker, ymax }), 1.0, o.theme.accent, o.theme.accent);
                mk.flush();
            }
            std::vector<Vec2> pts;
            for (const Vec2& p : in.pts) pts.push_back(finite(p) ? map(p) : Vec2{ k_nan, k_nan });
            StrokeLayer line(img);
            polyline(line, pts, in.colors, in.color, 2.0 * ui, false, ui);
            line.flush();
            draw_text(img, x0 + pw - 6 * ui, y0 + 14 * ui, tick_text(ymin + pad, (ymax - ymin) / 4) + " to " + tick_text(ymax - pad, (ymax - ymin) / 4), size * 0.9, with_alpha(o.theme.text, 0.7), 1);
            draw_text(img, x0 + pw - 6 * ui, y0 + ph - 4 * ui, in.xlabel, size * 0.9, with_alpha(o.theme.text, 0.7), 1);
        }
        bottom = y0 - static_cast<int>(8 * ui);
    }
}

void render2d(const Scene& scene, const RenderOptions& o, Image& img) {
    const Map2 m{ world_box(o.view2, img.w, img.h), img.w, img.h };
    const double ui = o.ui_scale;
    const Scene2& s = scene.s2;
    for (const Raster2& r : s.rasters) {
        if (r.w <= 0 || r.h <= 0 || r.px.size() < static_cast<std::size_t>(r.w) * r.h) continue;
        const int x0 = std::max(0, static_cast<int>(std::floor(m.sx(r.xmin))));
        const int x1 = std::min(img.w, static_cast<int>(std::ceil(m.sx(r.xmax))));
        const int y0 = std::max(0, static_cast<int>(std::floor(m.sy(r.ymax))));
        const int y1 = std::min(img.h, static_cast<int>(std::ceil(m.sy(r.ymin))));
        for (int y = y0; y < y1; ++y) {
            const double wy = m.box.ymax - (y + 0.5) / img.h * (m.box.ymax - m.box.ymin);
            const int j = static_cast<int>((r.ymax - wy) / (r.ymax - r.ymin) * r.h);
            if (j < 0 || j >= r.h) continue;
            for (int x = x0; x < x1; ++x) {
                const double wx = m.box.xmin + (x + 0.5) / img.w * (m.box.xmax - m.box.xmin);
                const int i = static_cast<int>((wx - r.xmin) / (r.xmax - r.xmin) * r.w);
                if (i < 0 || i >= r.w) continue;
                img.blend(x, y, r.px[static_cast<std::size_t>(j) * r.w + i], 1.0);
            }
        }
    }
    for (const Tri2& t : s.tris) fill_triangle(img, m(t.a), m(t.b), m(t.c), t.color);
    draw_grid2(img, m, o);
    StrokeLayer layer(img);
    for (const Stroke2& st : s.strokes) {
        std::vector<Vec2> pts;
        pts.reserve(st.pts.size());
        for (const Vec2& p : st.pts) pts.push_back(finite(p) ? m(p) : Vec2{ k_nan, k_nan });
        polyline(layer, pts, st.colors, st.color, st.width * ui, st.dashed, ui);
        layer.flush();
    }
    for (const Arrow2& a : s.arrows) {
        const Vec2 f = m(a.from);
        const Vec2 t = m(a.to);
        if (!finite(f) || !finite(t)) continue;
        if (a.head) draw_arrow_head(img, f, t, a.width * ui, a.color, layer);
        else {
            layer.segment(f, t, a.width * ui, a.color, a.color);
            layer.flush();
        }
    }
    for (const Dot2& d : s.dots) {
        const Vec2 p = m(d.at);
        if (!finite(p)) continue;
        if (!d.hollow) fill_circle(img, p, (d.radius + 1.5) * ui, with_alpha(o.theme.background, 0.9));
        fill_circle(img, p, d.radius * ui, d.color, d.hollow, 2.0 * ui);
        if (!d.label.empty() && o.labels) draw_text(img, p.x + (d.radius + 4) * ui, p.y - (d.radius + 3) * ui, d.label, 11.0 * ui, o.theme.text, -1);
    }
    for (const Text2& t : s.texts) {
        const Vec2 p = m(t.at);
        if (finite(p)) draw_text(img, p.x, p.y, t.text, 11.0 * ui, t.color, 0);
    }
    if (s.clipped) {
        const double fx0 = std::max(-1.0, std::min(img.w + 1.0, m.sx(std::max(s.clip_xmin, m.box.xmin - 1.0))));
        const double fx1 = std::max(-1.0, std::min(img.w + 1.0, m.sx(std::min(s.clip_xmax, m.box.xmax + 1.0))));
        const double fy0 = std::max(-1.0, std::min(img.h + 1.0, m.sy(std::min(s.clip_ymax, m.box.ymax + 1.0))));
        const double fy1 = std::max(-1.0, std::min(img.h + 1.0, m.sy(std::max(s.clip_ymin, m.box.ymin - 1.0))));
        const Rgba bg = with_alpha(o.theme.background, 1.0);
        for (int y = 0; y < img.h; ++y) {
            for (int x = 0; x < img.w; ++x) {
                const double cx = x + 0.5;
                const double cy = y + 0.5;
                if (cx >= fx0 && cx <= fx1 && cy >= fy0 && cy <= fy1) continue;
                img.blend(x, y, bg, 0.93);
            }
        }
        StrokeLayer edge(img);
        const Rgba ec = with_alpha(o.theme.text, 0.35);
        const std::vector<Vec2> frame = { Vec2{ fx0, fy0 }, Vec2{ fx1, fy0 }, Vec2{ fx1, fy1 }, Vec2{ fx0, fy1 }, Vec2{ fx0, fy0 } };
        polyline(edge, frame, {}, ec, 1.2 * ui, false, ui);
        edge.flush();
    }
    draw_insets(img, s, o);
}

struct ZBuffer {
    int w = 0;
    int h = 0;
    std::vector<float> z;
    void reset(int width, int height) {
        w = width;
        h = height;
        z.assign(static_cast<std::size_t>(w) * h, std::numeric_limits<float>::infinity());
    }
};

struct PV {
    double x = 0.0;
    double y = 0.0;
    double d = 0.0;
    bool ok = false;
};

Rgba shade(Rgba base, Vec3 nv, double alpha_k, bool lit) {
    if (!lit) return base;
    const Vec3 L = normalized(Vec3{ -0.35, 0.62, -0.7 });
    const Vec3 V{ 0.0, 0.0, -1.0 };
    if (nv.z > 0.0) nv = -nv;
    const double diff = std::max(0.0, dot(nv, L));
    const Vec3 R = nv * (2.0 * dot(nv, L)) - L;
    const double spec = std::pow(std::max(0.0, dot(R, V)), 28.0) * 0.28;
    const double k = 0.38 + 0.7 * diff;
    Rgba c = rgba(static_cast<int>(base.r * k + 255 * spec), static_cast<int>(base.g * k + 255 * spec), static_cast<int>(base.b * k + 255 * spec), base.a);
    (void)alpha_k;
    return c;
}

void raster_tri(Image& img, ZBuffer& zb, const PV& a, const PV& b, const PV& c, Rgba ca, Rgba cb, Rgba cc, double alpha, bool write_depth) {
    if (!a.ok || !b.ok || !c.ok) return;
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min({ a.x, b.x, c.x }))));
    const int x1 = std::min(img.w - 1, static_cast<int>(std::ceil(std::max({ a.x, b.x, c.x }))));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min({ a.y, b.y, c.y }))));
    const int y1 = std::min(img.h - 1, static_cast<int>(std::ceil(std::max({ a.y, b.y, c.y }))));
    if (x0 > x1 || y0 > y1) return;
    const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::fabs(area) < 1e-12) return;
    const double inv = 1.0 / area;
    for (int y = y0; y <= y1; ++y) {
        const double py = y + 0.5;
        for (int x = x0; x <= x1; ++x) {
            const double px = x + 0.5;
            const double w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) * inv;
            const double w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) * inv;
            const double w2 = 1.0 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            const float depth = static_cast<float>(w0 * a.d + w1 * b.d + w2 * c.d);
            float& zz = zb.z[static_cast<std::size_t>(y) * zb.w + x];
            if (depth >= zz) continue;
            if (write_depth) zz = depth;
            const Rgba col = rgba(static_cast<int>(w0 * ca.r + w1 * cb.r + w2 * cc.r), static_cast<int>(w0 * ca.g + w1 * cb.g + w2 * cc.g), static_cast<int>(w0 * ca.b + w1 * cb.b + w2 * cc.b), 255);
            img.blend(x, y, col, alpha * (w0 * ca.a + w1 * cb.a + w2 * cc.a) / 255.0);
        }
    }
}

void line3(Image& img, ZBuffer& zb, const PV& a, const PV& b, double width, Rgba ca, Rgba cb, bool on_top) {
    if (!a.ok || !b.ok) return;
    if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(a.d) || !std::isfinite(b.d)) return;
    const double len = std::hypot(b.x - a.x, b.y - a.y);
    if (!(len <= 8.0 * (img.w + img.h))) return;
    const double r = std::max(0.5, 0.5 * width);
    const int steps = std::max(1, static_cast<int>(std::ceil(len / std::max(0.5, r * 0.6))));
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min(a.x, b.x) - r - 1)));
    const int x1 = std::min(img.w - 1, static_cast<int>(std::ceil(std::max(a.x, b.x) + r + 1)));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min(a.y, b.y) - r - 1)));
    const int y1 = std::min(img.h - 1, static_cast<int>(std::ceil(std::max(a.y, b.y) + r + 1)));
    if (x0 > x1 || y0 > y1) return;
    (void)steps;
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    const double bias = 0.015;
    for (int y = y0; y <= y1; ++y) {
        const double py = y + 0.5;
        int sx0 = x0, sx1 = x1;
        if (std::fabs(dy) > 1e-9) {
            const double tA = (py - r - 1 - a.y) / dy;
            const double tB = (py + r + 1 - a.y) / dy;
            double xa = a.x + dx * std::clamp(std::min(tA, tB), 0.0, 1.0);
            double xb = a.x + dx * std::clamp(std::max(tA, tB), 0.0, 1.0);
            if (xa > xb) std::swap(xa, xb);
            sx0 = std::max(x0, static_cast<int>(std::floor(xa - r - 1)));
            sx1 = std::min(x1, static_cast<int>(std::ceil(xb + r + 1)));
        }
        for (int x = sx0; x <= sx1; ++x) {
            const double px = x + 0.5;
            double t = len2 > 0 ? ((px - a.x) * dx + (py - a.y) * dy) / len2 : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            const double qx = a.x + dx * t, qy = a.y + dy * t;
            const double dist = std::hypot(px - qx, py - qy);
            const double cov = std::clamp(r + 0.5 - dist, 0.0, 1.0);
            if (cov <= 0.0) continue;
            const float depth = static_cast<float>(a.d + (b.d - a.d) * t - bias);
            float& zz = zb.z[static_cast<std::size_t>(y) * zb.w + x];
            if (!on_top && depth > zz) continue;
            img.blend(x, y, mix(ca, cb, t), cov);
        }
    }
}

bool clip_box(Vec3& a, Vec3& b, double lim) {
    double t0 = 0.0, t1 = 1.0;
    const Vec3 d = b - a;
    const double p[6] = { -d.x, d.x, -d.y, d.y, -d.z, d.z };
    const double q[6] = { a.x + lim, lim - a.x, a.y + lim, lim - a.y, a.z + lim, lim - a.z };
    for (int i = 0; i < 6; ++i) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) return false;
            continue;
        }
        const double t = q[i] / p[i];
        if (p[i] < 0.0) t0 = std::max(t0, t);
        else t1 = std::min(t1, t);
        if (t0 > t1) return false;
    }
    const Vec3 A = a;
    a = A + d * t0;
    b = A + d * t1;
    return true;
}

} // namespace

Vec3 Camera3::normalize(Vec3 p) const {
    const double zc = 0.5 * (zmin + zmax);
    const double zh = std::max(1e-12, 0.5 * (zmax - zmin));
    return Vec3{ (p.x - target.x) / range, (p.y - target.y) / (range_y > 0.0 ? range_y : range), (p.z - zc) / zh };
}

Vec3 Camera3::view(Vec3 n) const {
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double x1 = n.x * cy - n.y * sy;
    const double y1 = n.x * sy + n.y * cy;
    const double z1 = n.z;
    return Vec3{ x1, z1 * cp + y1 * sp, y1 * cp - z1 * sp };
}

bool Camera3::project(Vec3 world, double& sx, double& sy, double& depth) const {
    if (!finite(world)) return false;
    const Vec3 v = view(normalize(world));
    const double ey = std::fabs(std::cos(pitch)) + 1.4142135623730951 * std::fabs(std::sin(pitch));
    const double grow = perspective ? 1.3 : 1.0;
    const double fit = std::min(0.46 * width / (1.4142135623730951 * grow), 0.44 * height / (ey * grow));
    const double f = std::min(std::min(width, height) * 0.3, fit) * zoom;
    const double dist = 5.0;
    const double k = perspective ? dist / std::max(0.2, dist + v.z) : 1.0;
    sx = 0.5 * width + f * v.x * k;
    sy = 0.5 * height - f * v.y * k;
    depth = v.z;
    return std::isfinite(sx) && std::isfinite(sy);
}

Camera3 make_camera(const View3D& v, const Scene& scene, int width, int height) {
    Camera3 c;
    c.yaw = v.yaw;
    c.pitch = v.pitch;
    c.zoom = v.zoom;
    c.perspective = v.perspective;
    c.target = v.target;
    c.range = std::max(1e-9, v.range);
    c.range_y = v.range_y > 0.0 ? std::max(1e-9, v.range_y) : c.range;
    c.zmin = scene.s3.has_z ? scene.s3.zmin : v.target.z - v.zrange;
    c.zmax = scene.s3.has_z ? scene.s3.zmax : v.target.z + v.zrange;
    if (!std::isfinite(c.zmin) || !std::isfinite(c.zmax) || !(c.zmax > c.zmin)) {
        c.zmin = v.target.z - v.zrange;
        c.zmax = v.target.z + v.zrange;
    }
    if (!std::isfinite(c.zmin) || !std::isfinite(c.zmax) || !(c.zmax > c.zmin)) {
        c.zmin = -1.0;
        c.zmax = 1.0;
    }
    c.width = width;
    c.height = height;
    return c;
}

namespace {

void render3d(const Scene& scene, const RenderOptions& o, Image& out) {
    const int ss = std::clamp(o.supersample, 1, 3);
    Image img;
    img.resize(out.w * ss, out.h * ss);
    img.clear(o.theme.background);
    ZBuffer zb;
    zb.reset(img.w, img.h);
    const Camera3 cam = make_camera(o.view3, scene, img.w, img.h);
    const double ui = o.ui_scale * ss;
    auto proj_n = [&](Vec3 n) {
        PV p;
        const double zc = 0.5 * (cam.zmin + cam.zmax);
        const double zh = 0.5 * (cam.zmax - cam.zmin);
        const Vec3 w{ cam.target.x + n.x * cam.range, cam.target.y + n.y * cam.range_y, zc + n.z * zh };
        p.ok = cam.project(w, p.x, p.y, p.d);
        return p;
    };
    auto proj = [&](Vec3 w) {
        PV p;
        p.ok = cam.project(w, p.x, p.y, p.d);
        return p;
    };
    const double zh = 0.5 * (cam.zmax - cam.zmin);
    auto view_normal = [&](Vec3 n) { return normalized(cam.view(Vec3{ n.x * cam.range, n.y * cam.range_y, n.z * zh })); };

    auto draw_mesh = [&](const Mesh3& m, bool transparent) {
        if (m.tri.empty() || m.pos.empty()) return;
        std::vector<PV> pv(m.pos.size());
        for (std::size_t i = 0; i < m.pos.size(); ++i) {
            const Vec3 n = cam.normalize(m.pos[i]);
            if (!finite(n) || std::fabs(n.x) > 1.0005 || std::fabs(n.y) > 1.0005 || std::fabs(n.z) > 1.0005) continue;
            pv[i] = proj(m.pos[i]);
        }
        const bool have_n = m.nrm.size() == m.pos.size();
        const bool have_c = m.col.size() == m.pos.size();
        std::vector<Rgba> lit(m.pos.size());
        if (have_n) {
            for (std::size_t i = 0; i < m.pos.size(); ++i) {
                if (!pv[i].ok) continue;
                const Rgba base = have_c ? m.col[i] : rgba(120, 160, 230);
                lit[i] = shade(base, view_normal(m.nrm[i]), m.alpha, m.lit);
            }
        }
        for (std::size_t t = 0; t + 2 < m.tri.size(); t += 3) {
            const std::uint32_t i0 = m.tri[t], i1 = m.tri[t + 1], i2 = m.tri[t + 2];
            if (i0 >= pv.size() || i1 >= pv.size() || i2 >= pv.size()) continue;
            if (!pv[i0].ok || !pv[i1].ok || !pv[i2].ok) continue;
            Rgba c0, c1, c2;
            if (have_n) {
                c0 = lit[i0];
                c1 = lit[i1];
                c2 = lit[i2];
            } else {
                const Vec3 fn = cross(cam.normalize(m.pos[i1]) - cam.normalize(m.pos[i0]), cam.normalize(m.pos[i2]) - cam.normalize(m.pos[i0]));
                const Vec3 vn = normalized(cam.view(fn));
                const Rgba base = have_c ? m.col[i0] : rgba(120, 160, 230);
                c0 = c1 = c2 = shade(base, vn, m.alpha, m.lit);
            }
            raster_tri(img, zb, pv[i0], pv[i1], pv[i2], c0, c1, c2, transparent ? m.alpha : 1.0, !transparent);
        }
    };
    for (const Mesh3& m : scene.s3.meshes) {
        if (m.alpha >= 0.999) draw_mesh(m, false);
    }

    const Rgba box_col = o.theme.box;
    auto seg_n = [&](Vec3 a, Vec3 b, Rgba c, double w, bool top = false) { line3(img, zb, proj_n(a), proj_n(b), w, c, c, top); };
    double tick_step = nice_step(2.0 * cam.range / 5.0);
    double ytick = nice_step(2.0 * cam.range_y / 5.0);
    double ztick = nice_step((cam.zmax - cam.zmin) / 5.0);
    if (o.grid) {
        for (double v = std::ceil((cam.target.x - cam.range) / tick_step) * tick_step; v <= cam.target.x + cam.range + 1e-9; v += tick_step) {
            const double n = (v - cam.target.x) / cam.range;
            seg_n(Vec3{ n, -1, -1 }, Vec3{ n, 1, -1 }, o.theme.grid_major, 1.0 * ui);
        }
        for (double v = std::ceil((cam.target.y - cam.range_y) / ytick) * ytick; v <= cam.target.y + cam.range_y + 1e-9; v += ytick) {
            const double n = (v - cam.target.y) / cam.range_y;
            seg_n(Vec3{ -1, n, -1 }, Vec3{ 1, n, -1 }, o.theme.grid_major, 1.0 * ui);
        }
    }
    if (o.box) {
        const double s[2] = { -1.0, 1.0 };
        for (double a : s) {
            for (double b : s) {
                seg_n(Vec3{ -1, a, b }, Vec3{ 1, a, b }, box_col, 1.0 * ui);
                seg_n(Vec3{ a, -1, b }, Vec3{ a, 1, b }, box_col, 1.0 * ui);
                seg_n(Vec3{ a, b, -1 }, Vec3{ a, b, 1 }, box_col, 1.0 * ui);
            }
        }
    }
    if (o.axes) {
        const Vec3 o0 = cam.normalize(Vec3{ 0, 0, 0 });
        auto inside = [](double v) { return v >= -1.0 && v <= 1.0; };
        if (inside(o0.y) && inside(o0.z)) seg_n(Vec3{ -1, o0.y, o0.z }, Vec3{ 1, o0.y, o0.z }, rgba(229, 83, 75, 220), 1.6 * ui);
        if (inside(o0.x) && inside(o0.z)) seg_n(Vec3{ o0.x, -1, o0.z }, Vec3{ o0.x, 1, o0.z }, rgba(63, 166, 90, 220), 1.6 * ui);
        if (inside(o0.x) && inside(o0.y)) seg_n(Vec3{ o0.x, o0.y, -1 }, Vec3{ o0.x, o0.y, 1 }, rgba(79, 143, 230, 220), 1.6 * ui);
    }

    for (const Line3& l : scene.s3.lines) {
        const bool per = l.colors.size() == l.pts.size();
        double phase = 0.0;
        for (std::size_t i = 1; i < l.pts.size(); ++i) {
            Vec3 a = cam.normalize(l.pts[i - 1]);
            Vec3 b = cam.normalize(l.pts[i]);
            if (!finite(a) || !finite(b)) continue;
            const Vec3 a0 = a;
            const Vec3 d0 = b - a;
            if (!clip_box(a, b, 1.0005)) continue;
            auto tpar = [&](Vec3 p) {
                const double l2 = dot(d0, d0);
                return l2 > 0 ? dot(p - a0, d0) / l2 : 0.0;
            };
            Rgba ca = per ? mix(l.colors[i - 1], l.colors[i], tpar(a)) : l.color;
            Rgba cb = per ? mix(l.colors[i - 1], l.colors[i], tpar(b)) : l.color;
            const PV pa = proj_n(a), pb = proj_n(b);
            if (l.dashed) {
                const double len = std::hypot(pb.x - pa.x, pb.y - pa.y);
                const double on = 6.0 * ui, off = 5.0 * ui;
                double s = 0.0;
                if (!(len < 1e7)) continue;
                while (s < len) {
                    const double cyc = std::fmod(phase, on + off);
                    const double left = cyc < on ? on - cyc : on + off - cyc;
                    const double e = std::min(len, s + std::max(left, 1e-3));
                    if (cyc < on && len > 0) {
                        PV p0 = pa, p1 = pa;
                        p0.x = pa.x + (pb.x - pa.x) * s / len;
                        p0.y = pa.y + (pb.y - pa.y) * s / len;
                        p0.d = pa.d + (pb.d - pa.d) * s / len;
                        p1.x = pa.x + (pb.x - pa.x) * e / len;
                        p1.y = pa.y + (pb.y - pa.y) * e / len;
                        p1.d = pa.d + (pb.d - pa.d) * e / len;
                        line3(img, zb, p0, p1, l.width * ui, ca, cb, l.on_top);
                    }
                    phase = std::fmod(phase + (e - s), on + off);
                    s = e;
                }
            } else {
                line3(img, zb, pa, pb, l.width * ui, ca, cb, l.on_top);
            }
        }
    }
    for (const Arrow3& a : scene.s3.arrows) {
        const PV f = proj(a.from), t = proj(a.to);
        if (!f.ok || !t.ok) continue;
        const double dx = t.x - f.x, dy = t.y - f.y;
        const double len = std::hypot(dx, dy);
        if (len < 0.5) continue;
        const double hl = std::min(len * 0.5, 3.5 * a.width * ui + 5.0 * ui);
        const double ux = dx / len, uy = dy / len;
        PV base = t;
        base.x = t.x - ux * hl;
        base.y = t.y - uy * hl;
        base.d = t.d + (f.d - t.d) * (hl / len);
        line3(img, zb, f, base, a.width * ui, a.color, a.color, false);
        PV l = base, r = base;
        l.x += -uy * hl * 0.45;
        l.y += ux * hl * 0.45;
        r.x -= -uy * hl * 0.45;
        r.y -= ux * hl * 0.45;
        PV tip = t;
        tip.d -= 0.02;
        l.d -= 0.02;
        r.d -= 0.02;
        raster_tri(img, zb, tip, l, r, a.color, a.color, a.color, 1.0, true);
    }
    for (const Dot3& d : scene.s3.dots) {
        const PV p = proj(d.at);
        if (!p.ok) continue;
        const double r = d.radius * ui;
        for (int y = static_cast<int>(p.y - r - 1); y <= static_cast<int>(p.y + r + 1); ++y) {
            for (int x = static_cast<int>(p.x - r - 1); x <= static_cast<int>(p.x + r + 1); ++x) {
                if (x < 0 || y < 0 || x >= img.w || y >= img.h) continue;
                const double dist = std::hypot(x + 0.5 - p.x, y + 0.5 - p.y);
                const double cov = std::clamp(r + 0.5 - dist, 0.0, 1.0);
                if (cov <= 0) continue;
                if (p.d - 0.03 > zb.z[static_cast<std::size_t>(y) * zb.w + x]) continue;
                img.blend(x, y, d.color, cov);
            }
        }
    }
    for (const Mesh3& m : scene.s3.meshes) {
        if (m.alpha < 0.999) draw_mesh(m, true);
    }

    if (ss > 1) out.px = downsample_rgba(img, out.w, out.h);
    else out.px = img.px;
    if (!o.labels) return;
    const Camera3 c1 = make_camera(o.view3, scene, out.w, out.h);
    const double u1 = o.ui_scale;
    const double size = 10.5 * u1;
    auto put = [&](Vec3 n, const std::string& text, Rgba col, int align) {
        const double zc = 0.5 * (c1.zmin + c1.zmax);
        const double h = 0.5 * (c1.zmax - c1.zmin);
        double sx, sy, dd;
        if (c1.project(Vec3{ c1.target.x + n.x * c1.range, c1.target.y + n.y * c1.range_y, zc + n.z * h }, sx, sy, dd)) draw_text(out, sx, sy, text, size, col, align);
    };
    auto depth_of = [&](Vec3 n) { return c1.view(n).z; };
    const double ey = depth_of(Vec3{ 0, -1, -1 }) < depth_of(Vec3{ 0, 1, -1 }) ? -1.0 : 1.0;
    const double ex = depth_of(Vec3{ -1, 0, -1 }) < depth_of(Vec3{ 1, 0, -1 }) ? -1.0 : 1.0;
    const Rgba tc = with_alpha(o.theme.text, 0.85);
    for (double v = std::ceil((c1.target.x - c1.range) / tick_step) * tick_step; v <= c1.target.x + c1.range + 1e-9; v += tick_step) put(Vec3{ (v - c1.target.x) / c1.range, ey * 1.12, -1.0 }, tick_text(v, tick_step), tc, 0);
    const double ytick1 = nice_step(2.0 * c1.range_y / 5.0);
    for (double v = std::ceil((c1.target.y - c1.range_y) / ytick1) * ytick1; v <= c1.target.y + c1.range_y + 1e-9; v += ytick1) put(Vec3{ ex * 1.12, (v - c1.target.y) / c1.range_y, -1.0 }, tick_text(v, ytick1), tc, 0);
    Vec3 best{ -1, -1, 0 };
    double best_x = 1e300;
    for (double a : { -1.0, 1.0 }) {
        for (double b : { -1.0, 1.0 }) {
            const Vec3 v = c1.view(Vec3{ a, b, 0 });
            if (v.x < best_x) {
                best_x = v.x;
                best = Vec3{ a, b, 0 };
            }
        }
    }
    const double zc = 0.5 * (c1.zmin + c1.zmax);
    const double zhh = 0.5 * (c1.zmax - c1.zmin);
    for (double v = std::ceil(c1.zmin / ztick) * ztick; v <= c1.zmax + 1e-9; v += ztick) put(Vec3{ best.x * 1.1, best.y * 1.1, (v - zc) / zhh }, tick_text(v, ztick), tc, 1);
    put(Vec3{ 0.0, ey * 1.3, -1.0 }, scene.s3.axis_x.empty() ? std::string("x") : scene.s3.axis_x, o.theme.text, 0);
    put(Vec3{ ex * 1.3, 0.0, -1.0 }, scene.s3.axis_y.empty() ? std::string("y") : scene.s3.axis_y, o.theme.text, 0);
    put(Vec3{ best.x * 1.25, best.y * 1.25, 0.0 }, scene.s3.axis_z.empty() ? std::string("z") : scene.s3.axis_z, o.theme.text, 1);
    for (const Arrow3& a : scene.s3.arrows) {
        if (a.label.empty()) continue;
        double sx, sy, dd;
        if (c1.project(a.to, sx, sy, dd)) draw_text(out, sx + 6 * u1, sy - 4 * u1, a.label, 12.0 * u1, a.color, -1);
    }
    for (const Dot3& d : scene.s3.dots) {
        if (d.label.empty()) continue;
        double sx, sy, dd;
        if (c1.project(d.at, sx, sy, dd)) draw_text(out, sx + 6 * u1, sy - 6 * u1, d.label, 11.0 * u1, o.theme.text, -1);
    }
}

} // namespace

void render_scene(const Scene& scene, const RenderOptions& options, Image& out) {
    out.clear(options.theme.background);
    if (options.space == Space::Solid) {
        render3d(scene, options, out);
        draw_insets(out, scene.s2, options);
    } else {
        render2d(scene, options, out);
    }
}

} // namespace arbor
