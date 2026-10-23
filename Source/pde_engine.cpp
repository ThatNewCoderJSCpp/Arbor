#include "pde_engine.hpp"

#ifndef FIZMO
#define FIZMO
#endif
#ifndef FIZMO_NUMERICS
#define FIZMO_NUMERICS
#endif
#include <fizmo/includes.hpp>

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

namespace num = fizmo::numerics;

num::PdeField field_of(const std::shared_ptr<Func>& f) {
    return [f](const num::PdeInputs& in, double* out) { f->eval(in.count, in.values, out); };
}

bool compile_equation(const ExprPtr& e, const PdeProblem& p, const std::vector<std::string>& vars, num::PdeEquation& out, std::string& err) {
    auto base = std::make_shared<Func>(e, vars, false, NumberType::Hardware);
    if (!base->ok()) {
        err = base->error();
        return false;
    }
    out.residual = field_of(base);
    out.linear = true;
    out.constant = true;
    for (std::size_t k = 0; k < p.terms.size(); ++k) {
        if (!depends_on(e, p.terms[k].name)) continue;
        out.terms.push_back(static_cast<int>(k));
        const ExprPtr de = d(e, p.terms[k].name);
        std::shared_ptr<Func> fd;
        if (de) {
            fd = std::make_shared<Func>(simplify(de), vars, false, NumberType::Hardware);
            if (!fd->ok()) fd.reset();
        }
        if (!fd) {
            out.linear = false;
            out.constant = false;
            continue;
        }
        for (const PdeTerm& t : p.terms) {
            if (depends_on(fd->expr(), t.name)) out.linear = false;
        }
        if (depends_on(fd->expr(), "t")) out.constant = false;
        out.partials.push_back({ static_cast<int>(k), field_of(fd) });
    }
    out.constant = out.constant && out.linear;
    return true;
}

} // namespace

bool solve_pde(const PdeProblem& p, PdeSolution& out, std::string& error) {
    std::vector<std::string> vars = { "x", "y", "z", "t" };
    for (const PdeTerm& t : p.terms) vars.push_back(t.name);
    num::PdeProblem q;
    q.dims = p.dims;
    q.functions = p.nfun;
    q.steady = p.steady;
    for (const PdeTerm& t : p.terms) q.terms.push_back(num::PdeTerm(t.fun, t.ord[0], t.ord[1], t.ord[2], t.tord));
    for (const ExprPtr& e : p.eqs) {
        num::PdeEquation eq;
        if (!compile_equation(e, p, vars, eq, error)) return false;
        if (eq.terms.empty()) {
            error = "an equation has none of the functions in it";
            return false;
        }
        q.equations.push_back(std::move(eq));
    }
    for (int a = 0; a < 3; ++a) {
        q.lo[a] = p.lo[a];
        q.hi[a] = p.hi[a];
        q.points[a] = p.n[a];
        for (int s = 0; s < 2; ++s) {
            const PdeEdge& edge = p.edge[a][s];
            num::PdeEdge& to = q.edges[a][s];
            to.mode = edge.mode == PdeEdgeMode::Periodic ? num::PdeEdgeMode::Periodic : edge.mode == PdeEdgeMode::Insulated ? num::PdeEdgeMode::Insulated : edge.mode == PdeEdgeMode::Free ? num::PdeEdgeMode::Free : num::PdeEdgeMode::Conditions;
            for (const PdeCondition& c : edge.conds) {
                num::PdeCondition nc;
                nc.function = c.fun;
                nc.label = c.text;
                if (!compile_equation(c.residual, p, vars, nc.equation, error)) return false;
                to.conditions.push_back(std::move(nc));
            }
        }
    }
    for (const auto& list : p.init) {
        std::vector<num::PdeField> fields;
        for (const ExprPtr& e : list) {
            if (!e) {
                fields.push_back(nullptr);
                continue;
            }
            auto f = std::make_shared<Func>(e, std::vector<std::string>{ "x", "y", "z", "t" }, false, NumberType::Hardware);
            if (!f->ok()) {
                error = f->error();
                return false;
            }
            fields.push_back([f](const num::PdeInputs& in, double* o) {
                std::vector<const double*> v(in.values.begin(), in.values.begin() + 4);
                f->eval(in.count, v, o);
            });
        }
        q.initial.push_back(std::move(fields));
    }
    q.t0 = p.t0;
    q.t1 = p.t1;
    q.steps = p.steps;
    q.frames = p.frames;
    q.method = p.method == PdeMethod::Trapezoid ? num::PdeMethod::Trapezoid : p.method == PdeMethod::Bdf2 ? num::PdeMethod::Bdf2 : num::PdeMethod::BackwardEuler;
    q.poll = []() {
        if (!build_cancelled()) return num::PdePoll::Continue;
        return item_timed_out() ? num::PdePoll::Finish : num::PdePoll::Abort;
    };
    num::PdeSolution s;
    if (!num::solve_pde(q, s, error)) return false;
    out.dims = s.dims;
    out.nfun = s.functions;
    for (int a = 0; a < 3; ++a) {
        out.n[a] = s.n[a];
        out.lo[a] = s.lo[a];
        out.hi[a] = s.hi[a];
        out.periodic[a] = s.periodic[a];
    }
    out.times = std::move(s.times);
    out.frames = std::move(s.frames);
    out.problem = s.problem;
    out.stopped = s.stopped;
    out.blew_up = s.blew_up;
    out.timed_out = s.finished_early;
    out.steps = s.steps;
    out.newton = s.newton;
    out.newton_max = s.newton_max;
    out.linear_iterations = s.linear_iterations;
    out.linear = s.linear;
    out.solver = s.solver;
    out.residual = s.residual;
    return true;
}

} // namespace build
} // namespace arbor
