#include "arbor/expr.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numeric>
#include <string>
#include <vector>

namespace arbor {

namespace {

struct Ratio {
    std::int64_t p = 1;
    std::int64_t q = 1;
    bool ok = true;

    static Ratio of(std::int64_t p, std::int64_t q = 1) {
        Ratio r;
        r.p = p;
        r.q = q;
        r.reduce();
        return r;
    }

    void reduce() {
        if (q == 0) {
            ok = false;
            return;
        }
        if (q < 0) {
            p = -p;
            q = -q;
        }
        const std::int64_t g = std::gcd(p < 0 ? -p : p, q);
        if (g > 1) {
            p /= g;
            q /= g;
        }
        const std::int64_t limit = 1000000000LL;
        if (p > limit || p < -limit || q > limit) ok = false;
    }

    Ratio operator*(const Ratio& o) const {
        Ratio r = of(p * o.p, q * o.q);
        r.ok = r.ok && ok && o.ok;
        return r;
    }
    Ratio operator+(const Ratio& o) const {
        Ratio r = of(p * o.q + o.p * q, q * o.q);
        r.ok = r.ok && ok && o.ok;
        return r;
    }
    Ratio inverse() const {
        Ratio r = of(q, p);
        r.ok = r.ok && ok && p != 0;
        return r;
    }
    bool zero() const { return p == 0; }
    bool one() const { return p == 1 && q == 1; }
};

bool small_integer(const ExprPtr& e, std::int64_t& out) {
    if (!e || e->op != Op::Number) return false;
    if (!e->text.empty() && e->text.find_first_not_of("0123456789") != std::string::npos) return false;
    const double v = e->value;
    if (!std::isfinite(v) || std::floor(v) != v || std::fabs(v) > 1e9) return false;
    out = static_cast<std::int64_t>(v);
    return true;
}

struct Factor {
    ExprPtr base;
    double power = 1.0;
};

struct Term {
    Ratio coef;
    std::map<std::string, Factor> factors;
    bool ok = true;

