#include "fizmo_num.hpp"

namespace arbor {

std::unique_ptr<Evaluator> make_f1024_evaluator(const Program& p) {
    return std::make_unique<detail::GenericEvaluator<detail::FizmoNum<detail::mp::float1024>>>(p);
}

} // namespace arbor
