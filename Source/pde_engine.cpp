#include "pde_engine.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <numeric>
#include <utility>

namespace arbor {
namespace build {

std::string pde_term_name(int fun, const int ord[3], int tord) {
    static const char* const names[] = { "u", "v", "w" };
    std::string s = names[std::clamp(fun, 0, 2)];
    std::string sub;
    sub.append(static_cast<std::size_t>(ord[0]), 'x');
    sub.append(static_cast<std::size_t>(ord[1]), 'y');
    sub.append(static_cast<std::size_t>(ord[2]), 'z');
    sub.append(static_cast<std::size_t>(tord), 't');
    if (!sub.empty()) s += "_" + sub;
    return s;
}

bool pde_parse_term(const std::string& name, const std::vector<std::string>& funs, const std::string& letters, int& fun, int counts[4]) {
    for (int k = 0; k < 4; ++k) counts[k] = 0;
    for (std::size_t f = 0; f < funs.size(); ++f) {
        if (name == funs[f]) {
            fun = static_cast<int>(f);
            return true;
        }
        const std::string head = funs[f] + "_";
        if (name.size() <= head.size() || name.compare(0, head.size(), head) != 0) continue;
        bool ok = true;
        for (std::size_t i = head.size(); i < name.size(); ++i) {
            const std::size_t at = letters.find(name[i]);
            if (at == std::string::npos || at >= 4) {
                ok = false;
                break;
            }
            ++counts[at];
        }
        if (!ok) {
            for (int k = 0; k < 4; ++k) counts[k] = 0;
            continue;
        }
        fun = static_cast<int>(f);
        return true;
    }
    return false;
}

double PdeSolution::coord(int axis, int i) const {
    if (axis >= dims) return 0.0;
    const double span = hi[axis] - lo[axis];
    if (periodic[axis]) return lo[axis] + span * i / n[axis];
    return n[axis] > 1 ? lo[axis] + span * i / (n[axis] - 1) : lo[axis];
}

double PdeSolution::value(int frame, int fun, int i, int j, int k) const {
    const std::size_t idx = static_cast<std::size_t>(fun) * points() + (static_cast<std::size_t>(k) * n[1] + j) * n[0] + i;
    return frames[static_cast<std::size_t>(frame)][idx];
}

namespace {

struct Csr {
    std::size_t n = 0;
    std::vector<std::size_t> ptr;
    std::vector<int> col;
    std::vector<double> val;

    void multiply(const std::vector<double>& x, std::vector<double>& y) const {
        y.assign(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            double s = 0.0;
            for (std::size_t p = ptr[i]; p < ptr[i + 1]; ++p) s += val[p] * x[static_cast<std::size_t>(col[p])];
            y[i] = s;
        }
    }
};

class Banded {
public:
    bool factor(const Csr& a) {
        n = a.n;
        kl = 0;
        ku = 0;
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t p = a.ptr[i]; p < a.ptr[i + 1]; ++p) {
                const long d = static_cast<long>(a.col[p]) - static_cast<long>(i);
                if (d < 0) kl = std::max(kl, static_cast<std::size_t>(-d));
                else ku = std::max(ku, static_cast<std::size_t>(d));
            }
        }
        w = 2 * kl + ku + 1;
        band.assign(n * w, 0.0);
        piv.assign(n, 0);
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t p = a.ptr[i]; p < a.ptr[i + 1]; ++p) at(i, static_cast<std::size_t>(a.col[p])) += a.val[p];
        }
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t last = std::min(n - 1, k + kl);
            std::size_t p = k;
            double best = std::fabs(at(k, k));
            for (std::size_t i = k + 1; i <= last; ++i) {
                const double v = std::fabs(at(i, k));
                if (v > best) {
                    best = v;
                    p = i;
                }
            }
            if (!(best > 0.0) || !std::isfinite(best)) return false;
            piv[k] = p;
            const std::size_t right = std::min(n - 1, k + ku + kl);
            if (p != k) {
                for (std::size_t j = k; j <= right; ++j) std::swap(at(k, j), at(p, j));
            }
            const double d = at(k, k);
            for (std::size_t i = k + 1; i <= last; ++i) {
                double& m = at(i, k);
                if (m == 0.0) continue;
                m /= d;
                const double mm = m;
                double* ri = &at(i, k + 1);
                const double* rk = &at(k, k + 1);
                for (std::size_t j = k + 1; j <= right; ++j) *ri++ -= mm * *rk++;
            }
        }
        return true;
    }

    void solve(std::vector<double>& b) const {
        for (std::size_t k = 0; k < n; ++k) {
            if (piv[k] != k) std::swap(b[k], b[piv[k]]);
            const std::size_t last = std::min(n - 1, k + kl);
            const double bk = b[k];
            if (bk == 0.0) continue;
            for (std::size_t i = k + 1; i <= last; ++i) b[i] -= at(i, k) * bk;
        }
        for (std::size_t k = n; k-- > 0;) {
            const std::size_t right = std::min(n - 1, k + ku + kl);
            double s = b[k];
            for (std::size_t j = k + 1; j <= right; ++j) s -= at(k, j) * b[j];
            b[k] = s / at(k, k);
        }
    }

    static double cost(const Csr& a) {
        std::size_t l = 0, u = 0;
        for (std::size_t i = 0; i < a.n; ++i) {
            for (std::size_t p = a.ptr[i]; p < a.ptr[i + 1]; ++p) {
                const long d = static_cast<long>(a.col[p]) - static_cast<long>(i);
                if (d < 0) l = std::max(l, static_cast<std::size_t>(-d));
                else u = std::max(u, static_cast<std::size_t>(d));
            }
        }
        return static_cast<double>(a.n) * static_cast<double>(l + 1) * static_cast<double>(2 * l + u + 1);
    }

private:
    double& at(std::size_t i, std::size_t j) { return band[i * w + (j + kl - i)]; }
    double at(std::size_t i, std::size_t j) const { return band[i * w + (j + kl - i)]; }

    std::size_t n = 0, kl = 0, ku = 0, w = 1;
    std::vector<double> band;
    std::vector<std::size_t> piv;
};

