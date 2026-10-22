#include "fizmo_num.hpp"

namespace arbor {

std::unique_ptr<Evaluator> make_f32_evaluator(const Program& p);
std::unique_ptr<Evaluator> make_f64_evaluator(const Program& p);
std::unique_ptr<Evaluator> make_f128_evaluator(const Program& p);
std::unique_ptr<Evaluator> make_f256_evaluator(const Program& p);
std::unique_ptr<Evaluator> make_f512_evaluator(const Program& p);
std::unique_ptr<Evaluator> make_f1024_evaluator(const Program& p);
std::unique_ptr<Evaluator> make_f2048_evaluator(const Program& p);

namespace detail {

double fizmo_special_double(Fn fn, const double* args, int n) {
    using N = FizmoNum<mp::float64>;
    N::T in[4];
    for (int k = 0; k < n && k < 4; ++k) in[k] = N::from_double(args[k]);
    return N::to_double(N::special(fn, in, n));
}

} // namespace detail

std::unique_ptr<Evaluator> make_fizmo_evaluator(const Program& p, NumberType type) {
    switch (type) {
        case NumberType::ShortShortFloat: return make_f32_evaluator(p);
        case NumberType::ShortFloat: return make_f64_evaluator(p);
        case NumberType::Float: return make_f128_evaluator(p);
        case NumberType::LongFloat: return make_f256_evaluator(p);
        case NumberType::LongLongFloat: return make_f512_evaluator(p);
        case NumberType::LongDouble: return make_f1024_evaluator(p);
        case NumberType::LongLongDouble: return make_f2048_evaluator(p);
        default: return make_hardware_evaluator(p);
    }
}

} // namespace arbor
