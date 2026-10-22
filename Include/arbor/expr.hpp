#ifndef ARBOR_EXPR_HPP
#define ARBOR_EXPR_HPP

#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace arbor {

enum class Op {
    Number,
    Variable,
    Imag,
    Add,
    Sub,
    Mul,
    Div,
    Pow,
    Neg,
    Call,
    Vector
};

struct Expr;
using ExprPtr = std::shared_ptr<const Expr>;

struct Expr {
    Op op = Op::Number;
    double value = 0.0;
    std::string text;
    std::vector<ExprPtr> args;
};

ExprPtr number(double v);
ExprPtr number_text(const std::string& literal);
ExprPtr variable(const std::string& name);
ExprPtr imag_unit();
ExprPtr add(ExprPtr a, ExprPtr b);
ExprPtr sub(ExprPtr a, ExprPtr b);
ExprPtr mul(ExprPtr a, ExprPtr b);
ExprPtr div(ExprPtr a, ExprPtr b);
ExprPtr pow(ExprPtr a, ExprPtr b);
ExprPtr neg(ExprPtr a);
ExprPtr call(const std::string& name, std::vector<ExprPtr> args);
ExprPtr vector_of(std::vector<ExprPtr> parts);

bool is_number(const ExprPtr& e, double v);
bool is_constant(const ExprPtr& e);
bool depends_on(const ExprPtr& e, const std::string& name);
void collect_names(const ExprPtr& e, std::set<std::string>& out);
bool uses_imaginary(const ExprPtr& e);
bool uses_branches(const ExprPtr& e, bool complex);

struct FunctionInfo {
    std::string name;
    int min_args = 1;
    int max_args = 1;
    bool complex_only = false;
};

const FunctionInfo* find_function(const std::string& name);
const std::vector<FunctionInfo>& builtin_functions();
bool is_builtin_constant(const std::string& name);
std::string canonical_name(const std::string& name);

struct UserFunction {
    std::vector<std::string> params;
    ExprPtr body;
};

using FunctionTable = std::map<std::string, UserFunction>;

ExprPtr substitute(const ExprPtr& e, const std::map<std::string, ExprPtr>& with);
ExprPtr inline_functions(const ExprPtr& e, const FunctionTable& functions, std::string& error, int depth = 0);
ExprPtr simplify(const ExprPtr& e);
ExprPtr tidy(const ExprPtr& e);
ExprPtr derivative(const ExprPtr& e, const std::string& var);
std::size_t component_count(const ExprPtr& e);
ExprPtr component(const ExprPtr& e, std::size_t i);

std::string to_string(const ExprPtr& e);
std::string to_mathtext(const ExprPtr& e);

enum class Relation {
    None,
    Equal,
    Less,
    LessEqual,
    Greater,
    GreaterEqual
};

struct Statement {
    ExprPtr lhs;
    ExprPtr rhs;
    Relation relation = Relation::None;
    std::string defines;
    std::vector<std::string> params;
    bool is_function_definition = false;
    bool is_value_definition = false;
};

struct ParseOptions {
    std::set<std::string> known_names;
    std::set<std::string> function_names;
};

struct ParseResult {
    bool ok = false;
    Statement statement;
    std::string error;
    std::size_t error_at = 0;
};

ParseResult parse_statement(const std::string& text, const ParseOptions& options = ParseOptions());
ParseResult parse_expression(const std::string& text, const ParseOptions& options = ParseOptions());
ParseResult parse_condition(const std::string& text, const ParseOptions& options = ParseOptions());
ParseResult parse_statement_as_typed(const std::string& text, const ParseOptions& options = ParseOptions());
std::string display_mathtext(const ExprPtr& e);
std::string display_mathtext(const Statement& s);
const char* relation_text(Relation r);

} // namespace arbor

#endif // ARBOR_EXPR_HPP
