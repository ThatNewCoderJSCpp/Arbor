#ifndef ARBOR_LIBRARY_HPP
#define ARBOR_LIBRARY_HPP

#include <string>
#include <utility>
#include <vector>

namespace arbor {

enum class LibKind {
    Function,
    Constant,
    Variable,
    Operator
};

struct LibEntry {
    LibKind kind = LibKind::Function;
    std::string name;
    std::vector<std::string> aliases;
    std::string usage;
    std::string symbol;
    std::string category;
    std::string description;
    std::vector<std::pair<std::string, std::string>> args;
    int min_args = 1;
    int max_args = 1;
    bool complex = true;
    bool complex_only = false;
    bool branched = false;
    bool needs_paren = false;
    std::string literal;
    std::string value;
};

const std::vector<LibEntry>& library();
const LibEntry* library_function(const std::string& name);
const LibEntry* library_constant(const std::string& name);
const std::vector<std::string>& library_categories();
std::string library_text();

} // namespace arbor

#endif // ARBOR_LIBRARY_HPP
