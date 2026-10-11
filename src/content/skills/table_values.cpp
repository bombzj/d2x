#include "table_values.hpp"
#include "resources/data_table.hpp"
#include <stdexcept>
#include <string>

namespace d2x {
int skillTableNumber(const DataTable &table, size_t row, std::string_view field, SkillNumberPolicy policy) {
    const auto value = table.number(row,field);
    if (value) return *value;
    const bool requiresColumn = policy == SkillNumberPolicy::Required ||
        policy == SkillNumberPolicy::BlankIsZero || policy == SkillNumberPolicy::ColumnIsZero;
    const bool rejectsMalformed = policy == SkillNumberPolicy::Required ||
        policy == SkillNumberPolicy::BlankIsZero || policy == SkillNumberPolicy::OptionalBlankIsZero;
    if ((requiresColumn && !table.has(field)) || policy == SkillNumberPolicy::Required ||
        (rejectsMalformed && !table.value(row,field).empty()))
        throw std::runtime_error("Missing or invalid original skill table number: " + std::string(field) +
            " at row " + std::to_string(row));
    return 0;
}
size_t findSkillTableRow(const DataTable &table, std::string_view field, std::string_view value,
                        SkillIdentityPolicy policy) {
    if (!value.empty() || policy == SkillIdentityPolicy::AllowEmpty)
        for (size_t row = 0; row < table.rows().size(); ++row)
            if (table.value(row,field) == value) return row;
    throw std::runtime_error("Missing original skill table reference: " + std::string(field) + " = " + std::string(value));
}
std::string_view skillTableFormula(const DataTable &table, size_t row, std::string_view field) {
    auto value = table.value(row,field);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1,value.size()-2);
    return value;
}
void requireSkillTableText(const DataTable &table, size_t row, std::string_view field,
                          std::string_view expected, SkillTextPolicy policy) {
    const auto value = policy == SkillTextPolicy::Formula ? skillTableFormula(table,row,field) : table.value(row,field);
    if (value != expected)
        throw std::runtime_error("Unsupported original skill table program: " + std::string(field) +
            " = " + std::string(table.value(row,field)) + " at row " + std::to_string(row));
}
}
