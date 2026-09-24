#pragma once
#include "resources/data_table.hpp"
#include <string>
#include <vector>

namespace d2x {
struct HirelingDefinition {
    int sourceRow = -1;
    int classId = -1, seller = -1, act = 0, difficulty = 0;
    int level = 0, life = 0, attackRating = 0, damageMin = 0, damageMax = 0;
    std::string subtype, nameFirst, nameLast;
};
std::vector<HirelingDefinition> loadHirelingDefinitions(const DataTable &table);
} // namespace d2x
