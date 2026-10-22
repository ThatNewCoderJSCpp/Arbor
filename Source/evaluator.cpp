#include "eval_impl.hpp"

namespace arbor {

std::unique_ptr<Evaluator> make_hardware_evaluator(const Program& p) {
    return std::make_unique<detail::GenericEvaluator<detail::DoubleNum>>(p);
}

} // namespace arbor
