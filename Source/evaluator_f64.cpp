#include "fizmo_num.hpp"

namespace arbor {

std::unique_ptr<Evaluator> make_f64_evaluator(const Program& p) {
    return std::make_unique<detail::GenericEvaluator<detail::FizmoNum<detail::mp::float64>>>(p);
}

} // namespace arbor