class Ilu {
public:
    void factor(const Csr& a) {
        m = a;
        const std::size_t n = m.n;
        diag.assign(n, 0);
        std::vector<long> iw(n, -1);
        for (std::size_t i = 0; i < n; ++i) {
            diag[i] = static_cast<std::size_t>(-1);
            for (std::size_t p = m.ptr[i]; p < m.ptr[i + 1]; ++p) {
                if (static_cast<std::size_t>(m.col[p]) == i) diag[i] = p;
            }
        }
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t p = m.ptr[i]; p < m.ptr[i + 1]; ++p) iw[static_cast<std::size_t>(m.col[p])] = static_cast<long>(p);
            for (std::size_t p = m.ptr[i]; p < m.ptr[i + 1]; ++p) {
                const std::size_t k = static_cast<std::size_t>(m.col[p]);
                if (k >= i) break;
                if (diag[k] == static_cast<std::size_t>(-1)) continue;
                const double piv = m.val[diag[k]];
                m.val[p] /= piv;
                const double f = m.val[p];
                for (std::size_t q = diag[k] + 1; q < m.ptr[k + 1]; ++q) {
                    const long at = iw[static_cast<std::size_t>(m.col[q])];
                    if (at >= 0) m.val[static_cast<std::size_t>(at)] -= f * m.val[q];
                }
            }
            for (std::size_t p = m.ptr[i]; p < m.ptr[i + 1]; ++p) iw[static_cast<std::size_t>(m.col[p])] = -1;
            if (diag[i] != static_cast<std::size_t>(-1)) {
                double& d = m.val[diag[i]];
                if (std::fabs(d) < 1e-14 || !std::isfinite(d)) d = d < 0.0 ? -1e-8 : 1e-8;
            }
        }
    }

    void apply(const std::vector<double>& r, std::vector<double>& z) const {
        const std::size_t n = m.n;
        z = r;
        for (std::size_t i = 0; i < n; ++i) {
            double s = z[i];
            for (std::size_t p = m.ptr[i]; p < m.ptr[i + 1]; ++p) {
                const std::size_t k = static_cast<std::size_t>(m.col[p]);
                if (k >= i) break;
                s -= m.val[p] * z[k];
            }
            z[i] = s;
        }
        for (std::size_t i = n; i-- > 0;) {
            double s = z[i];
            if (diag[i] == static_cast<std::size_t>(-1)) {
                z[i] = s;
                continue;
            }
            for (std::size_t p = diag[i] + 1; p < m.ptr[i + 1]; ++p) s -= m.val[p] * z[static_cast<std::size_t>(m.col[p])];
            z[i] = s / m.val[diag[i]];
        }
    }

private:
    Csr m;
    std::vector<std::size_t> diag;
};

double norm2(const std::vector<double>& v) {
    double s = 0.0;
    for (double x : v) s += x * x;
    return std::sqrt(s);
}

