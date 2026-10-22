#include "fizmo_num.hpp"

namespace arbor {

std::unique_ptr<Evaluator> make_f128_evaluator(const Program& p) {
    return std::make_unique<detail::GenericEvaluator<detail::FizmoNum<detail::mp::float128>>>(p);
}

} // namespace arbor
