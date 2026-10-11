#pragma once
#include <cstddef>
#include <string_view>

namespace d2x {
class DataTable;
// Preserve each loader's original schema policy when sharing table access.
enum class SkillNumberPolicy { Required, BlankIsZero, ColumnIsZero, OptionalZero, OptionalBlankIsZero };
enum class SkillTextPolicy { Exact, Formula };
enum class SkillIdentityPolicy { Nonempty, AllowEmpty };
int skillTableNumber(const DataTable &, size_t row, std::string_view field,
                     SkillNumberPolicy = SkillNumberPolicy::Required);
size_t findSkillTableRow(const DataTable &, std::string_view field, std::string_view value,
                        SkillIdentityPolicy = SkillIdentityPolicy::Nonempty);
std::string_view skillTableFormula(const DataTable &, size_t row, std::string_view field);
void requireSkillTableText(const DataTable &, size_t row, std::string_view field,
                          std::string_view expected, SkillTextPolicy = SkillTextPolicy::Exact);
}
