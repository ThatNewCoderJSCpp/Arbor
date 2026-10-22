#ifndef ARBOR_KINDS_HPP
#define ARBOR_KINDS_HPP

#include <functional>
#include <string>
#include <vector>

namespace arbor {

enum class Space {
    Plane,
    Solid
};

const char* space_id(Space s);
Space space_from_id(const std::string& id);

struct FieldSpec {
    std::string id;
    std::string label;
    std::string value;
    std::string hint;
    bool wide = false;
    std::vector<std::string> choices;
    std::string show_if;
};

struct OptionSpec {
    std::string id;
    std::string label;
    bool value = false;
    std::string hint;
    std::string show_if;
};

struct KindInfo {
    std::string id;
    std::string section;
    std::string title;
    std::string formula;
    std::string description;
    Space space = Space::Plane;
    std::vector<FieldSpec> fields;
    std::vector<OptionSpec> options;
};

struct SectionInfo {
    std::string id;
    std::string title;
    std::string description;
};

const std::vector<SectionInfo>& sections();
const std::vector<KindInfo>& kinds();
const KindInfo* find_kind(const std::string& id);
std::vector<const KindInfo*> kinds_for_space(Space space);
const std::vector<std::string>& palette();
bool shown_for(const std::string& condition, const std::function<std::string(const std::string&)>& value_of);

} // namespace arbor

#endif // ARBOR_KINDS_HPP
