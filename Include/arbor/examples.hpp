#ifndef ARBOR_EXAMPLES_HPP
#define ARBOR_EXAMPLES_HPP

#include "graph.hpp"

#include <string>
#include <vector>

namespace arbor {

struct Example {
    std::string id;
    std::string section;
    std::string title;
    std::string description;
};

const std::vector<Example>& examples();
bool make_example(const std::string& id, Graph& out);

} // namespace arbor

#endif // ARBOR_EXAMPLES_HPP
