#include "build_common.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>

namespace arbor {
namespace build {

namespace {

const double k_nan = std::nan("");
const double k_tau = 6.283185307179586;

ExprPtr complex_field(const Item& item, Ctx& c, ItemStatus& st, const std::string& var) {
    ParseResult r = parse_field(item.field("eq"), c.env, { var, "z", "w" });
    if (!r.ok) {
        st.error = r.error;
        return nullptr;
    }
    ExprPtr raw = r.statement.relation == Relation::None ? r.statement.lhs : r.statement.rhs;
    if (r.statement.relation == Relation::Equal && r.statement.lhs->op == Op::Variable && (r.statement.lhs->text == "w" || r.statement.lhs->text == "f")) raw = r.statement.rhs;
    std::string err;
    ExprPtr F = prepare(raw, c.env, { var }, err);
    if (!F) st.error = err;
    return F;
}

Rgba phase_color(double re, double im, bool rings, bool phase, bool grid) {
    if (!std::isfinite(re) || !std::isfinite(im)) return rgba(255, 255, 255);
    const double arg = std::atan2(im, re);
    const double mod = std::hypot(re, im);
    double v = 0.96;
    if (rings && mod > 0.0) {
        const double l = std::log2(mod);
        v = 0.62 + 0.38 * (l - std::floor(l));
    }
    if (phase) {
        const double k = arg / (k_tau / 12.0);
        const double f = std::fabs(k - std::round(k));
        if (f < 0.05) v *= 0.72;
    }
    if (grid) {
        const double fr = std::fabs(re - std::round(re));
        const double fi = std::fabs(im - std::round(im));
        if (fr < 0.03 || fi < 0.03) v *= 0.6;
    }
    return hsv(arg / k_tau, 0.88, v);
}

struct Pole {
    std::complex<double> at;
    int order = 1;
    std::complex<double> residue;
    bool exact_spot = true;
    bool residue_sure = true;
};

std::complex<double> eval_c(Func& f, std::complex<double> z) {
    double x = z.real();
    double y = z.imag();
    double ore = 0.0;
    double oim = 0.0;
    f.eval_complex(1, { &x }, { &y }, &ore, &oim);
    return { ore, oim };
}

std::complex<double> circle_integral(Func& f, std::complex<double> center, double r, int n) {
    std::vector<double> xs(static_cast<std::size_t>(n));
    std::vector<double> ys(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k) {
        const double th = k_tau * (k + 0.5) / n;
        xs[k] = center.real() + r * std::cos(th);
        ys[k] = center.imag() + r * std::sin(th);
    }
    std::vector<double> ore(xs.size());
    std::vector<double> oim(xs.size());
    f.eval_complex(xs.size(), { xs.data() }, { ys.data() }, ore.data(), oim.data());
    std::complex<double> sum = 0.0;
    for (int k = 0; k < n; ++k) {
        const double th = k_tau * (k + 0.5) / n;
        const std::complex<double> e(std::cos(th), std::sin(th));
        sum += std::complex<double>(ore[k], oim[k]) * e;
    }
    return sum * (r / n);
}

double mean_modulus(Func& f, std::complex<double> center, double r, int n) {
    std::vector<double> xs(static_cast<std::size_t>(n));
    std::vector<double> ys(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k) {
        const double th = k_tau * (k + 0.25) / n;
        xs[k] = center.real() + r * std::cos(th);
        ys[k] = center.imag() + r * std::sin(th);
    }
    std::vector<double> ore(xs.size());
    std::vector<double> oim(xs.size());
    f.eval_complex(xs.size(), { xs.data() }, { ys.data() }, ore.data(), oim.data());
    double sum = 0.0;
    for (int k = 0; k < n; ++k) {
        const double m = std::hypot(ore[k], oim[k]);
        if (!std::isfinite(m)) return std::numeric_limits<double>::infinity();
        sum += m;
    }
    return sum / n;
}

int winding_on_circle(Func& f, std::complex<double> center, double r, int n) {
    std::vector<double> xs(static_cast<std::size_t>(n));
    std::vector<double> ys(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k) {
        const double th = k_tau * k / n;
        xs[k] = center.real() + r * std::cos(th);
        ys[k] = center.imag() + r * std::sin(th);
    }
    std::vector<double> ore(xs.size());
    std::vector<double> oim(xs.size());
    f.eval_complex(xs.size(), { xs.data() }, { ys.data() }, ore.data(), oim.data());
    double total = 0.0;
    for (int k = 0; k < n; ++k) {
        const int k1 = (k + 1) % n;
        if (!std::isfinite(ore[k]) || !std::isfinite(oim[k]) || !std::isfinite(ore[k1]) || !std::isfinite(oim[k1])) return 0;
        double da = std::atan2(oim[k1], ore[k1]) - std::atan2(oim[k], ore[k]);
        while (da > k_tau / 2) da -= k_tau;
        while (da < -k_tau / 2) da += k_tau;
        total += da;
    }
    return static_cast<int>(std::lround(total / k_tau));
}

struct SampleGrid {
    int w = 0;
    int h = 0;
    double xmin = 0.0;
    double xmax = 0.0;
    double ymin = 0.0;
    double ymax = 0.0;
    const double* re = nullptr;
    const double* im = nullptr;
};

std::vector<Pole> find_poles(Func& f, Func* df, double xmin, double xmax, double ymin, double ymax, std::size_t limit, const SampleGrid* fine) {
    std::vector<Pole> poles;
    const double w = xmax - xmin;
    const double h = ymax - ymin;
    if (!(w > 0.0) || !(h > 0.0)) return poles;
    const int nx = 72;
    const int ny = std::clamp(static_cast<int>(std::lround(nx * h / w)), 16, 160);
    const int m = 6;
    const double ox = xmin + w * 0.0013719;
    const double oy = ymin + h * 0.0021347;
    const double cw = w / nx;
    const double ch = h / ny;
    const double cell = std::max(cw, ch);
    const int hx = nx * m + 1;
    const int vy = ny * m + 1;
    std::vector<double> hre(static_cast<std::size_t>(hx) * (ny + 1));
    std::vector<double> him(hre.size());
    std::vector<double> vre(static_cast<std::size_t>(vy) * (nx + 1));
    std::vector<double> vim(vre.size());
    {
        std::vector<double> xs(hre.size());
        std::vector<double> ys(hre.size());
        for (int j = 0; j <= ny; ++j) {
            for (int k = 0; k < hx; ++k) {
                xs[static_cast<std::size_t>(j) * hx + k] = ox + cw * k / m;
                ys[static_cast<std::size_t>(j) * hx + k] = oy + ch * j;
            }
        }
        f.eval_complex(xs.size(), { xs.data() }, { ys.data() }, hre.data(), him.data());
        std::vector<double> xs2(vre.size());
        std::vector<double> ys2(vre.size());
        for (int i = 0; i <= nx; ++i) {
            for (int k = 0; k < vy; ++k) {
                xs2[static_cast<std::size_t>(i) * vy + k] = ox + cw * i;
                ys2[static_cast<std::size_t>(i) * vy + k] = oy + ch * k / m;
            }
        }
        f.eval_complex(xs2.size(), { xs2.data() }, { ys2.data() }, vre.data(), vim.data());
    }
    auto arg_step = [](double r0, double i0, double r1, double i1, bool& bad) {
        if (!std::isfinite(r0) || !std::isfinite(i0) || !std::isfinite(r1) || !std::isfinite(i1)) {
            bad = true;
            return 0.0;
        }
        double da = std::atan2(i1, r1) - std::atan2(i0, r0);
        while (da > k_tau / 2) da -= k_tau;
        while (da < -k_tau / 2) da += k_tau;
        return da;
    };

    struct Seed {
        std::complex<double> z;
        int order = 1;
        bool from_winding = false;
    };
    std::vector<Seed> seeds;
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            bool bad = false;
            double total = 0.0;
            for (int k = 0; k < m; ++k) {
                const std::size_t a = static_cast<std::size_t>(j) * hx + i * m + k;
                total += arg_step(hre[a], him[a], hre[a + 1], him[a + 1], bad);
            }
            for (int k = 0; k < m; ++k) {
                const std::size_t a = static_cast<std::size_t>(i + 1) * vy + j * m + k;
                total += arg_step(vre[a], vim[a], vre[a + 1], vim[a + 1], bad);
            }
            for (int k = m; k > 0; --k) {
                const std::size_t a = static_cast<std::size_t>(j + 1) * hx + i * m + k;
                total += arg_step(hre[a], him[a], hre[a - 1], him[a - 1], bad);
            }
            for (int k = m; k > 0; --k) {
                const std::size_t a = static_cast<std::size_t>(i) * vy + j * m + k;
                total += arg_step(vre[a], vim[a], vre[a - 1], vim[a - 1], bad);
            }
            if (bad) continue;
            const int wind = static_cast<int>(std::lround(total / k_tau));
            if (wind < 0) seeds.push_back(Seed{ std::complex<double>(ox + cw * (i + 0.5), oy + ch * (j + 0.5)), -wind, true });
        }
    }
    auto node_mod = [&](int i, int j) {
        const std::size_t a = static_cast<std::size_t>(j) * hx + i * m;
        return std::hypot(hre[a], him[a]);
    };
    for (int j = 1; j < ny; ++j) {
        for (int i = 1; i < nx; ++i) {
            const double v = node_mod(i, j);
            if (!std::isfinite(v)) {
                seeds.push_back(Seed{ std::complex<double>(ox + cw * i, oy + ch * j), 1, false });
                continue;
            }
            bool peak = v > 0.0;
            for (const auto& o : { std::pair<int, int>{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) {
                const double u = node_mod(i + o.first, j + o.second);
                if (!(std::isfinite(u) && v > 1.5 * u)) peak = false;
            }
            if (peak) seeds.push_back(Seed{ std::complex<double>(ox + cw * i, oy + ch * j), 1, false });
        }
    }
    {
        std::vector<std::pair<double, int>> mods;
        for (int j = 1; j < ny; ++j) {
            for (int i = 1; i < nx; ++i) {
                const double v = node_mod(i, j);
                if (std::isfinite(v)) mods.push_back({ v, j * (nx + 1) + i });
            }
        }
        if (!mods.empty()) {
            std::vector<double> values;
            values.reserve(mods.size());
            for (const auto& mv : mods) values.push_back(mv.first);
            std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2), values.end());
            const double median = values[values.size() / 2];
            std::sort(mods.begin(), mods.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            const std::size_t take = std::min<std::size_t>(mods.size(), std::max<std::size_t>(limit * 4, mods.size() / 50));
            for (std::size_t k = 0; k < take; ++k) {
                if (!(mods[k].first > 4.0 * median)) break;
                const int i = mods[k].second % (nx + 1);
                const int j = mods[k].second / (nx + 1);
                seeds.push_back(Seed{ std::complex<double>(ox + cw * i, oy + ch * j), 1, false });
            }
        }
    }
    if (fine && fine->w > 2 && fine->h > 2) {
        std::vector<std::pair<double, std::size_t>> peaks;
        auto mod = [&](int i, int j) {
            const std::size_t a = static_cast<std::size_t>(j) * fine->w + i;
            return std::hypot(fine->re[a], fine->im[a]);
        };
        std::vector<double> sample;
        for (int j = 0; j < fine->h; j += 7) {
            for (int i = 0; i < fine->w; i += 7) {
                const double v = mod(i, j);
                if (std::isfinite(v)) sample.push_back(v);
            }
        }
        double median = 0.0;
        if (!sample.empty()) {
            std::nth_element(sample.begin(), sample.begin() + static_cast<std::ptrdiff_t>(sample.size() / 2), sample.end());
            median = sample[sample.size() / 2];
        }
        for (int j = 1; j + 1 < fine->h; ++j) {
            for (int i = 1; i + 1 < fine->w; ++i) {
                const double v = mod(i, j);
                if (!(v > 8.0 * median) && std::isfinite(v)) continue;
                bool peak = true;
                for (int dj = -1; dj <= 1 && peak; ++dj) {
                    for (int di = -1; di <= 1; ++di) {
                        if ((di || dj) && !(v >= mod(i + di, j + dj))) {
                            peak = false;
                            break;
                        }
                    }
                }
                if (peak) peaks.push_back({ std::isfinite(v) ? v : std::numeric_limits<double>::max(), static_cast<std::size_t>(j) * fine->w + i });
            }
        }
        std::sort(peaks.begin(), peaks.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        if (peaks.size() > limit * 6) peaks.resize(limit * 6);
        for (const auto& pk : peaks) {
            const int i = static_cast<int>(pk.second % fine->w);
            const int j = static_cast<int>(pk.second / fine->w);
            const double x = fine->xmin + (fine->xmax - fine->xmin) * (i + 0.5) / fine->w;
            const double y = fine->ymax - (fine->ymax - fine->ymin) * (j + 0.5) / fine->h;
            seeds.insert(seeds.begin(), Seed{ std::complex<double>(x, y), 1, false });
        }
    }
    if (seeds.size() > limit * 16) seeds.resize(limit * 16);

    const double hh = 1e-6 * cell;
    for (const Seed& seed : seeds) {
        Pole p;
        p.order = seed.order;
        std::complex<double> z = seed.z;
        bool converged = false;
        for (int it = 0; it < 100; ++it) {
            const std::complex<double> fz = eval_c(f, z);
            if (!std::isfinite(fz.real()) || !std::isfinite(fz.imag())) {
                converged = true;
                break;
            }
            std::complex<double> step;
            bool exact_derivative = false;
            if (df) {
                const std::complex<double> dfz = eval_c(*df, z);
                if (std::isfinite(dfz.real()) && std::isfinite(dfz.imag()) && std::abs(dfz) > 0.0) {
                    step = static_cast<double>(p.order) * fz / dfz;
                    exact_derivative = true;
                }
            }
            if (!exact_derivative) {
                const std::complex<double> g0 = 1.0 / fz;
                const std::complex<double> gp = (1.0 / eval_c(f, z + hh) - 1.0 / eval_c(f, z - hh)) / (2.0 * hh);
                if (!std::isfinite(gp.real()) || !std::isfinite(gp.imag()) || std::abs(gp) == 0.0) break;
                step = -static_cast<double>(p.order) * g0 / gp;
            }
            if (!std::isfinite(step.real()) || !std::isfinite(step.imag())) break;
            z += step;
            if (std::abs(z - seed.z) > 3.0 * cell) break;
            if (std::abs(step) < (exact_derivative ? 1e-13 : 1e-10) * std::max(1.0, std::abs(z))) {
                converged = true;
                break;
            }
        }
        if (!converged) {
            if (!seed.from_winding) continue;
            z = seed.z;
            p.exact_spot = false;
            if (winding_on_circle(f, z, 0.75 * cell, 256) >= 0) continue;
        }
        if (z.real() < xmin || z.real() > xmax || z.imag() < ymin || z.imag() > ymax) continue;
        const double snap = 1e-9 * std::max(1.0, std::abs(z));
        if (std::fabs(z.real() - std::round(z.real())) < snap) z.real(std::round(z.real()));
        if (std::fabs(z.imag() - std::round(z.imag())) < snap) z.imag(std::round(z.imag()));
        if (std::fabs(z.real()) < 1e-12 * std::max(1.0, std::abs(z))) z.real(0.0);
        if (std::fabs(z.imag()) < 1e-12 * std::max(1.0, std::abs(z))) z.imag(0.0);
        bool dup = false;
        for (Pole& q : poles) {
            const double near_enough = (q.exact_spot && p.exact_spot) ? 1e-7 * std::max(1.0, std::abs(z)) : 0.5 * std::min(cw, ch);
            if (std::abs(q.at - z) < near_enough) {
                dup = true;
                if (!q.exact_spot && p.exact_spot) {
                    q.at = z;
                    q.exact_spot = true;
                }
                break;
            }
        }
        if (!dup) {
            p.at = z;
            poles.push_back(p);
        }
    }

    for (std::size_t a = 0; a < poles.size(); ++a) {
        double nearest = 0.25 * std::min(w, h);
        for (std::size_t b = 0; b < poles.size(); ++b) {
            if (a != b) nearest = std::min(nearest, std::abs(poles[a].at - poles[b].at));
        }
        const double r = std::max(0.35 * nearest, 1e-9 * std::max(1.0, std::abs(poles[a].at)));
        const double tiny = poles[a].exact_spot ? std::max(r / 256.0, 1e-7 * std::max(1.0, std::abs(poles[a].at))) : r * 0.5;
        const int order = winding_on_circle(f, poles[a].at, tiny, 256);
        if (order >= 0 || mean_modulus(f, poles[a].at, tiny * 0.5, 64) < 1.5 * mean_modulus(f, poles[a].at, tiny, 64)) {
            poles[a].order = 0;
            continue;
        }
        poles[a].order = -order;
        const std::complex<double> big = circle_integral(f, poles[a].at, r, 256);
        const std::complex<double> small = circle_integral(f, poles[a].at, r * 0.5, 256);
        poles[a].residue = small;
        const double scale = std::max({ std::abs(big), std::abs(small), 1e-300 });
        poles[a].residue_sure = poles[a].exact_spot && std::abs(big - small) <= 1e-7 * scale + 1e-12;
        if (std::fabs(poles[a].residue.real() - std::round(poles[a].residue.real())) < 1e-10 * std::max(1.0, scale)) poles[a].residue.real(std::round(poles[a].residue.real()));
        if (std::fabs(poles[a].residue.imag() - std::round(poles[a].residue.imag())) < 1e-10 * std::max(1.0, scale)) poles[a].residue.imag(std::round(poles[a].residue.imag()));
        if (std::fabs(poles[a].residue.real()) < 1e-11 * std::max(1.0, scale)) poles[a].residue.real(0.0);
        if (std::fabs(poles[a].residue.imag()) < 1e-11 * std::max(1.0, scale)) poles[a].residue.imag(0.0);
    }
    poles.erase(std::remove_if(poles.begin(), poles.end(), [](const Pole& q) { return q.order <= 0; }), poles.end());
    std::sort(poles.begin(), poles.end(), [](const Pole& a, const Pole& b) { return std::abs(a.at) < std::abs(b.at); });
    if (poles.size() > limit) poles.resize(limit);
    return poles;
}

std::string complex_text(std::complex<double> v, int digits) {
    const double re = v.real();
    const double im = v.imag();
    if (!std::isfinite(re) || !std::isfinite(im)) return "undefined";
    if (im == 0.0) return format_double(re, digits);
    const std::string imag = (std::fabs(im) == 1.0 ? std::string() : format_double(std::fabs(im), digits)) + "i";
    if (re == 0.0) return (im < 0 ? "-" : "") + imag;
    return format_double(re, digits) + (im < 0 ? " - " : " + ") + imag;
}

void add_pole_readouts(Func& fn, const ExprPtr& F, Ctx& c, ItemStatus& st, double xmin, double xmax, double ymin, double ymax, bool dots, const SampleGrid* fine = nullptr) {
    Func dfn;
    Func* dptr = nullptr;
    if (ExprPtr dF = d(F, "z")) {
        dfn = Func(dF, { "z" }, true, NumberType::Hardware);
        if (dfn.ok()) dptr = &dfn;
    }
    const std::size_t limit = 12;
    const std::vector<Pole> poles = find_poles(fn, dptr, xmin, xmax, ymin, ymax, limit + 1, fine);
    if (poles.empty()) {
        st.readouts.push_back({ "poles in view", "none" });
        return;
    }
    const double ppx = (xmax - xmin) / std::max(1, c.width);
    const double ppy = (ymax - ymin) / std::max(1, c.height);
    std::vector<std::array<double, 4>> boxes;
    for (std::size_t k = 0; k < poles.size() && k < limit; ++k) {
        const Pole& p = poles[k];
        const std::string where = (p.exact_spot ? "" : "\xE2\x89\x88") + complex_text(p.at, 8);
        const std::string label = "pole " + where + (p.order > 1 ? " (order " + std::to_string(p.order) + ")" : "");
        st.readouts.push_back({ label, "Res = " + std::string(p.residue_sure ? "" : "\xE2\x89\x88") + complex_text(p.residue, 10) });
        if (dots) {
            std::string text = "Res " + complex_text(p.residue, 4);
            const std::array<double, 4> box = { p.at.real(), p.at.real() + (12.0 + 7.0 * static_cast<double>(text.size())) * ppx, p.at.imag() - 10.0 * ppy, p.at.imag() + 10.0 * ppy };
            for (const auto& b : boxes) {
                if (box[0] < b[1] && b[0] < box[1] && box[2] < b[3] && b[2] < box[3]) {
                    text.clear();
                    break;
                }
            }
            if (!text.empty()) boxes.push_back(box);
            c.scene.s2.dots.push_back(Dot2{ Vec2{ p.at.real(), p.at.imag() }, rgba(255, 255, 255), 5.0f, true, text });
        }
    }
    if (poles.size() > limit) st.readouts.push_back({ "more poles", "zoom in to see the rest" });
}

} // namespace

void build_domain(const Item& item, Ctx& c, ItemStatus& st) {
    ExprPtr F = complex_field(item, c, st, "z");
    if (!F) return;
    st.mathtext = "f(z) = " + to_mathtext(F);
    Func fn(F, { "z" }, true, NumberType::Hardware);
    if (!fn.ok()) {
        st.error = fn.error();
        return;
    }
    const int step = c.env.quality <= 1 ? 2 : 1;
    Raster2 ras;
    ras.xmin = c.box.xmin;
    ras.xmax = c.box.xmax;
    ras.ymin = c.box.ymin;
    ras.ymax = c.box.ymax;
    ras.w = std::max(1, c.width / step);
    ras.h = std::max(1, c.height / step);
    const std::size_t n = static_cast<std::size_t>(ras.w) * ras.h;
    std::vector<double> re(n);
    std::vector<double> im(n);
    for (int j = 0; j < ras.h; ++j) {
        for (int i = 0; i < ras.w; ++i) {
            re[static_cast<std::size_t>(j) * ras.w + i] = ras.xmin + (ras.xmax - ras.xmin) * (i + 0.5) / ras.w;
            im[static_cast<std::size_t>(j) * ras.w + i] = ras.ymax - (ras.ymax - ras.ymin) * (j + 0.5) / ras.h;
        }
    }
    std::vector<double> ore(n);
    std::vector<double> oim(n);
    fn.eval_complex(n, { re.data() }, { im.data() }, ore.data(), oim.data());
    ras.px.resize(n);
    const bool rings = item.option("modulus");
    const bool phase = item.option("phase");
    const bool grid = item.option("grid");
    for (std::size_t i = 0; i < n; ++i) ras.px[i] = phase_color(ore[i], oim[i], rings, phase, grid);
    c.scene.s2.rasters.push_back(std::move(ras));
    st.legend_map = "phase";
    if (ExprPtr fp = d(F, "z")) st.readouts.push_back(formula_readout("f\xE2\x80\xB2(z)", fp));
    if (item.option("residues")) {
        SampleGrid grid;
        grid.w = std::max(1, c.width / step);
        grid.h = std::max(1, c.height / step);
        grid.xmin = c.box.xmin;
        grid.xmax = c.box.xmax;
        grid.ymin = c.box.ymin;
        grid.ymax = c.box.ymax;
        grid.re = ore.data();
        grid.im = oim.data();
        add_pole_readouts(fn, F, c, st, c.box.xmin, c.box.xmax, c.box.ymin, c.box.ymax, true, &grid);
    }
    Func probe(F, { "z" }, true, NumberType::Hardware);
    c.scene.probes.push_back(Probe{ item.id, [probe](double x, double y) mutable {
        double ore1 = 0.0;
        double oim1 = 0.0;
        probe.eval_complex(1, { &x }, { &y }, &ore1, &oim1);
        const double m = std::hypot(ore1, oim1);
        return "f(z) = " + format_double(ore1, 5) + (oim1 < 0 ? " - " : " + ") + format_double(std::fabs(oim1), 5) + "i   |f| = " + format_double(m, 5) + "   arg = " + format_double(std::atan2(oim1, ore1), 4);
    } });
}

void build_conformal(const Item& item, Ctx& c, ItemStatus& st) {
    ExprPtr F = complex_field(item, c, st, "z");
    if (!F) return;
    st.mathtext = "w = " + to_mathtext(F);
    Func fn(F, { "z" }, true, c.env.geo_type);
    if (!fn.ok()) {
        st.error = fn.error();
        return;
    }
    Range re;
    Range im;
    std::string err;
    if (!parse_range(item, "remin", "remax", c.env, re, err) || !parse_range(item, "immin", "immax", c.env, im, err)) {
        st.error = err;
        return;
    }
    double lines = 16.0;
    parse_number(item.field("lines"), c.env, lines, err);
    const int n = static_cast<int>(std::clamp(lines, 2.0, 80.0));
    const Rgba a = item_color(item);
    Rgba b;
    parse_color(palette()[(std::find(palette().begin(), palette().end(), item.color) - palette().begin() + 1) % palette().size()], b);
    const int samples = 240;
    auto family = [&](bool vertical, Rgba color) {
        Stroke2 s;
        s.color = color;
        s.width = 1.6f;
        Stroke2 src;
        src.color = with_alpha(c.env.theme.text, 0.18);
        src.width = 1.0f;
        for (int k = 0; k <= n; ++k) {
            const double fixed = vertical ? re.a + (re.b - re.a) * k / n : im.a + (im.b - im.a) * k / n;
            std::vector<double> rs(samples + 1);
            std::vector<double> is(samples + 1);
            for (int i = 0; i <= samples; ++i) {
                const double moving = vertical ? im.a + (im.b - im.a) * i / samples : re.a + (re.b - re.a) * i / samples;
                rs[static_cast<std::size_t>(i)] = vertical ? fixed : moving;
                is[static_cast<std::size_t>(i)] = vertical ? moving : fixed;
            }
            std::vector<double> ore(rs.size());
            std::vector<double> oim(rs.size());
            fn.eval_complex(rs.size(), { rs.data() }, { is.data() }, ore.data(), oim.data());
            for (std::size_t i = 0; i < rs.size(); ++i) {
                s.pts.push_back(Vec2{ ore[i], oim[i] });
                src.pts.push_back(Vec2{ rs[i], is[i] });
            }
            s.pts.push_back(Vec2{ k_nan, k_nan });
            src.pts.push_back(Vec2{ k_nan, k_nan });
        }
        if (item.option("source")) c.scene.s2.strokes.push_back(std::move(src));
        c.scene.s2.strokes.push_back(std::move(s));
    };
    family(true, a);
    family(false, b);
    if (ExprPtr fp = d(F, "z")) st.readouts.push_back(formula_readout("f\xE2\x80\xB2(z)", fp));
    st.note = "Re z = const lines in this item's color, Im z = const lines in the next color. Where f\xE2\x80\xB2 \xE2\x89\xA0 0 they still cross at right angles.";
}

void build_complex_surface(const Item& item, Ctx& c, ItemStatus& st) {
    ExprPtr F = complex_field(item, c, st, "z");
    if (!F) return;
    std::string mode = item.field("height");
    for (char& ch : mode) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    st.mathtext = (mode == "real" ? "Re f(z), f = " : mode == "imag" ? "Im f(z), f = " : mode == "log" ? "log|f(z)|, f = " : "|f(z)|, f = ") + to_mathtext(F);
    Func fn(F, { "z" }, true, c.env.geo_type);
    if (!fn.ok()) {
        st.error = fn.error();
        return;
    }
    const int n = c.env.quality <= 1 ? 50 : c.env.quality >= 3 ? 150 : 96;
    Mesh3 mesh;
    std::vector<Vec3> grid;
    std::vector<double> args;
    surface_mesh(c, [&](const std::vector<double>& us, const std::vector<double>& vs, std::vector<Vec3>& out) {
        std::vector<double> ore(us.size());
        std::vector<double> oim(us.size());
        fn.eval_complex(us.size(), { us.data() }, { vs.data() }, ore.data(), oim.data());
        out.resize(us.size());
        args.resize(us.size());
        for (std::size_t i = 0; i < us.size(); ++i) {
            double h = std::hypot(ore[i], oim[i]);
            if (mode == "real") h = ore[i];
            else if (mode == "imag") h = oim[i];
            else if (mode == "log") h = std::log(std::hypot(ore[i], oim[i]));
            if (std::isinf(h)) h = h > 0 ? 1e6 : -1e6;
            out[i] = Vec3{ us[i], vs[i], h };
            args[i] = std::atan2(oim[i], ore[i]);
        }
    }, c.view3.target.x - c.range, c.view3.target.x + c.range, c.view3.target.y - c.range_y, c.view3.target.y + c.range_y, n, n, mesh, grid);
    mesh.col.resize(mesh.pos.size());
    for (std::size_t i = 0; i < mesh.pos.size(); ++i) mesh.col[i] = std::isfinite(args[i]) ? hsv(args[i] / k_tau, 0.8, 0.95) : rgba(200, 200, 200);
    for (Vec3& p : mesh.pos) {
        if (finite(p)) p.z = std::clamp(p.z, c.zmin - (c.zmax - c.zmin), c.zmax + (c.zmax - c.zmin));
    }
    c.scene.s3.meshes.push_back(std::move(mesh));
    if (item.option("mesh")) mesh_lines(c, grid, n, n, std::max(1, n / 16), with_alpha(rgba(0, 0, 0), 0.3));
    st.legend_map = "phase";
    if (item.option("residues")) {
        Func hw(F, { "z" }, true, NumberType::Hardware);
        if (hw.ok()) add_pole_readouts(hw, F, c, st, c.view3.target.x - c.range, c.view3.target.x + c.range, c.view3.target.y - c.range_y, c.view3.target.y + c.range_y, false);
    }
}

void build_complex_curve(const Item& item, Ctx& c, ItemStatus& st) {
    ExprPtr F = complex_field(item, c, st, "t");
    if (!F) return;
    st.mathtext = "f(t) = " + to_mathtext(F);
    Range range;
    std::string err;
    if (!parse_range(item, "min", "max", c.env, range, err)) {
        st.error = err;
        return;
    }
    Func fn(F, { "t" }, true, c.env.geo_type);
    if (!fn.ok()) {
        st.error = fn.error();
        return;
    }
    std::string axis = item.field("axis");
    for (char& ch : axis) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const bool up = axis.rfind("up", 0) == 0 || axis.rfind("vert", 0) == 0 || axis == "z";
    if (!axis.empty() && !up && axis.rfind("across", 0) != 0 && axis.rfind("left", 0) != 0 && axis.rfind("horiz", 0) != 0 && axis != "x") {
        st.error = "t runs: write across (left-right) or up (up-down), not '" + item.field("axis") + "'";
        return;
    }
    auto place = [up](double t, double re, double im) { return up ? Vec3{ re, im, t } : Vec3{ t, re, im }; };
    auto eval_at = [&](const std::vector<double>& t, std::vector<double>& ore, std::vector<double>& oim) {
        ore.assign(t.size(), 0.0);
        oim.assign(t.size(), 0.0);
        std::vector<double> zeros(t.size(), 0.0);
        fn.eval_complex(t.size(), { t.data() }, { zeros.data() }, ore.data(), oim.data());
    };
    CurveSampler3 s;
    s.scale = 2.0 * c.range;
    s.initial = 600;
    s.eval = [&](const std::vector<double>& t, std::vector<Vec3>& out) {
        std::vector<double> ore;
        std::vector<double> oim;
        eval_at(t, ore, oim);
        out.resize(t.size());
        for (std::size_t i = 0; i < t.size(); ++i) out[i] = place(t[i], ore[i], oim[i]);
    };
    std::vector<double> ts;
    std::vector<Vec3> pts;
    sample_curve3(s, range.a, range.b, ts, pts);
    const Rgba col = item_color(item);
    Line3 line;
    line.pts = pts;
    line.color = col;
    line.width = 3.0f;
    for (const Vec3& p : pts) {
        const double re = up ? p.x : p.y;
        const double im = up ? p.y : p.z;
        line.colors.push_back(finite(p) ? hsv(std::atan2(im, re) / k_tau, 0.85, 0.95) : col);
    }
    c.scene.s3.axis_x = up ? "Re" : "t";
    c.scene.s3.axis_y = up ? "Im" : "Re";
    c.scene.s3.axis_z = up ? "t" : "Im";
    if (item.option("shadows")) {
        Line3 re;
        re.color = with_alpha(c.env.theme.text, 0.45);
        re.width = 1.5f;
        re.dashed = true;
        Line3 im = re;
        for (const Vec3& p : pts) {
            if (!finite(p)) {
                re.pts.push_back(p);
                im.pts.push_back(p);
            } else if (up) {
                re.pts.push_back(Vec3{ p.x, c.view3.target.y + c.range_y, p.z });
                im.pts.push_back(Vec3{ c.view3.target.x - c.range, p.y, p.z });
            } else {
                re.pts.push_back(Vec3{ p.x, p.y, c.zmin });
                im.pts.push_back(Vec3{ p.x, c.view3.target.y + c.range_y, p.z });
            }
        }
        c.scene.s3.lines.push_back(std::move(re));
        c.scene.s3.lines.push_back(std::move(im));
    }
    st.note = up ? "t runs up the z axis; across it, x is Re f(t) and y is Im f(t)." : "t runs along the x axis; across it, y is Re f(t) and z is Im f(t).";
    if (item.option("shadows")) st.note += up ? " Dashed: Re f against t on the back wall, Im f against t on the side wall." : " Dashed: Re f on the floor, Im f on the back wall.";
    c.scene.s3.lines.push_back(std::move(line));

    double t0 = 0.5 * (range.a + range.b);
    {
        std::string e2;
        double v = 0.0;
        if (!item.field("at").empty() && parse_number(item.field("at"), c.env, v, e2) && std::isfinite(v)) {
            t0 = v;
        }
    }
    offer_param(st, "t\xE2\x82\x80", range.a, range.b, t0);
    std::vector<double> r0;
    std::vector<double> i0;
    eval_at({ t0 }, r0, i0);
    const bool ok0 = std::isfinite(r0[0]) && std::isfinite(i0[0]);

    if (item.option("plane") && ok0) {
        double extent = 0.0;
        for (const Vec3& p : pts) {
            if (!finite(p)) continue;
            extent = std::max(extent, std::max(std::fabs(up ? p.x : p.y), std::fabs(up ? p.y : p.z)));
        }
        extent = std::clamp(extent * 1.15, 0.5, c.range);
        Mesh3 plane;
        plane.alpha = 0.22;
        plane.lit = false;
        const Rgba pc = with_alpha(c.env.theme.text, 0.35);
        const double corners[4][2] = { { -extent, -extent }, { extent, -extent }, { extent, extent }, { -extent, extent } };
        for (const auto& k : corners) {
            plane.pos.push_back(place(t0, k[0], k[1]));
            plane.nrm.push_back(up ? Vec3{ 0, 0, 1 } : Vec3{ 1, 0, 0 });
            plane.col.push_back(pc);
        }
        plane.tri = { 0, 1, 2, 0, 2, 3 };
        c.scene.s3.meshes.push_back(std::move(plane));
        Line3 frame;
        frame.color = with_alpha(c.env.theme.text, 0.55);
        frame.width = 1.2f;
        for (int k = 0; k <= 4; ++k) frame.pts.push_back(place(t0, corners[k % 4][0], corners[k % 4][1]));
        c.scene.s3.lines.push_back(frame);
        Line3 reaxis;
        reaxis.color = with_alpha(c.env.theme.text, 0.7);
        reaxis.width = 1.2f;
        reaxis.pts = { place(t0, -extent, 0.0), place(t0, extent, 0.0) };
        Line3 imaxis = reaxis;
        imaxis.pts = { place(t0, 0.0, -extent), place(t0, 0.0, extent) };
        c.scene.s3.lines.push_back(std::move(reaxis));
        c.scene.s3.lines.push_back(std::move(imaxis));
        c.scene.s3.arrows.push_back(Arrow3{ place(t0, 0.0, 0.0), place(t0, r0[0], i0[0]), col, 2.2f, "" });
        c.scene.s3.dots.push_back(Dot3{ place(t0, r0[0], i0[0]), col, 6.0f, "f(t\xE2\x82\x80)" });
        c.scene.s3.dots.push_back(Dot3{ place(t0, extent, 0.0), with_alpha(c.env.theme.text, 0.0), 1.0f, "Re" });
        c.scene.s3.dots.push_back(Dot3{ place(t0, 0.0, extent), with_alpha(c.env.theme.text, 0.0), 1.0f, "Im" });
    }

    if (item.option("frame")) {
        const double h = 1e-4 * std::max(1.0, std::fabs(range.b - range.a));
        std::vector<Vec3> q;
        s.eval({ t0 - h, t0, t0 + h }, q);
        const Vec3 vel = (q[2] - q[0]) * (0.5 / h);
        const Vec3 acc = (q[2] - q[1] * 2.0 + q[0]) * (1.0 / (h * h));
        const Vec3 T = normalized(vel);
        const Vec3 B = normalized(cross(vel, acc));
        const Vec3 N = cross(B, T);
        const double L = c.range * 0.35;
        if (finite(T) && finite(B)) {
            c.scene.s3.arrows.push_back(Arrow3{ q[1], q[1] + T * L, rgba(229, 83, 75), 2.4f, "T" });
            c.scene.s3.arrows.push_back(Arrow3{ q[1], q[1] + N * L, rgba(63, 166, 90), 2.4f, "N" });
            c.scene.s3.arrows.push_back(Arrow3{ q[1], q[1] + B * L, rgba(79, 143, 230), 2.4f, "B" });
            if (!item.option("plane")) c.scene.s3.dots.push_back(Dot3{ q[1], col, 6.0f, "" });
        }
    }

    const std::string at = format_double(t0, 4);
    st.readouts.push_back({ "f(" + at + ")", complex_value_text(F, { "t" }, { t0 }, { 0.0 }, c.env) });
    if (ok0) {
        st.readouts.push_back({ "|f(" + at + ")|", format_double(std::hypot(r0[0], i0[0]), 10) });
        st.readouts.push_back({ "arg f(" + at + ")", format_double(std::atan2(i0[0], r0[0]), 10) });
    }
    ExprPtr dF = derivative(F, "t");
    if (dF) {
        dF = simplify(dF);
        st.readouts.push_back({ "f\xE2\x80\xB2(" + at + ")", complex_value_text(dF, { "t" }, { t0 }, { 0.0 }, c.env) });
    }
}

void build_riemann(const Item& item, Ctx& c, ItemStatus& st) {
    ExprPtr G = complex_field(item, c, st, "w");
    if (!G) return;
    std::string mode = item.field("height");
    for (char& ch : mode) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const bool imag = mode == "imag" || mode == "im";
    st.mathtext = "z = " + to_mathtext(G) + ", height " + (imag ? "Im w" : "Re w");
    double rmax = 1.5;
    std::string err;
    parse_number(item.field("rmax"), c.env, rmax, err);
    rmax = std::max(0.05, rmax);
    Func fn(G, { "w" }, true, c.env.geo_type);
    if (!fn.ok()) {
        st.error = fn.error();
        return;
    }
    const int nr = c.env.quality <= 1 ? 30 : c.env.quality >= 3 ? 90 : 56;
    const int nt = nr * 3;
    Mesh3 mesh;
    std::vector<Vec3> grid;
    std::vector<double> args;
    surface_mesh(c, [&](const std::vector<double>& rs, const std::vector<double>& ths, std::vector<Vec3>& out) {
        std::vector<double> wre(rs.size());
        std::vector<double> wim(rs.size());
        for (std::size_t i = 0; i < rs.size(); ++i) {
            wre[i] = rs[i] * std::cos(ths[i]);
            wim[i] = rs[i] * std::sin(ths[i]);
        }
        std::vector<double> zre(rs.size());
        std::vector<double> zim(rs.size());
        fn.eval_complex(rs.size(), { wre.data() }, { wim.data() }, zre.data(), zim.data());
        out.resize(rs.size());
        args.resize(rs.size());
        for (std::size_t i = 0; i < rs.size(); ++i) {
            out[i] = Vec3{ zre[i], zim[i], imag ? wim[i] : wre[i] };
            args[i] = ths[i];
        }
    }, 0.0, rmax, 0.0, k_tau, nr, nt, mesh, grid);
    mesh.col.resize(mesh.pos.size());
    for (std::size_t i = 0; i < mesh.pos.size(); ++i) mesh.col[i] = hsv(args[i] / k_tau, 0.7, 0.95);
    c.scene.s3.meshes.push_back(std::move(mesh));
    if (item.option("mesh")) mesh_lines(c, grid, nr, nt, std::max(1, nr / 10), with_alpha(rgba(0, 0, 0), 0.3));
    st.note = "Each point is (Re z, Im z, " + std::string(imag ? "Im w" : "Re w") + ") for w in the disk |w| \xE2\x89\xA4 " + format_double(rmax, 4) + ". Colors follow arg w, so you can follow one sheet into the next.";
    st.legend_map = "phase";
}

void build_transform2d(const Item& item, Ctx& c, ItemStatus& st) {
    ParseResult r = parse_field(item.field("eq"), c.env, { "u", "v" });
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    ExprPtr vec = r.statement.relation == Relation::None ? r.statement.lhs : r.statement.rhs;
    if (component_count(vec) != 2) {
        st.error = "write the map as \xE2\x9F\xA8x(u, v), y(u, v)\xE2\x9F\xA9";
        return;
    }
    std::string err;
    ExprPtr X = prepare(component(vec, 0), c.env, { "u", "v" }, err);
    ExprPtr Y = X ? prepare(component(vec, 1), c.env, { "u", "v" }, err) : nullptr;
    if (!X || !Y) {
        st.error = err;
        return;
    }
    st.mathtext = "(x, y) = \xE2\x9F\xA8" + to_mathtext(X) + ", " + to_mathtext(Y) + "\xE2\x9F\xA9";
    Range ru;
    Range rv;
    if (!parse_range(item, "umin", "umax", c.env, ru, err) || !parse_range(item, "vmin", "vmax", c.env, rv, err)) {
        st.error = err;
        return;
    }
    double lines = 16.0;
    parse_number(item.field("lines"), c.env, lines, err);
    const int n = static_cast<int>(std::clamp(lines, 2.0, 80.0));
    Func fx(X, { "u", "v" }, false, c.env.geo_type);
    Func fy(Y, { "u", "v" }, false, c.env.geo_type);
    if (!fx.ok() || !fy.ok()) {
        st.error = !fx.ok() ? fx.error() : fy.error();
        return;
    }
    const std::vector<std::string> uv = { "u", "v" };
    const ExprPtr xu = d(X, "u"), xv = d(X, "v"), yu = d(Y, "u"), yv = d(Y, "v");
    ExprPtr det;
    if (xu && xv && yu && yv) {
        det = tidy(sub(mul(xu, yv), mul(xv, yu)));
        st.readouts.push_back({ "J", "[[" + to_string(xu) + ", " + to_string(xv) + "], [" + to_string(yu) + ", " + to_string(yv) + "]]" });
        st.readouts.push_back(formula_readout("det J", det));
    }
    if (item.option("jacobian") && det) {
        Func fd(det, uv, false, NumberType::Hardware);
        const int m = 48;
        std::vector<double> us, vs;
        for (int j = 0; j <= m; ++j) {
            for (int i = 0; i <= m; ++i) {
                us.push_back(ru.a + (ru.b - ru.a) * i / m);
                vs.push_back(rv.a + (rv.b - rv.a) * j / m);
            }
        }
        const std::vector<double> px = fx.map2(us, vs);
        const std::vector<double> py = fy.map2(us, vs);
        const std::vector<double> dv = fd.map2(us, vs);
        std::vector<double> mags;
        for (double q : dv) mags.push_back(std::fabs(q));
        const double top = std::max(1e-12, robust_max(mags));
        auto at = [&](int i, int j) { return static_cast<std::size_t>(j) * (m + 1) + i; };
        for (int j = 0; j < m; ++j) {
            for (int i = 0; i < m; ++i) {
                const std::size_t a = at(i, j), b = at(i + 1, j), cc = at(i + 1, j + 1), dd = at(i, j + 1);
                const Vec2 pa{ px[a], py[a] }, pb{ px[b], py[b] }, pc{ px[cc], py[cc] }, pd{ px[dd], py[dd] };
                if (!finite(pa) || !finite(pb) || !finite(pc) || !finite(pd)) continue;
                const double mid = 0.25 * (std::fabs(dv[a]) + std::fabs(dv[b]) + std::fabs(dv[cc]) + std::fabs(dv[dd]));
                const Rgba col = with_alpha(colormap("magma", 0.15 + 0.8 * std::min(1.0, mid / top)), 0.55);
                c.scene.s2.tris.push_back(Tri2{ pa, pb, pc, col });
                c.scene.s2.tris.push_back(Tri2{ pa, pc, pd, col });
            }
        }
        st.legend_min = "0";
        st.legend_max = number_text(top);
        st.legend_map = "magma";
    }
    const Rgba a = item_color(item);
    Rgba b;
    parse_color(palette()[(std::find(palette().begin(), palette().end(), item.color) - palette().begin() + 1) % palette().size()], b);
    const int samples = 200;
    for (int fam = 0; fam < 2; ++fam) {
        Stroke2 s;
        s.color = fam == 0 ? a : b;
        s.width = 1.6f;
        Stroke2 src;
        src.color = with_alpha(c.env.theme.text, 0.2);
        src.width = 1.0f;
        src.dashed = true;
        for (int k = 0; k <= n; ++k) {
            std::vector<double> us(samples + 1);
            std::vector<double> vs(samples + 1);
            for (int i = 0; i <= samples; ++i) {
                if (fam == 0) {
                    us[static_cast<std::size_t>(i)] = ru.a + (ru.b - ru.a) * k / n;
                    vs[static_cast<std::size_t>(i)] = rv.a + (rv.b - rv.a) * i / samples;
                } else {
                    us[static_cast<std::size_t>(i)] = ru.a + (ru.b - ru.a) * i / samples;
                    vs[static_cast<std::size_t>(i)] = rv.a + (rv.b - rv.a) * k / n;
                }
            }
            const std::vector<double> px = fx.map2(us, vs);
            const std::vector<double> py = fy.map2(us, vs);
            for (std::size_t i = 0; i < us.size(); ++i) {
                s.pts.push_back(Vec2{ px[i], py[i] });
                src.pts.push_back(Vec2{ us[i], vs[i] });
            }
            s.pts.push_back(Vec2{ k_nan, k_nan });
            src.pts.push_back(Vec2{ k_nan, k_nan });
        }
        if (item.option("source")) c.scene.s2.strokes.push_back(std::move(src));
        c.scene.s2.strokes.push_back(std::move(s));
    }
    double u0 = 0.0;
    double v0 = 0.0;
    if (parse_number(item.field("u0"), c.env, u0, err) && parse_number(item.field("v0"), c.env, v0, err) && xu && xv && yu && yv) {
        st.readouts.push_back(sym_vreadout("T(u\xE2\x82\x80, v\xE2\x82\x80)", { X, Y }, uv, { u0, v0 }, c.env));
        st.readouts.push_back({ "J(u\xE2\x82\x80, v\xE2\x82\x80)", "[[" + value_text(xu, uv, { u0, v0 }, c.env) + ", " + value_text(xv, uv, { u0, v0 }, c.env) + "], [" + value_text(yu, uv, { u0, v0 }, c.env) + ", " + value_text(yv, uv, { u0, v0 }, c.env) + "]]" });
        if (det) st.readouts.push_back(sym_readout("det J(u\xE2\x82\x80, v\xE2\x82\x80)", det, uv, { u0, v0 }, c.env));
        if (item.option("cell")) {
            Func gxu(xu, uv, false, NumberType::Hardware), gxv(xv, uv, false, NumberType::Hardware), gyu(yu, uv, false, NumberType::Hardware), gyv(yv, uv, false, NumberType::Hardware);
            const Vec2 p{ fx.at({ u0, v0 }), fy.at({ u0, v0 }) };
            const double du = (ru.b - ru.a) / n;
            const double dv = (rv.b - rv.a) / n;
            const Vec2 eu = Vec2{ gxu.at({ u0, v0 }), gyu.at({ u0, v0 }) } * du;
            const Vec2 ev = Vec2{ gxv.at({ u0, v0 }), gyv.at({ u0, v0 }) } * dv;
            if (finite(p) && finite(eu) && finite(ev)) {
                const Rgba fill = with_alpha(c.env.theme.accent, 0.4);
                c.scene.s2.tris.push_back(Tri2{ p, p + eu, p + eu + ev, fill });
                c.scene.s2.tris.push_back(Tri2{ p, p + eu + ev, p + ev, fill });
                add_arrow2(c.scene.s2, p, p + eu, a, 2.4f);
                add_arrow2(c.scene.s2, p, p + ev, b, 2.4f);
                c.scene.s2.dots.push_back(Dot2{ p, c.env.theme.text, 5.0f, false, "" });
                st.note = "The shaded parallelogram is J times one grid cell (du = " + format_double(du, 4) + ", dv = " + format_double(dv, 4) + "): its area is |det J| du dv.";
            }
        }
    }
}

void build_transform3d(const Item& item, Ctx& c, ItemStatus& st) {
    ParseResult r = parse_field(item.field("eq"), c.env, { "u", "v", "w" });
    if (!r.ok) {
        st.error = r.error;
        return;
    }
    ExprPtr vec = r.statement.relation == Relation::None ? r.statement.lhs : r.statement.rhs;
    if (component_count(vec) != 3) {
        st.error = "write the map as \xE2\x9F\xA8x(u, v, w), y(u, v, w), z(u, v, w)\xE2\x9F\xA9";
        return;
    }
    std::string err;
    std::vector<ExprPtr> comp;
    const std::vector<std::string> uvw = { "u", "v", "w" };
    for (int i = 0; i < 3; ++i) {
        ExprPtr e = prepare(component(vec, static_cast<std::size_t>(i)), c.env, uvw, err);
        if (!e) {
            st.error = err;
            return;
        }
        comp.push_back(e);
    }
    st.mathtext = "(x, y, z) = \xE2\x9F\xA8" + to_mathtext(comp[0]) + ", " + to_mathtext(comp[1]) + ", " + to_mathtext(comp[2]) + "\xE2\x9F\xA9";
    Range rr[3];
    if (!parse_range(item, "umin", "umax", c.env, rr[0], err) || !parse_range(item, "vmin", "vmax", c.env, rr[1], err) || !parse_range(item, "wmin", "wmax", c.env, rr[2], err)) {
        st.error = err;
        return;
    }
    double lines = 6.0;
    parse_number(item.field("lines"), c.env, lines, err);
    const int n = static_cast<int>(std::clamp(lines, 2.0, 24.0));
    Func f[3] = { Func(comp[0], uvw, false, c.env.geo_type), Func(comp[1], uvw, false, c.env.geo_type), Func(comp[2], uvw, false, c.env.geo_type) };
    for (Func& fi : f) {
        if (!fi.ok()) {
            st.error = fi.error();
            return;
        }
    }
    ExprPtr J[3][3];
    bool have_j = true;
    for (int i = 0; i < 3; ++i) {
        for (int k = 0; k < 3; ++k) {
            J[i][k] = d(comp[static_cast<std::size_t>(i)], uvw[static_cast<std::size_t>(k)]);
            if (!J[i][k]) have_j = false;
        }
    }
    ExprPtr det;
    if (have_j) {
        det = tidy(add(sub(mul(J[0][0], sub(mul(J[1][1], J[2][2]), mul(J[1][2], J[2][1]))), mul(J[0][1], sub(mul(J[1][0], J[2][2]), mul(J[1][2], J[2][0])))), mul(J[0][2], sub(mul(J[1][0], J[2][1]), mul(J[1][1], J[2][0])))));
        st.readouts.push_back(formula_readout("det J", det));
    }
    Func fd = det ? Func(det, uvw, false, NumberType::Hardware) : Func();
    const int samples = 60;
    const Rgba fam_colors[3] = { item_color(item), rgba(63, 166, 90), rgba(240, 140, 46) };
    std::vector<Line3> fams(3);
    std::vector<double> all_det;
    for (int fam = 0; fam < 3; ++fam) {
        Line3& line = fams[static_cast<std::size_t>(fam)];
        line.color = fam_colors[fam];
        line.width = 1.6f;
        const int a = (fam + 1) % 3;
        const int b = (fam + 2) % 3;
        for (int ia = 0; ia <= n; ++ia) {
            for (int ib = 0; ib <= n; ++ib) {
                std::vector<double> p[3];
                for (int s = 0; s <= samples; ++s) {
                    double vals[3];
                    vals[fam] = rr[fam].a + (rr[fam].b - rr[fam].a) * s / samples;
                    vals[a] = rr[a].a + (rr[a].b - rr[a].a) * ia / n;
                    vals[b] = rr[b].a + (rr[b].b - rr[b].a) * ib / n;
                    for (int k = 0; k < 3; ++k) p[k].push_back(vals[k]);
                }
                const std::vector<double> X = f[0].map3(p[0], p[1], p[2]);
                const std::vector<double> Y = f[1].map3(p[0], p[1], p[2]);
                const std::vector<double> Z = f[2].map3(p[0], p[1], p[2]);
                std::vector<double> D;
                if (det) D = fd.map3(p[0], p[1], p[2]);
                for (std::size_t s = 0; s < X.size(); ++s) {
                    line.pts.push_back(Vec3{ X[s], Y[s], Z[s] });
                    if (det) all_det.push_back(D[s]);
                }
                line.pts.push_back(Vec3{ k_nan, k_nan, k_nan });
                if (det) all_det.push_back(k_nan);
            }
        }
    }
    if (item.option("jacobian") && det) {
        std::vector<double> mags;
        for (double q : all_det) mags.push_back(std::fabs(q));
        const double top = std::max(1e-12, robust_max(mags));
        std::size_t k = 0;
        for (Line3& line : fams) {
            for (std::size_t i = 0; i < line.pts.size(); ++i, ++k) line.colors.push_back(colormap("magma", 0.15 + 0.85 * std::min(1.0, std::fabs(all_det[k]) / top)));
        }
        st.legend_min = "0";
        st.legend_max = number_text(top);
        st.legend_map = "magma";
    }
    for (Line3& line : fams) c.scene.s3.lines.push_back(std::move(line));
    st.note = "u lines in this item's color, v lines green, w lines orange.";
}

} // namespace build
} // namespace arbor