int gmres(const Csr& a, const Ilu& pre, const std::vector<double>& b, std::vector<double>& x, double tol, int max_iter, bool& ok) {
    const std::size_t n = a.n;
    const int m = 40;
    x.assign(n, 0.0);
    const double bnorm = norm2(b);
    ok = true;
    if (bnorm == 0.0) return 0;
    std::vector<double> r = b;
    double beta = bnorm;
    int total = 0;
    thread_local std::vector<std::vector<double>> V;
    thread_local std::vector<std::vector<double>> Z;
    V.resize(m + 1);
    Z.resize(m);
    for (auto& v : V) v.resize(n);
    for (auto& z : Z) z.resize(n);
    std::vector<std::vector<double>> H(m + 1, std::vector<double>(m, 0.0));
    std::vector<double> cs(m), sn(m), g(m + 1);
    std::vector<double> w;
    while (total < max_iter) {
        for (std::size_t i = 0; i < n; ++i) V[0][i] = r[i] / beta;
        std::fill(g.begin(), g.end(), 0.0);
        g[0] = beta;
        int j = 0;
        bool done = false;
        for (; j < m && total < max_iter; ++j, ++total) {
            pre.apply(V[static_cast<std::size_t>(j)], Z[static_cast<std::size_t>(j)]);
            a.multiply(Z[static_cast<std::size_t>(j)], w);
            for (int i = 0; i <= j; ++i) {
                double h = 0.0;
                const std::vector<double>& vi = V[static_cast<std::size_t>(i)];
                for (std::size_t k = 0; k < n; ++k) h += w[k] * vi[k];
                H[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = h;
                for (std::size_t k = 0; k < n; ++k) w[k] -= h * vi[k];
            }
            const double hn = norm2(w);
            H[static_cast<std::size_t>(j) + 1][static_cast<std::size_t>(j)] = hn;
            if (hn > 0.0) {
                for (std::size_t k = 0; k < n; ++k) V[static_cast<std::size_t>(j) + 1][k] = w[k] / hn;
            }
            for (int i = 0; i < j; ++i) {
                const double h0 = H[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
                const double h1 = H[static_cast<std::size_t>(i) + 1][static_cast<std::size_t>(j)];
                H[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = cs[static_cast<std::size_t>(i)] * h0 + sn[static_cast<std::size_t>(i)] * h1;
                H[static_cast<std::size_t>(i) + 1][static_cast<std::size_t>(j)] = -sn[static_cast<std::size_t>(i)] * h0 + cs[static_cast<std::size_t>(i)] * h1;
            }
            const double h0 = H[static_cast<std::size_t>(j)][static_cast<std::size_t>(j)];
            const double h1 = H[static_cast<std::size_t>(j) + 1][static_cast<std::size_t>(j)];
            const double den = std::hypot(h0, h1);
            cs[static_cast<std::size_t>(j)] = den > 0.0 ? h0 / den : 1.0;
            sn[static_cast<std::size_t>(j)] = den > 0.0 ? h1 / den : 0.0;
            H[static_cast<std::size_t>(j)][static_cast<std::size_t>(j)] = den;
            H[static_cast<std::size_t>(j) + 1][static_cast<std::size_t>(j)] = 0.0;
            g[static_cast<std::size_t>(j) + 1] = -sn[static_cast<std::size_t>(j)] * g[static_cast<std::size_t>(j)];
            g[static_cast<std::size_t>(j)] = cs[static_cast<std::size_t>(j)] * g[static_cast<std::size_t>(j)];
            if (std::fabs(g[static_cast<std::size_t>(j) + 1]) <= tol * bnorm || hn == 0.0) {
                ++j;
                ++total;
                done = true;
                break;
            }
        }
        std::vector<double> y(static_cast<std::size_t>(j), 0.0);
        for (int i = j - 1; i >= 0; --i) {
            double s = g[static_cast<std::size_t>(i)];
            for (int k = i + 1; k < j; ++k) s -= H[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] * y[static_cast<std::size_t>(k)];
            const double d = H[static_cast<std::size_t>(i)][static_cast<std::size_t>(i)];
            y[static_cast<std::size_t>(i)] = d != 0.0 ? s / d : 0.0;
        }
        for (int i = 0; i < j; ++i) {
            const std::vector<double>& zi = Z[static_cast<std::size_t>(i)];
            const double yi = y[static_cast<std::size_t>(i)];
            for (std::size_t k = 0; k < n; ++k) x[k] += yi * zi[k];
        }
        a.multiply(x, w);
        for (std::size_t k = 0; k < n; ++k) r[k] = b[k] - w[k];
        beta = norm2(r);
        if (beta <= tol * bnorm * 10.0) return total;
        if (done && beta <= tol * bnorm * 1000.0) return total;
        if (!std::isfinite(beta)) break;
    }
    ok = beta <= 1e-6 * bnorm;
    return total;
}

struct Linear {
    bool banded = false;
    Banded lu;
    Ilu ilu;
    Csr a;
    std::vector<double> rowscale;
    bool ready = false;
};

struct Tap {
    int off[3];
    double w;
};

std::vector<std::pair<int, double>> taps1(int order, double h) {
    switch (order) {
    case 0: return { { 0, 1.0 } };
    case 1: return { { -1, -0.5 / h }, { 1, 0.5 / h } };
    case 2: return { { -1, 1.0 / (h * h) }, { 0, -2.0 / (h * h) }, { 1, 1.0 / (h * h) } };
    case 3: {
        const double c = 1.0 / (h * h * h);
        return { { -2, -0.5 * c }, { -1, c }, { 1, -c }, { 2, 0.5 * c } };
    }
    default: {
        const double c = 1.0 / (h * h * h * h);
        return { { -2, c }, { -1, -4.0 * c }, { 0, 6.0 * c }, { 1, -4.0 * c }, { 2, c } };
    }
    }
}

int reach_of(int order) {
    return order == 0 ? 0 : order <= 2 ? 1 : 2;
}

class Engine {
public:
    explicit Engine(const PdeProblem& p) : P(p) {}

    bool run(PdeSolution& out, std::string& err);

private:
    struct Partial {
        int term = 0;
        std::unique_ptr<Func> f;
        bool numeric = false;
    };

    struct TermInfo {
        int fun = 0;
        int level = 0;
        std::vector<Tap> taps;
        std::vector<long> nb;
    };

    struct CondFn {
        int axis = 0;
        int side = 0;
        int fun = 0;
        std::unique_ptr<Func> f;
        std::vector<Partial> parts;
        std::vector<double> res;
        std::vector<std::vector<double>> coef;
    };

    struct Plan {
        int dirichlet = -1;
        std::vector<int> layer;
    };

    bool setup(std::string& err);
    bool make_partials(const ExprPtr& e, std::vector<Partial>& out, std::string& err);
    void eval_partials(Func& base, std::vector<Partial>& parts, std::size_t count, std::vector<const double*>& in, const std::vector<double>& base_values, std::vector<std::vector<double>>& out);
    long nbr(std::size_t e, const int off[3]) const;
    std::size_t ext_of(const int c[3]) const { return static_cast<std::size_t>(c[0]) + static_cast<std::size_t>(E[0]) * (static_cast<std::size_t>(c[1]) + static_cast<std::size_t>(E[1]) * static_cast<std::size_t>(c[2])); }
    void decode(std::size_t e, int c[3]) const {
        c[0] = static_cast<int>(e % static_cast<std::size_t>(E[0]));
        c[1] = static_cast<int>((e / static_cast<std::size_t>(E[0])) % static_cast<std::size_t>(E[1]));
        c[2] = static_cast<int>(e / (static_cast<std::size_t>(E[0]) * static_cast<std::size_t>(E[1])));
    }
    void set_affine(int mode, double dt);
    void compute_levels();
    void assemble(double t, bool initial, bool matrix);
    void build_pattern();
    bool linear_solve(int key, std::vector<double>& rhs, bool rebuild);
    bool newton(double t, bool initial, int& iters);
    bool advance(double t, double dt, int mode, int depth);
    void record(PdeSolution& out, double t);
    void fill_ghost_states();
    double level0(int f, std::size_t e) const;
    int lev_for(int f, bool initial) const { return (initial && P.korder[static_cast<std::size_t>(f)] >= 1) ? P.korder[static_cast<std::size_t>(f)] : 0; }

    const PdeProblem& P;
    int nf = 1;
    int n[3] = { 1, 1, 1 };
    int g[3] = { 0, 0, 0 };
    int E[3] = { 1, 1, 1 };
    bool per[3] = { false, false, false };
    double h[3] = { 1.0, 1.0, 1.0 };
    std::size_t Next = 0, Nreal = 0, Nu = 0;
    std::vector<long> ext_real;
    std::vector<std::size_t> real_ext;
    std::vector<double> RX, RY, RZ, Tt;
    std::vector<TermInfo> T;
    std::vector<std::string> varnames;
    std::vector<std::unique_ptr<Func>> G;
    std::vector<std::vector<Partial>> GP;
    std::vector<CondFn> conds;
    Plan plan[3][2][3];
    std::vector<long> replaced;
    std::vector<std::size_t> face_nodes[3][2];
    std::vector<long> face_pos[3][2];
    std::vector<std::vector<std::vector<double>>> S, Sprev, K, Lv;
    std::vector<std::vector<double>> A;
    std::vector<std::vector<double>> cc;
    std::vector<std::vector<double>> TV;
    std::vector<std::vector<double>> GR;
    std::vector<std::vector<std::vector<double>>> GC;
    std::vector<std::size_t> trow;
    std::vector<int> tcol;
    std::vector<double> tval;
    std::vector<double> R;
    std::vector<double> rowscale;
    std::vector<std::size_t> pat_pos;
    std::vector<std::size_t> pat_row;
    std::vector<int> pat_col;
    Csr J;
    bool linear = false;
    bool constant = false;
    bool history = false;
    std::map<std::pair<int, long long>, std::unique_ptr<Linear>> cache;
    int cache_key = 0;
    long long cache_dt = 0;
    double scale0 = 1.0;
    std::string solver_name;
    long long lin_iters = 0;
    int newton_total = 0;
    int newton_max = 0;
    bool stalled = false;
    bool substepped = false;
};

long Engine::nbr(std::size_t e, const int off[3]) const {
    int c[3];
    decode(e, c);
    for (int a = 0; a < 3; ++a) {
        if (off[a] == 0) continue;
        int v = c[a] + off[a];
        if (per[a]) v = ((v % n[a]) + n[a]) % n[a];
        else if (v < 0 || v >= E[a]) return -1;
        c[a] = v;
    }
    return static_cast<long>(ext_of(c));
}

bool Engine::make_partials(const ExprPtr& e, std::vector<Partial>& out, std::string& err) {
    for (std::size_t k = 0; k < P.terms.size(); ++k) {
        if (!depends_on(e, P.terms[k].name)) continue;
        Partial p;
        p.term = static_cast<int>(k);
        const ExprPtr de = d(e, P.terms[k].name);
        if (de) {
            p.f = std::make_unique<Func>(simplify(de), varnames, false, NumberType::Hardware);
            if (!p.f->ok()) p.f.reset();
        }
        if (!p.f) p.numeric = true;
        if (p.f) {
            for (const PdeTerm& t : P.terms) {
                if (depends_on(p.f->expr(), t.name)) linear = false;
            }
            if (depends_on(p.f->expr(), "t")) constant = false;
        } else {
            linear = false;
        }
        out.push_back(std::move(p));
    }
    (void)err;
    return true;
}

void Engine::eval_partials(Func& base, std::vector<Partial>& parts, std::size_t count, std::vector<const double*>& in, const std::vector<double>& base_values, std::vector<std::vector<double>>& out) {
    out.resize(parts.size());
    for (std::size_t q = 0; q < parts.size(); ++q) {
        out[q].resize(count);
        if (parts[q].f) {
            parts[q].f->eval(count, in, out[q].data());
            continue;
        }
        const std::size_t slot = 4 + static_cast<std::size_t>(parts[q].term);
        const double* keep = in[slot];
        std::vector<double> bumped(keep, keep + count);
        std::vector<double> eps(count);
        for (std::size_t i = 0; i < count; ++i) {
            eps[i] = 1e-6 * (1.0 + std::fabs(bumped[i]));
            bumped[i] += eps[i];
        }
        in[slot] = bumped.data();
        base.eval(count, in, out[q].data());
        in[slot] = keep;
        for (std::size_t i = 0; i < count; ++i) out[q][i] = (out[q][i] - base_values[i]) / eps[i];
    }
}

bool Engine::setup(std::string& err) {
    nf = P.nfun;
    if (static_cast<int>(P.eqs.size()) != nf) {
        err = "there has to be one equation for each function";
        return false;
    }
    int maxo[3] = { 0, 0, 0 };
    auto note_term = [&](const PdeTerm& t) {
        for (int a = 0; a < 3; ++a) maxo[a] = std::max(maxo[a], t.ord[a]);
    };
    for (const PdeTerm& t : P.terms) note_term(t);
    for (int a = 0; a < 3; ++a) {
        if (maxo[a] > 4) {
            err = std::string("Arbor handles up to fourth derivatives in each of x, y and z");
            return false;
        }
        if (a >= P.dims && maxo[a] > 0) {
            err = std::string("the equation uses a derivative in ") + "xyz"[a] + ", which isn't one of the space variables here";
            return false;
        }
    }
    for (int a = 0; a < 3; ++a) {
        if (a >= P.dims) {
            n[a] = 1;
            g[a] = 0;
            per[a] = false;
            h[a] = 1.0;
            E[a] = 1;
            continue;
        }
        n[a] = std::max(5, P.n[a]);
        per[a] = P.edge[a][0].mode == PdeEdgeMode::Periodic || P.edge[a][1].mode == PdeEdgeMode::Periodic;
        h[a] = per[a] ? (P.hi[a] - P.lo[a]) / n[a] : (P.hi[a] - P.lo[a]) / (n[a] - 1);
        g[a] = per[a] ? 0 : reach_of(maxo[a]);
        if (!per[a]) {
            for (int s = 0; s < 2; ++s) {
                for (const PdeCondition& c : P.edge[a][s].conds) g[a] = std::max(g[a], c.reach);
            }
        }
        if (n[a] <= 2 * std::max(g[a], 2)) n[a] = 2 * std::max(g[a], 2) + 1;
        E[a] = n[a] + 2 * g[a];
    }
    Next = static_cast<std::size_t>(E[0]) * E[1] * E[2];
    Nreal = static_cast<std::size_t>(n[0]) * n[1] * n[2];
    Nu = Next * static_cast<std::size_t>(nf);
    ext_real.assign(Next, -1);
    real_ext.resize(Nreal);
    RX.resize(Nreal);
    RY.resize(Nreal);
    RZ.resize(Nreal);
    for (int k = 0; k < n[2]; ++k) {
        for (int j = 0; j < n[1]; ++j) {
            for (int i = 0; i < n[0]; ++i) {
                const std::size_t r = static_cast<std::size_t>(i) + static_cast<std::size_t>(n[0]) * (static_cast<std::size_t>(j) + static_cast<std::size_t>(n[1]) * static_cast<std::size_t>(k));
                const int c[3] = { i + g[0], j + g[1], k + g[2] };
                const std::size_t e = ext_of(c);
                real_ext[r] = e;
                ext_real[e] = static_cast<long>(r);
                RX[r] = P.dims > 0 ? P.lo[0] + h[0] * i : 0.0;
                RY[r] = P.dims > 1 ? P.lo[1] + h[1] * j : 0.0;
                RZ[r] = P.dims > 2 ? P.lo[2] + h[2] * k : 0.0;
            }
        }
    }
    Tt.assign(Nreal, P.t0);
    varnames = { "x", "y", "z", "t" };
    for (const PdeTerm& t : P.terms) varnames.push_back(t.name);
    T.resize(P.terms.size());
    for (std::size_t q = 0; q < P.terms.size(); ++q) {
        const PdeTerm& pt = P.terms[q];
        TermInfo& ti = T[q];
        ti.fun = pt.fun;
        ti.level = pt.tord;
        if (pt.tord > P.korder[static_cast<std::size_t>(pt.fun)]) {
            err = pt.name + " goes past the highest time derivative of its function in the equations";
            return false;
        }
        std::vector<std::pair<int, double>> s0 = taps1(pt.ord[0], h[0]);
        std::vector<std::pair<int, double>> s1 = taps1(pt.ord[1], h[1]);
        std::vector<std::pair<int, double>> s2 = taps1(pt.ord[2], h[2]);
        for (const auto& a : s0) {
            for (const auto& b : s1) {
                for (const auto& c : s2) ti.taps.push_back(Tap{ { a.first, b.first, c.first }, a.second * b.second * c.second });
            }
        }
        ti.nb.resize(ti.taps.size() * Nreal);
        for (std::size_t s = 0; s < ti.taps.size(); ++s) {
            for (std::size_t r = 0; r < Nreal; ++r) ti.nb[s * Nreal + r] = nbr(real_ext[r], ti.taps[s].off);
        }
    }
    linear = true;
    constant = true;
    G.clear();
    GP.clear();
    GP.resize(static_cast<std::size_t>(nf));
    for (int e = 0; e < nf; ++e) {
        G.push_back(std::make_unique<Func>(P.eqs[static_cast<std::size_t>(e)], varnames, false, NumberType::Hardware));
        if (!G.back()->ok()) {
            err = G.back()->error();
            return false;
        }
        if (!make_partials(P.eqs[static_cast<std::size_t>(e)], GP[static_cast<std::size_t>(e)], err)) return false;
        if (GP[static_cast<std::size_t>(e)].empty()) {
            err = "an equation has none of the functions in it";
            return false;
        }
    }
    replaced.assign(Nreal * static_cast<std::size_t>(nf), -1);
    for (int a = 0; a < P.dims; ++a) {
        if (per[a]) continue;
        for (int s = 0; s < 2; ++s) {
            face_nodes[a][s].clear();
            face_pos[a][s].assign(Nreal, -1);
            for (std::size_t r = 0; r < Nreal; ++r) {
                int c[3];
                decode(real_ext[r], c);
                const int idx = c[a] - g[a];
                if ((s == 0 && idx == 0) || (s == 1 && idx == n[a] - 1)) {
                    face_pos[a][s][r] = static_cast<long>(face_nodes[a][s].size());
                    face_nodes[a][s].push_back(r);
                }
            }
            const PdeEdge& edge = P.edge[a][s];
            for (int f = 0; f < nf; ++f) {
                Plan& pl = plan[a][s][f];
                pl.dirichlet = -1;
                pl.layer.assign(static_cast<std::size_t>(g[a]), -1);
                if (edge.mode == PdeEdgeMode::Insulated) {
                    std::fill(pl.layer.begin(), pl.layer.end(), -2);
                    continue;
                }
                if (edge.mode == PdeEdgeMode::Free) continue;
                std::vector<std::pair<int, int>> normal;
                for (std::size_t q = 0; q < edge.conds.size(); ++q) {
                    const PdeCondition& pc = edge.conds[q];
                    if (pc.fun != f) continue;
                    CondFn cf;
                    cf.axis = a;
                    cf.side = s;
                    cf.fun = f;
                    cf.f = std::make_unique<Func>(pc.residual, varnames, false, NumberType::Hardware);
                    if (!cf.f->ok()) {
                        err = cf.f->error();
                        return false;
                    }
                    make_partials(pc.residual, cf.parts, err);
                    if (cf.parts.empty()) {
                        err = "the edge condition " + pc.text + " doesn't involve the functions";
                        return false;
                    }
                    const int id = static_cast<int>(conds.size());
                    conds.push_back(std::move(cf));
                    if (pc.value) {
                        if (pl.dirichlet >= 0) {
                            err = "an edge can fix the value of each function only once (" + pc.text + ")";
                            return false;
                        }
                        pl.dirichlet = id;
                    } else {
                        normal.push_back({ pc.reach, id });
                    }
                }
                std::stable_sort(normal.begin(), normal.end());
                if (static_cast<int>(normal.size()) > g[a]) {
                    err = std::string("an edge in ") + "xyz"[a] + " has more derivative conditions than this equation needs (it takes at most " + std::to_string(g[a]) + ")";
                    return false;
                }
                for (std::size_t q = 0; q < normal.size(); ++q) pl.layer[q] = normal[q].second;
                if (pl.dirichlet >= 0) {
                    for (std::size_t r : face_nodes[a][s]) {
                        long& slot = replaced[r * static_cast<std::size_t>(nf) + static_cast<std::size_t>(f)];
                        if (slot < 0) slot = pl.dirichlet;
                    }
                }
            }
        }
    }
    S.assign(static_cast<std::size_t>(nf), {});
    Sprev.assign(static_cast<std::size_t>(nf), {});
    K.assign(static_cast<std::size_t>(nf), {});
    Lv.assign(static_cast<std::size_t>(nf), {});
    A.assign(static_cast<std::size_t>(nf), std::vector<double>(Next, 0.0));
    cc.assign(static_cast<std::size_t>(nf), {});
    for (int f = 0; f < nf; ++f) {
        const int k = P.korder[static_cast<std::size_t>(f)];
        S[static_cast<std::size_t>(f)].assign(static_cast<std::size_t>(k), std::vector<double>(Next, 0.0));
        Sprev[static_cast<std::size_t>(f)] = S[static_cast<std::size_t>(f)];
        K[static_cast<std::size_t>(f)].assign(static_cast<std::size_t>(k) + 1, std::vector<double>(Next, 0.0));
        Lv[static_cast<std::size_t>(f)].assign(static_cast<std::size_t>(k) + 1, std::vector<double>(Next, 0.0));
        cc[static_cast<std::size_t>(f)].assign(static_cast<std::size_t>(k) + 1, 0.0);
    }
    TV.assign(P.terms.size(), std::vector<double>(Nreal, 0.0));
    constant = constant && linear;
    return true;
}

double Engine::level0(int f, std::size_t e) const {
    const int k = P.korder[static_cast<std::size_t>(f)];
    return k == 0 ? A[static_cast<std::size_t>(f)][e] : S[static_cast<std::size_t>(f)][0][e];
}

void Engine::fill_ghost_states() {
    for (std::size_t e = 0; e < Next; ++e) {
        if (ext_real[e] >= 0) continue;
        int c[3];
        decode(e, c);
        int b[3] = { c[0], c[1], c[2] };
        int in1[3];
        double wq = 0.0;
        int gax = -1;
        bool mirror = false;
        for (int a = 0; a < 3; ++a) {
            const int idx = c[a] - g[a];
            if (idx < 0) {
                b[a] = g[a];
                gax = a;
                wq = -idx;
                mirror = P.edge[a][0].mode == PdeEdgeMode::Insulated;
            } else if (idx >= n[a]) {
                b[a] = g[a] + n[a] - 1;
                gax = a;
                wq = idx - n[a] + 1;
                mirror = P.edge[a][1].mode == PdeEdgeMode::Insulated;
            }
        }
        for (int a = 0; a < 3; ++a) in1[a] = b[a];
        int qn = static_cast<int>(wq);
        if (gax >= 0) in1[gax] += (b[gax] == g[gax]) ? (mirror ? qn : 1) : (mirror ? -qn : -1);
        const std::size_t be = ext_of(b);
        const std::size_t ie = ext_of(in1);
        for (int f = 0; f < nf; ++f) {
            const int k = P.korder[static_cast<std::size_t>(f)];
            for (int j = 0; j < k; ++j) {
                std::vector<double>& s = S[static_cast<std::size_t>(f)][static_cast<std::size_t>(j)];
                s[e] = mirror ? s[ie] : (wq + 1.0) * s[be] - wq * s[ie];
            }
            if (k == 0) {
                std::vector<double>& s = A[static_cast<std::size_t>(f)];
                s[e] = mirror ? s[ie] : (wq + 1.0) * s[be] - wq * s[ie];
            }
        }
    }
}

void Engine::set_affine(int mode, double dt) {
    for (int f = 0; f < nf; ++f) {
        const std::size_t fi = static_cast<std::size_t>(f);
        const int k = P.korder[fi];
        std::vector<std::vector<double>>& Kf = K[fi];
        std::vector<double>& cf = cc[fi];
        std::fill(Kf[static_cast<std::size_t>(k)].begin(), Kf[static_cast<std::size_t>(k)].end(), 0.0);
        cf[static_cast<std::size_t>(k)] = 1.0;
        if (mode == -1) {
            for (int j = 0; j < k; ++j) {
                Kf[static_cast<std::size_t>(j)] = S[fi][static_cast<std::size_t>(j)];
                cf[static_cast<std::size_t>(j)] = 0.0;
            }
            continue;
        }
        double beta = dt;
        if (mode == 0) beta = 0.5 * dt;
        else if (mode == 1) beta = 2.0 * dt / 3.0;
        for (int j = k - 1; j >= 0; --j) {
            const std::size_t js = static_cast<std::size_t>(j);
            std::vector<double>& out = Kf[js];
            const std::vector<double>& sj = S[fi][js];
            const std::vector<double>& next = Kf[js + 1];
            if (mode == 0) {
                const std::vector<double>& up = (j + 1 < k) ? S[fi][js + 1] : A[fi];
                for (std::size_t e = 0; e < Next; ++e) out[e] = sj[e] + beta * up[e] + beta * next[e];
            } else if (mode == 1) {
                const std::vector<double>& sp = Sprev[fi][js];
                for (std::size_t e = 0; e < Next; ++e) out[e] = (4.0 * sj[e] - sp[e]) / 3.0 + beta * next[e];
            } else {
                for (std::size_t e = 0; e < Next; ++e) out[e] = sj[e] + beta * next[e];
            }
            cf[js] = beta * cf[js + 1];
        }
    }
}

void Engine::compute_levels() {
    for (int f = 0; f < nf; ++f) {
        const std::size_t fi = static_cast<std::size_t>(f);
        const int k = P.korder[fi];
        for (int j = 0; j <= k; ++j) {
            const std::size_t js = static_cast<std::size_t>(j);
            std::vector<double>& out = Lv[fi][js];
            const std::vector<double>& kk = K[fi][js];
            const double c = cc[fi][js];
            const std::vector<double>& a = A[fi];
            for (std::size_t e = 0; e < Next; ++e) out[e] = kk[e] + c * a[e];
        }
    }
    for (std::size_t q = 0; q < T.size(); ++q) {
        const TermInfo& ti = T[q];
        const std::vector<double>& L = Lv[static_cast<std::size_t>(ti.fun)][static_cast<std::size_t>(ti.level)];
        std::vector<double>& out = TV[q];
        std::fill(out.begin(), out.end(), 0.0);
        for (std::size_t s = 0; s < ti.taps.size(); ++s) {
            const double w = ti.taps[s].w;
            const long* nb = &ti.nb[s * Nreal];
            for (std::size_t r = 0; r < Nreal; ++r) {
                const long e = nb[r];
                if (e >= 0) out[r] += w * L[static_cast<std::size_t>(e)];
            }
        }
    }
}

void Engine::assemble(double t, bool initial, bool matrix) {
    compute_levels();
    std::fill(Tt.begin(), Tt.end(), t);
    std::vector<const double*> in = { RX.data(), RY.data(), RZ.data(), Tt.data() };
    for (const auto& tv : TV) in.push_back(tv.data());
    GR.resize(static_cast<std::size_t>(nf));
    GC.resize(static_cast<std::size_t>(nf));
    for (int e = 0; e < nf; ++e) {
        const std::size_t es = static_cast<std::size_t>(e);
        GR[es].resize(Nreal);
        G[es]->eval(Nreal, in, GR[es].data());
        if (matrix) eval_partials(*G[es], GP[es], Nreal, in, GR[es], GC[es]);
    }
    for (CondFn& cf : conds) {
        const std::vector<std::size_t>& nodes = face_nodes[cf.axis][cf.side];
        const std::size_t m = nodes.size();
        std::vector<std::vector<double>> cols(4 + TV.size(), std::vector<double>(m));
        for (std::size_t i = 0; i < m; ++i) {
            const std::size_t r = nodes[i];
            cols[0][i] = RX[r];
            cols[1][i] = RY[r];
            cols[2][i] = RZ[r];
            cols[3][i] = t;
            for (std::size_t q = 0; q < TV.size(); ++q) cols[4 + q][i] = TV[q][r];
        }
        std::vector<const double*> cin;
        for (const auto& col : cols) cin.push_back(col.data());
        cf.res.resize(m);
        cf.f->eval(m, cin, cf.res.data());
        if (matrix) eval_partials(*cf.f, cf.parts, m, cin, cf.res, cf.coef);
    }
    R.assign(Nu, 0.0);
    trow.clear();
    tcol.clear();
    tval.clear();
    auto push = [&](std::size_t row, long col, double v) {
        if (!matrix || col < 0) return;
        trow.push_back(row);
        tcol.push_back(static_cast<int>(col));
        tval.push_back(v);
    };
    auto term_row = [&](std::size_t row, std::size_t r, const std::vector<Partial>& parts, const std::vector<std::vector<double>>& coef, std::size_t at) {
        if (!matrix) return;
        for (std::size_t p = 0; p < parts.size(); ++p) {
            const double cval = coef[p][at];
            if (!std::isfinite(cval)) continue;
            const TermInfo& ti = T[static_cast<std::size_t>(parts[p].term)];
            const double lv = cc[static_cast<std::size_t>(ti.fun)][static_cast<std::size_t>(ti.level)];
            if (lv == 0.0) continue;
            for (std::size_t s = 0; s < ti.taps.size(); ++s) {
                const long e = ti.nb[s * Nreal + r];
                if (e < 0) continue;
                push(row, e * nf + ti.fun, cval * ti.taps[s].w * lv);
            }
        }
    };
    for (std::size_t r = 0; r < Nreal; ++r) {
        const std::size_t e = real_ext[r];
        for (int f = 0; f < nf; ++f) {
            const std::size_t row = e * static_cast<std::size_t>(nf) + static_cast<std::size_t>(f);
            long rep = replaced[r * static_cast<std::size_t>(nf) + static_cast<std::size_t>(f)];
            if (initial && P.korder[static_cast<std::size_t>(f)] >= 1) rep = -1;
            if (rep >= 0) {
                CondFn& cf = conds[static_cast<std::size_t>(rep)];
                const std::size_t at = static_cast<std::size_t>(face_pos[cf.axis][cf.side][r]);
                R[row] = cf.res[at];
                term_row(row, r, cf.parts, cf.coef, at);
            } else {
                R[row] = GR[static_cast<std::size_t>(f)][r];
                term_row(row, r, GP[static_cast<std::size_t>(f)], GC[static_cast<std::size_t>(f)], r);
            }
        }
    }
    for (std::size_t e = 0; e < Next; ++e) {
        if (ext_real[e] >= 0) continue;
        int c[3];
        decode(e, c);
        int gaxes[3];
        int ng = 0;
        int side[3] = { 0, 0, 0 };
        int layer[3] = { 0, 0, 0 };
        for (int a = 0; a < 3; ++a) {
            const int idx = c[a] - g[a];
            if (idx < 0) {
                gaxes[ng++] = a;
                side[a] = 0;
                layer[a] = -idx;
            } else if (idx >= n[a]) {
                gaxes[ng++] = a;
                side[a] = 1;
                layer[a] = idx - n[a] + 1;
            }
        }
        for (int f = 0; f < nf; ++f) {
            const std::size_t row = e * static_cast<std::size_t>(nf) + static_cast<std::size_t>(f);
            const int lev = lev_for(f, initial);
            const std::vector<double>& L = Lv[static_cast<std::size_t>(f)][static_cast<std::size_t>(lev)];
            const double lc = cc[static_cast<std::size_t>(f)][static_cast<std::size_t>(lev)];
            auto put = [&](const int p[3], double w) {
                const std::size_t pe = ext_of(p);
                R[row] += w * L[pe];
                push(row, static_cast<long>(pe * static_cast<std::size_t>(nf) + static_cast<std::size_t>(f)), w * lc);
            };
            if (ng >= 2) {
                put(c, 1.0);
                const int full = (1 << ng) - 1;
                for (int mask = 0; mask < full; ++mask) {
                    int p[3] = { c[0], c[1], c[2] };
                    int out_count = 0;
                    for (int q = 0; q < ng; ++q) {
                        const int a = gaxes[q];
                        if (!(mask & (1 << q))) {
                            p[a] = side[a] == 0 ? g[a] : g[a] + n[a] - 1;
                            ++out_count;
                        }
                    }
                    const double sign = (out_count % 2 == 1) ? 1.0 : -1.0;
                    put(p, -sign);
                }
                continue;
            }
            const int a = gaxes[0];
            const int s = side[a];
            const int q = layer[a];
            int b[3] = { c[0], c[1], c[2] };
            b[a] = s == 0 ? g[a] : g[a] + n[a] - 1;
            const int inward = s == 0 ? 1 : -1;
            int kind = plan[a][s][f].layer.size() >= static_cast<std::size_t>(q) ? plan[a][s][f].layer[static_cast<std::size_t>(q) - 1] : -1;
            if (initial && P.korder[static_cast<std::size_t>(f)] >= 1 && kind >= 0) kind = -1;
            if (kind >= 0) {
                CondFn& cf = conds[static_cast<std::size_t>(kind)];
                const long rb = ext_real[ext_of(b)];
                const std::size_t at = static_cast<std::size_t>(face_pos[a][s][static_cast<std::size_t>(rb)]);
                R[row] = cf.res[at];
                term_row(row, static_cast<std::size_t>(rb), cf.parts, cf.coef, at);
            } else if (kind == -2) {
                int m[3] = { b[0], b[1], b[2] };
                m[a] += inward * q;
                put(c, 1.0);
                put(m, -1.0);
            } else {
                int b1[3] = { b[0], b[1], b[2] };
                b1[a] += inward;
                put(c, 1.0);
                put(b, -(q + 1.0));
                put(b1, static_cast<double>(q));
            }
        }
    }
    if (!matrix) return;
    if (pat_pos.size() == trow.size() && J.n == Nu && pat_row == trow && pat_col == tcol) {
        std::fill(J.val.begin(), J.val.end(), 0.0);
        for (std::size_t k = 0; k < tval.size(); ++k) J.val[pat_pos[k]] += tval[k];
    } else {
        build_pattern();
    }
    rowscale.assign(Nu, 1.0);
    for (std::size_t i = 0; i < Nu; ++i) {
        double big = 0.0;
        for (std::size_t p = J.ptr[i]; p < J.ptr[i + 1]; ++p) big = std::max(big, std::fabs(J.val[p]));
        if (!(big > 0.0) || !std::isfinite(big)) big = 1.0;
        const double inv = 1.0 / big;
        for (std::size_t p = J.ptr[i]; p < J.ptr[i + 1]; ++p) J.val[p] *= inv;
        R[i] *= inv;
        rowscale[i] = inv;
    }
}

void Engine::build_pattern() {
    J.n = Nu;
    J.ptr.assign(Nu + 1, 0);
    for (std::size_t row : trow) ++J.ptr[row + 1];
    for (std::size_t i = 0; i < Nu; ++i) J.ptr[i + 1] += J.ptr[i];
    std::vector<int> cols(trow.size());
    std::vector<double> vals(trow.size());
    std::vector<std::size_t> fill(J.ptr.begin(), J.ptr.end() - 1);
    for (std::size_t k = 0; k < trow.size(); ++k) {
        const std::size_t at = fill[trow[k]]++;
        cols[at] = tcol[k];
        vals[at] = tval[k];
    }
    J.col.clear();
    J.val.clear();
    std::vector<std::size_t> ptr(Nu + 1, 0);
    std::vector<std::pair<int, double>> rowbuf;
    J.col.reserve(trow.size() + Nu);
    J.val.reserve(trow.size() + Nu);
    for (std::size_t i = 0; i < Nu; ++i) {
        rowbuf.clear();
        bool has_diag = false;
        for (std::size_t p = J.ptr[i]; p < J.ptr[i + 1]; ++p) {
            rowbuf.push_back({ cols[p], vals[p] });
            if (static_cast<std::size_t>(cols[p]) == i) has_diag = true;
        }
        if (!has_diag) rowbuf.push_back({ static_cast<int>(i), 0.0 });
        std::sort(rowbuf.begin(), rowbuf.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
        for (std::size_t k = 0; k < rowbuf.size(); ++k) {
            if (J.col.size() > ptr[i] && J.col.back() == rowbuf[k].first) J.val.back() += rowbuf[k].second;
            else {
                J.col.push_back(rowbuf[k].first);
                J.val.push_back(rowbuf[k].second);
            }
        }
        ptr[i + 1] = J.col.size();
    }
    J.ptr = ptr;
    pat_pos.resize(trow.size());
    for (std::size_t k = 0; k < trow.size(); ++k) {
        const auto first = J.col.begin() + static_cast<long>(J.ptr[trow[k]]);
        const auto last = J.col.begin() + static_cast<long>(J.ptr[trow[k] + 1]);
        pat_pos[k] = static_cast<std::size_t>(std::lower_bound(first, last, tcol[k]) - J.col.begin());
    }
    pat_row = trow;
    pat_col = tcol;
}

bool Engine::linear_solve(int key, std::vector<double>& rhs, bool rebuild) {
    const std::pair<int, long long> ck{ key, cache_dt };
    std::unique_ptr<Linear>& slot = cache[ck];
    if (!slot) slot = std::make_unique<Linear>();
    Linear& lin = *slot;
    if (rebuild || !lin.ready) {
        lin.a = J;
        lin.rowscale = rowscale;
        const double cost = Banded::cost(J);
        const double budget = constant ? 6e8 : 3e7;
        lin.banded = cost <= budget;
        if (lin.banded) {
            if (!lin.lu.factor(J)) {
                lin.banded = false;
            }
        }
        if (!lin.banded) lin.ilu.factor(J);
        lin.ready = true;
        solver_name = lin.banded ? "banded LU" : "GMRES with ILU(0)";
    }
    if (lin.banded) {
        lin.lu.solve(rhs);
        for (double v : rhs) {
            if (!std::isfinite(v)) return false;
        }
        return true;
    }
    std::vector<double> x;
    bool ok = false;
    lin_iters += gmres(lin.a, lin.ilu, rhs, x, 1e-11, 1500, ok);
    if (!rebuild && !ok) return false;
    rhs = x;
    for (double v : rhs) {
        if (!std::isfinite(v)) return false;
    }
    return ok;
}

bool Engine::newton(double t, bool initial, int& iters) {
    const int maxit = P.steady ? 60 : 14;
    iters = 0;
    double last = INFINITY;
    for (int it = 0; it < maxit; ++it) {
        if (build_cancelled()) return false;
        Linear* cached = nullptr;
        auto found = cache.find({ cache_key, cache_dt });
        if (found != cache.end() && found->second && found->second->ready) cached = found->second.get();
        const bool reuse = constant && cached != nullptr && cached->rowscale.size() == Nu;
        assemble(t, initial, !reuse);
        if (reuse) {
            for (std::size_t i = 0; i < Nu; ++i) R[i] *= cached->rowscale[i];
        }
        double rn = 0.0;
        for (double v : R) {
            if (!std::isfinite(v)) return false;
            rn = std::max(rn, std::fabs(v));
        }
        if (it > 0 && rn <= 1e-12) return true;
        std::vector<double> rhs(R.size());
        for (std::size_t i = 0; i < R.size(); ++i) rhs[i] = -R[i];
        if (!linear_solve(cache_key, rhs, !reuse)) {
            if (reuse) {
                assemble(t, initial, true);
                rhs.assign(R.size(), 0.0);
                for (std::size_t i = 0; i < R.size(); ++i) rhs[i] = -R[i];
                if (!linear_solve(cache_key, rhs, true)) return false;
            } else {
                return false;
            }
        }
        ++iters;
        double step = 1.0;
        double dn = 0.0, an = 0.0;
        for (int f = 0; f < nf; ++f) {
            for (std::size_t e = 0; e < Next; ++e) {
                dn = std::max(dn, std::fabs(rhs[e * static_cast<std::size_t>(nf) + static_cast<std::size_t>(f)]));
                an = std::max(an, std::fabs(A[static_cast<std::size_t>(f)][e]));
            }
        }
        std::vector<std::vector<double>> before = A;
        for (int tries = 0; tries < (P.steady && !linear ? 10 : 1); ++tries) {
            for (int f = 0; f < nf; ++f) {
                for (std::size_t e = 0; e < Next; ++e) A[static_cast<std::size_t>(f)][e] = before[static_cast<std::size_t>(f)][e] + step * rhs[e * static_cast<std::size_t>(nf) + static_cast<std::size_t>(f)];
            }
            if (!(P.steady && !linear)) break;
            assemble(t, initial, false);
            for (std::size_t i = 0; i < Nu && rowscale.size() == Nu; ++i) R[i] *= rowscale[i];
            double tn = 0.0;
            bool bad = false;
            for (double v : R) {
                if (!std::isfinite(v)) bad = true;
                tn = std::max(tn, std::fabs(v));
            }
            if (!bad && tn < rn * (1.0 - 1e-4 * step)) break;
            step *= 0.5;
        }
        if (linear) return true;
        if (dn * step <= 1e-10 * (1.0 + an)) return true;
        if (it > 6 && rn > last * 0.999 && P.steady) {
            if (rn < 1e-8) return true;
        }
        last = rn;
    }
    stalled = true;
    return false;
}

bool Engine::advance(double t, double dt, int mode, int depth) {
    std::vector<std::vector<std::vector<double>>> s_keep = S;
    std::vector<std::vector<std::vector<double>>> sp_keep = Sprev;
    std::vector<std::vector<double>> a_keep = A;
    set_affine(mode, dt);
    cache_key = mode;
    cache_dt = static_cast<long long>(std::llround(dt * 1e12));
    int iters = 0;
    const bool ok = newton(t + dt, false, iters);
    if (ok) {
        newton_total += iters;
        newton_max = std::max(newton_max, iters);
        for (int f = 0; f < nf; ++f) {
            const std::size_t fi = static_cast<std::size_t>(f);
            const int k = P.korder[fi];
            Sprev[fi] = S[fi];
            for (int j = 0; j < k; ++j) {
                const std::size_t js = static_cast<std::size_t>(j);
                const double c = cc[fi][js];
                for (std::size_t e = 0; e < Next; ++e) S[fi][js][e] = K[fi][js][e] + c * A[fi][e];
            }
        }
        return true;
    }
    if (build_cancelled()) return false;
    S = s_keep;
    Sprev = sp_keep;
    A = a_keep;
    if (depth >= 6) return false;
    substepped = true;
    if (!advance(t, 0.5 * dt, 2, depth + 1)) return false;
    if (!advance(t + 0.5 * dt, 0.5 * dt, 2, depth + 1)) return false;
    return true;
}

void Engine::record(PdeSolution& out, double t) {
    std::vector<double> frame(Nreal * static_cast<std::size_t>(nf));
    for (int f = 0; f < nf; ++f) {
        for (std::size_t r = 0; r < Nreal; ++r) frame[static_cast<std::size_t>(f) * Nreal + r] = level0(f, real_ext[r]);
    }
    out.times.push_back(t);
    out.frames.push_back(std::move(frame));
}

bool Engine::run(PdeSolution& out, std::string& err) {
    if (!setup(err)) return false;
    out.dims = P.dims;
    out.nfun = nf;
    for (int a = 0; a < 3; ++a) {
        out.n[a] = n[a];
        out.lo[a] = P.lo[a];
        out.hi[a] = P.hi[a];
        out.periodic[a] = per[a];
    }
    out.linear = linear;
    std::vector<const double*> in0 = { RX.data(), RY.data(), RZ.data(), Tt.data() };
    std::fill(Tt.begin(), Tt.end(), P.t0);
    for (int f = 0; f < nf; ++f) {
        const std::size_t fi = static_cast<std::size_t>(f);
        const int k = P.korder[fi];
        const std::vector<ExprPtr>& iv = P.init[fi];
        const int count = std::max(k, 1);
        for (int j = 0; j < count; ++j) {
            std::vector<double> vals(Nreal, 0.0);
            if (static_cast<std::size_t>(j) < iv.size() && iv[static_cast<std::size_t>(j)]) {
                Func fi_fn(iv[static_cast<std::size_t>(j)], { "x", "y", "z", "t" }, false, NumberType::Hardware);
                if (!fi_fn.ok()) {
                    err = fi_fn.error();
                    return false;
                }
                fi_fn.eval(Nreal, in0, vals.data());
            }
            for (std::size_t r = 0; r < Nreal; ++r) {
                if (!std::isfinite(vals[r])) {
                    err = "the starting values aren't defined everywhere on the grid (at " + number_text(RX[r]) + (P.dims > 1 ? ", " + number_text(RY[r]) : std::string()) + (P.dims > 2 ? ", " + number_text(RZ[r]) : std::string()) + ")";
                    return false;
                }
                if (k == 0) A[fi][real_ext[r]] = vals[r];
                else S[fi][static_cast<std::size_t>(j)][real_ext[r]] = vals[r];
            }
        }
    }
    fill_ghost_states();
    Sprev = S;
    double initial_max = 0.0;
    for (int f = 0; f < nf; ++f) {
        for (std::size_t r = 0; r < Nreal; ++r) initial_max = std::max(initial_max, std::fabs(level0(f, real_ext[r])));
    }
    if (P.steady) {
        set_affine(2, 1.0);
        cache_key = 7;
        cache_dt = 0;
        int iters = 0;
        const bool ok = newton(0.0, false, iters);
        newton_total = iters;
        newton_max = iters;
        out.solver = solver_name;
        out.newton = iters;
        out.newton_max = iters;
        out.linear_iterations = lin_iters;
        assemble(0.0, false, false);
        for (std::size_t i = 0; i < Nu && rowscale.size() == Nu; ++i) R[i] *= rowscale[i];
        double rn = 0.0;
        for (double v : R) rn = std::max(rn, std::fabs(v));
        out.residual = rn;
        if (!ok) {
            if (build_cancelled()) return false;
            err = linear ? "the linear system for this problem has no unique solution; check the edge conditions" : "Newton's method didn't settle on a solution; try a different starting guess";
            if (rn > 1e-3 || linear) return false;
        }
        record(out, 0.0);
        out.steps = 1;
        return true;
    }
    int mode = P.method == PdeMethod::Trapezoid ? 0 : P.method == PdeMethod::Bdf2 ? 1 : 2;
    {
        set_affine(-1, 0.0);
        cache_key = -1;
        cache_dt = 0;
        int iters = 0;
        if (!newton(P.t0, true, iters)) {
            if (build_cancelled()) return false;
            if (mode == 0) mode = 1;
            for (int f = 0; f < nf; ++f) {
                if (P.korder[static_cast<std::size_t>(f)] >= 1) std::fill(A[static_cast<std::size_t>(f)].begin(), A[static_cast<std::size_t>(f)].end(), 0.0);
            }
        }
    }
    record(out, P.t0);
    const int frames = std::max(2, P.frames);
    const int per_frame = std::max(1, (P.steps + frames - 2) / (frames - 1));
    const int total = per_frame * (frames - 1);
    const double dt = (P.t1 - P.t0) / total;
    double t = P.t0;
    history = false;
    for (int step = 1; step <= total; ++step) {
        if (build_cancelled()) {
            out.timed_out = item_timed_out();
            out.stopped = t;
            if (!out.timed_out) return false;
            break;
        }
        const int use = (mode == 1 && !history) ? 2 : mode;
        substepped = false;
        if (!advance(t, dt, use, 0)) {
            if (build_cancelled() && !item_timed_out()) return false;
            out.stopped = t;
            out.timed_out = item_timed_out();
            if (!out.timed_out) out.problem = stalled ? "Newton's method stopped converging" : "the linear solver failed";
            break;
        }
        history = !substepped;
        t = P.t0 + dt * step;
        out.steps = step;
        double big = 0.0;
        bool finite = true;
        for (int f = 0; f < nf; ++f) {
            for (std::size_t r = 0; r < Nreal; ++r) {
                const double v = level0(f, real_ext[r]);
                if (!std::isfinite(v)) finite = false;
                else big = std::max(big, std::fabs(v));
            }
        }
        if (!finite || big > 1e8 * (1.0 + initial_max)) {
            out.blew_up = true;
            out.stopped = t;
            break;
        }
        if (step % per_frame == 0) record(out, t);
    }
    out.solver = solver_name;
    out.newton = newton_total;
    out.newton_max = newton_max;
    out.linear_iterations = lin_iters;
    return true;
}

} // namespace

bool solve_pde(const PdeProblem& p, PdeSolution& out, std::string& error) {
    Engine e(p);
    return e.run(out, error);
}

} // namespace build
} // namespace arbor