    std::string signature() const {
        std::string s;
        for (const auto& [k, f] : factors) s += k + "^" + std::to_string(f.power) + ";";
        return s;
    }
};

ExprPtr tidy_inner(const ExprPtr& e);

ExprPtr make_product(const ExprPtr& a, const ExprPtr& b) {
    auto m = std::make_shared<Expr>();
    m->op = Op::Mul;
    m->args = { a, b };
    return m;
}

ExprPtr make_quotient(const ExprPtr& a, const ExprPtr& b) {
    auto m = std::make_shared<Expr>();
    m->op = Op::Div;
    m->args = { a, b };
    return m;
}

void put_factor(Term& t, const ExprPtr& base, double power) {
    if (power == 0.0) return;
    const std::string key = to_string(base);
    Factor& f = t.factors[key];
    if (!f.base) {
        f.base = base;
        f.power = power;
    } else {
        f.power += power;
    }
    if (f.power == 0.0) t.factors.erase(key);
}

void collect_product(const ExprPtr& e, Term& t, double power) {
    std::int64_t n = 0;
    switch (e->op) {
        case Op::Mul:
            collect_product(e->args[0], t, power);
            collect_product(e->args[1], t, power);
            return;
        case Op::Div:
            collect_product(e->args[0], t, power);
            collect_product(e->args[1], t, -power);
            return;
        case Op::Neg:
            if (std::floor(power) != power) {
                put_factor(t, e, power);
                return;
            }
            if (std::fmod(std::fabs(power), 2.0) == 1.0) t.coef = t.coef * Ratio::of(-1);
            collect_product(e->args[0], t, power);
            return;
        case Op::Number:
            if (small_integer(e, n) && std::floor(power) == power) {
                Ratio r = Ratio::of(n);
                if (n == 0) {
                    if (power > 0) t.coef = Ratio::of(0);
                    else t.ok = false;
                    return;
                }
                const Ratio base = power > 0 ? r : r.inverse();
                for (int k = 0; k < static_cast<int>(std::fabs(power)); ++k) t.coef = t.coef * base;
                return;
            }
            put_factor(t, e, power);
            return;
        case Op::Pow: {
            const ExprPtr& ex = e->args[1];
            if (ex->op == Op::Number && ex->text.empty() && std::isfinite(ex->value)) {
                const ExprPtr inner = e->args[0];
                if ((inner->op == Op::Mul || inner->op == Op::Div || inner->op == Op::Neg) && std::floor(ex->value) == ex->value) {
                    collect_product(inner, t, power * ex->value);
                    return;
                }
                if (inner->op == Op::Number) {
                    std::int64_t m = 0;
                    if (small_integer(inner, m) && std::floor(ex->value * power) == ex->value * power && std::fabs(ex->value * power) <= 16) {
                        collect_product(inner, t, power * ex->value);
                        return;
                    }
                }
                put_factor(t, inner, power * ex->value);
                return;
            }
            put_factor(t, e, power);
            return;
        }
        default:
            put_factor(t, e, power);
            return;
    }
}

struct Signed {
    bool negative = false;
    ExprPtr e;
};

std::vector<Signed> terms_of(const ExprPtr& e, std::size_t budget) {
    switch (e->op) {
        case Op::Add:
        case Op::Sub: {
            std::vector<Signed> a = terms_of(e->args[0], budget);
            std::vector<Signed> b = terms_of(e->args[1], budget);
            if (e->op == Op::Sub) {
                for (Signed& t : b) t.negative = !t.negative;
            }
            a.insert(a.end(), b.begin(), b.end());
            return a;
        }
        case Op::Neg: {
            std::vector<Signed> a = terms_of(e->args[0], budget);
            for (Signed& t : a) t.negative = !t.negative;
            return a;
        }
        case Op::Mul: {
            std::vector<Signed> a = terms_of(e->args[0], budget);
            std::vector<Signed> b = terms_of(e->args[1], budget);
            if (a.size() * b.size() > budget) return { Signed{ false, e } };
            std::vector<Signed> out;
            for (const Signed& x : a) {
                for (const Signed& y : b) out.push_back(Signed{ x.negative != y.negative, make_product(x.e, y.e) });
            }
            return out;
        }
        case Op::Div: {
            std::vector<Signed> a = terms_of(e->args[0], budget);
            for (Signed& t : a) t.e = make_quotient(t.e, e->args[1]);
            return a;
        }
        default:
            return { Signed{ false, e } };
    }
}

void collect_sum(const ExprPtr& e, bool negative, std::vector<Term>& out) {
    for (const Signed& s : terms_of(e, 256)) {
        Term t;
        t.coef = Ratio::of(negative != s.negative ? -1 : 1);
        collect_product(s.e, t, 1.0);
        auto it = t.factors.find("i");
        if (it != t.factors.end() && it->second.base->op == Op::Imag && std::floor(it->second.power) == it->second.power) {
            long long k = static_cast<long long>(it->second.power) % 4;
            if (k < 0) k += 4;
            t.factors.erase(it);
            if (k >= 2) t.coef = t.coef * Ratio::of(-1);
            if (k % 2 == 1) t.factors["i"] = Factor{ imag_unit(), 1.0 };
        }
        out.push_back(t);
    }
}

bool trig_power(const Factor& f, const char* fn, std::string& arg_key) {
    if (f.power < 2.0 || std::floor(f.power) != f.power || !f.base || f.base->op != Op::Call || f.base->text != fn || f.base->args.size() != 1) return false;
    arg_key = to_string(f.base->args[0]);
    return true;
}

Term lowered(const Term& t, const std::string& key) {
    Term out = t;
    auto it = out.factors.find(key);
    it->second.power -= 2.0;
    if (it->second.power == 0.0) out.factors.erase(it);
    return out;
}

bool merge_pythagoras(std::vector<Term>& terms) {
    for (std::size_t i = 0; i < terms.size(); ++i) {
        for (const auto& [key, f] : terms[i].factors) {
            std::string w;
            if (!trig_power(f, "sin", w)) continue;
            const Term a = lowered(terms[i], key);
            for (std::size_t j = 0; j < terms.size(); ++j) {
                if (j == i || terms[j].coef.p != terms[i].coef.p || terms[j].coef.q != terms[i].coef.q) continue;
                for (const auto& [k2, f2] : terms[j].factors) {
                    std::string w2;
                    if (!trig_power(f2, "cos", w2) || w2 != w) continue;
                    const Term b = lowered(terms[j], k2);
                    if (a.signature() != b.signature()) continue;
                    terms[i] = a;
                    terms.erase(terms.begin() + static_cast<long>(j));
                    return true;
                }
            }
        }
    }
    return false;
}

ExprPtr power_of(const Factor& f) {
    const double p = std::fabs(f.power);
    if (p == 1.0) return f.base;
    if (p == 0.5) return call("sqrt", { f.base });
    return pow(f.base, number(p));
}

ExprPtr build_term(const Term& t, bool& negative) {
    negative = t.coef.p < 0;
    const std::int64_t p = negative ? -t.coef.p : t.coef.p;
    const std::int64_t q = t.coef.q;
    ExprPtr top;
    ExprPtr bottom;
    auto times = [](ExprPtr acc, ExprPtr x) { return acc ? mul(acc, x) : x; };
    if (p != 1) top = number(static_cast<double>(p));
    if (q != 1) bottom = number(static_cast<double>(q));
    std::vector<const Factor*> order;
    for (const auto& [k, f] : t.factors) order.push_back(&f);
    std::stable_sort(order.begin(), order.end(), [](const Factor* a, const Factor* b) {
        const int ra = a->base->op == Op::Number ? 0 : a->base->op == Op::Variable || a->base->op == Op::Imag ? 1 : 2;
        const int rb = b->base->op == Op::Number ? 0 : b->base->op == Op::Variable || b->base->op == Op::Imag ? 1 : 2;
        return ra < rb;
    });
    for (const Factor* f : order) {
        if (f->power > 0) top = times(top, power_of(*f));
        else bottom = times(bottom, power_of(*f));
    }
    if (!top) top = number(1.0);
    return bottom ? div(top, bottom) : top;
}

ExprPtr tidy_sum(const ExprPtr& e) {
    std::vector<Term> raw;
    collect_sum(e, false, raw);
    for (const Term& t : raw) {
        if (!t.ok || !t.coef.ok) return e;
    }
    std::vector<Term> terms;
    std::map<std::string, std::size_t> where;
    for (const Term& t : raw) {
        if (t.coef.zero()) continue;
        const std::string sig = t.signature();
        auto it = where.find(sig);
        if (it == where.end()) {
            where[sig] = terms.size();
            terms.push_back(t);
        } else {
            terms[it->second].coef = terms[it->second].coef + t.coef;
            if (!terms[it->second].coef.ok) return e;
        }
    }
    terms.erase(std::remove_if(terms.begin(), terms.end(), [](const Term& t) { return t.coef.zero(); }), terms.end());
    while (merge_pythagoras(terms)) {
    }
    if (terms.empty()) return number(0.0);
    std::stable_sort(terms.begin(), terms.end(), [](const Term& a, const Term& b) { return !a.factors.empty() && b.factors.empty(); });
    auto positive = std::find_if(terms.begin(), terms.end(), [](const Term& t) { return t.coef.p > 0; });
    if (positive != terms.end() && positive != terms.begin()) std::rotate(terms.begin(), positive, positive + 1);
    ExprPtr out;
    for (const Term& t : terms) {
        bool negative = false;
        ExprPtr x = build_term(t, negative);
        if (!out) out = negative ? neg(x) : x;
        else out = negative ? sub(out, x) : add(out, x);
    }
    return out;
}

ExprPtr tidy_inner(const ExprPtr& e) {
    if (!e) return e;
    if (e->args.empty()) return e;
    std::vector<ExprPtr> args;
    for (const ExprPtr& a : e->args) args.push_back(tidy_inner(a));
    auto rebuilt = std::make_shared<Expr>(*e);
    rebuilt->args = args;
    ExprPtr r = simplify(rebuilt);
    switch (r->op) {
        case Op::Add:
        case Op::Sub:
        case Op::Mul:
        case Op::Div:
        case Op::Neg:
        case Op::Pow:
            return tidy_sum(r);
        default:
            return r;
    }
}

} // namespace

ExprPtr tidy(const ExprPtr& e) {
    ExprPtr a = tidy_inner(e);
    ExprPtr b = tidy_inner(a);
    return to_string(b).size() <= to_string(e).size() + 8 ? b : e;
}

} // namespace arbor
