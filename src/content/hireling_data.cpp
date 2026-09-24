#include "hireling_data.hpp"
#include <stdexcept>

namespace d2x {
std::vector<HirelingDefinition> loadHirelingDefinitions(const DataTable &table) {
    for (auto column : {"Class", "Seller", "Act", "Difficulty", "Level", "HP", "SubType",
                        "NameFirst", "NameLast", "AR", "Dmg-Min", "Dmg-Max"})
        if (!table.has(column)) throw std::runtime_error("Original Hireling table lacks a required column");
    std::vector<HirelingDefinition> result;
    for (size_t row = 0; row < table.rows().size(); ++row) {
        auto actor = table.number(row, "Class");
        auto seller = table.number(row, "Seller");
        auto act = table.number(row, "Act");
        auto difficulty = table.number(row, "Difficulty");
        auto level = table.number(row, "Level");
        auto life = table.number(row, "HP");
        auto rating = table.number(row, "AR");
        auto minimum = table.number(row, "Dmg-Min");
        auto maximum = table.number(row, "Dmg-Max");
        if (!actor || !seller || !act || !difficulty || !level || !life ||
            !rating || !minimum || !maximum || *level <= 0 || *life <= 0 ||
            *minimum < 0 || *maximum < *minimum || table.value(row, "SubType").empty()) continue;
        result.push_back({int(row), *actor, *seller, *act, *difficulty, *level, *life,
                          *rating, *minimum, *maximum,
                          std::string(table.value(row, "SubType")),
                          std::string(table.value(row, "NameFirst")),
                          std::string(table.value(row, "NameLast"))});
    }
    return result;
}
} // namespace d2x
