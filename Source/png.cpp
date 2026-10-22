#include "arbor/png.hpp"

#include <array>
#include <fstream>

namespace arbor {

namespace {

std::uint32_t crc32(const std::uint8_t* data, std::size_t n, std::uint32_t crc = 0) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data) {
    put32(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32(out, crc32(&out[start], out.size() - start));
}

class BitWriter {
public:
    std::vector<std::uint8_t> bytes;
    void bits(std::uint32_t v, int n) {
        for (int i = 0; i < n; ++i) {
            m_acc |= ((v >> i) & 1u) << m_n;
            if (++m_n == 8) {
                bytes.push_back(static_cast<std::uint8_t>(m_acc));
                m_acc = 0;
                m_n = 0;
            }
        }
    }
    void reversed(std::uint32_t code, int n) {
        std::uint32_t r = 0;
        for (int i = 0; i < n; ++i) r |= ((code >> i) & 1u) << (n - 1 - i);
        bits(r, n);
    }
    void flush() {
        if (m_n) bytes.push_back(static_cast<std::uint8_t>(m_acc));
        m_acc = 0;
        m_n = 0;
    }

private:
    std::uint32_t m_acc = 0;
    int m_n = 0;
};

void lit(BitWriter& w, int v) {
    if (v < 144) w.reversed(0x30 + v, 8);
    else if (v < 256) w.reversed(0x190 + v - 144, 9);
    else if (v < 280) w.reversed(v - 256, 7);
    else w.reversed(0xC0 + v - 280, 8);
}

void length_code(BitWriter& w, int len) {
    static const int base[] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const int extra[] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    int i = 28;
    while (base[i] > len) --i;
    lit(w, 257 + i);
    if (extra[i]) w.bits(static_cast<std::uint32_t>(len - base[i]), extra[i]);
}

void dist_code(BitWriter& w, int d) {
    static const int base[] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    static const int extra[] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    int i = 29;
    while (base[i] > d) --i;
    w.reversed(static_cast<std::uint32_t>(i), 5);
    if (extra[i]) w.bits(static_cast<std::uint32_t>(d - base[i]), extra[i]);
}

std::vector<std::uint8_t> zlib(const std::vector<std::uint8_t>& in) {
    BitWriter w;
    w.bits(0x78, 8);
    w.bits(0x01, 8);
    w.bits(1, 1);
    w.bits(1, 2);
    const int window = 32768;
    const int hash_size = 1 << 15;
    std::vector<int> head(hash_size, -1);
    std::vector<int> prev(in.size(), -1);
    auto hash = [&](std::size_t i) { return ((in[i] << 10) ^ (in[i + 1] << 5) ^ in[i + 2]) & (hash_size - 1); };
    std::size_t i = 0;
    const std::size_t n = in.size();
    while (i < n) {
        int best = 0;
        int best_d = 0;
        if (i + 3 <= n) {
            const int h = hash(i);
            int cand = head[h];
            int tries = 24;
            while (cand >= 0 && static_cast<int>(i) - cand <= window && tries-- > 0) {
                int len = 0;
                while (len < 258 && i + len < n && in[cand + len] == in[i + len]) ++len;
                if (len > best) {
                    best = len;
                    best_d = static_cast<int>(i) - cand;
                    if (len == 258) break;
                }
                cand = prev[cand];
            }
        }
        const std::size_t step = best >= 3 ? static_cast<std::size_t>(best) : 1;
        if (best >= 3) {
            length_code(w, best);
            dist_code(w, best_d);
        } else {
            lit(w, in[i]);
        }
        for (std::size_t k = 0; k < step; ++k, ++i) {
            if (i + 3 <= n) {
                const int h = hash(i);
                prev[i] = head[h];
                head[h] = static_cast<int>(i);
            }
        }
    }
    lit(w, 256);
    w.flush();
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t c : in) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    std::vector<std::uint8_t> out = std::move(w.bytes);
    put32(out, (b << 16) | a);
    return out;
}

} // namespace

std::vector<std::uint8_t> encode_png(const std::uint8_t* rgba, int width, int height) {
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(width * 3 + 1) * height);
    for (int y = 0; y < height; ++y) {
        raw.push_back(y == 0 ? 0 : 2);
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* p = rgba + (static_cast<std::size_t>(y) * width + x) * 4;
            for (int c = 0; c < 3; ++c) {
                const std::uint8_t up = y == 0 ? 0 : rgba[(static_cast<std::size_t>(y - 1) * width + x) * 4 + c];
                raw.push_back(static_cast<std::uint8_t>(p[c] - up));
            }
        }
    }
    std::vector<std::uint8_t> out = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    std::vector<std::uint8_t> ihdr;
    put32(ihdr, static_cast<std::uint32_t>(width));
    put32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.insert(ihdr.end(), { 8, 2, 0, 0, 0 });
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", zlib(raw));
    chunk(out, "IEND", {});
    return out;
}

bool write_png(const std::string& path, const std::uint8_t* rgba, int width, int height) {
    const std::vector<std::uint8_t> data = encode_png(rgba, width, height);
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}

} // namespace arbor
