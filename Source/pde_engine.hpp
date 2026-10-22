#ifndef ARBOR_PDE_ENGINE_HPP
#define ARBOR_PDE_ENGINE_HPP

#include "build_common.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace arbor {
namespace build {

struct PdeTerm {
    int fun = 0;
    int ord[3] = { 0, 0, 0 };
    int tord = 0;
    std::string name;
};

std::string pde_term_name(int fun, const int ord[3], int tord);
bool pde_parse_term(const std::string& name, const std::vector<std::string>& funs, const std::string& letters, int& fun, int counts[4]);

enum class PdeEdgeMode { Conditions, Periodic, Insulated, Free };

struct PdeCondition {
    ExprPtr residual;
    std::string text;
    int fun = 0;
    bool value = false;
    int reach = 0;
};

struct PdeEdge {
    PdeEdgeMode mode = PdeEdgeMode::Conditions;
    std::vector<PdeCondition> conds;
};

enum class PdeMethod { Trapezoid, Bdf2, Euler };

struct PdeProblem {
    int dims = 1;
    int nfun = 1;
    bool steady = false;
    std::vector<ExprPtr> eqs;
    std::vector<PdeTerm> terms;
    std::vector<int> korder;
    double lo[3] = { 0.0, 0.0, 0.0 };
    double hi[3] = { 1.0, 1.0, 1.0 };
    int n[3] = { 1, 1, 1 };
    PdeEdge edge[3][2];
    std::vector<std::vector<ExprPtr>> init;
    double t0 = 0.0;
    double t1 = 1.0;
    int steps = 100;
    int frames = 2;
    PdeMethod method = PdeMethod::Trapezoid;
};

struct PdeSolution {
    int dims = 1;
    int nfun = 1;
    int n[3] = { 1, 1, 1 };
    double lo[3] = { 0.0, 0.0, 0.0 };
    double hi[3] = { 1.0, 1.0, 1.0 };
    bool periodic[3] = { false, false, false };
    std::vector<double> times;
    std::vector<std::vector<double>> frames;
    std::string problem;
    double stopped = -1.0;
    bool blew_up = false;
    bool timed_out = false;
    int steps = 0;
    int newton = 0;
    int newton_max = 0;
    long long linear_iterations = 0;
    bool linear = false;
    std::string solver;
    double residual = 0.0;

    std::size_t points() const { return static_cast<std::size_t>(n[0]) * n[1] * n[2]; }
    double coord(int axis, int i) const;
    double value(int frame, int fun, int i, int j, int k) const;
};

bool solve_pde(const PdeProblem& p, PdeSolution& out, std::string& error);

} // namespace build
} // namespace arbor

#endif // ARBOR_PDE_ENGINE_HPP
