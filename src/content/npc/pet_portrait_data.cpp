#include "pet_portrait_data.hpp"
#include "content/string_table.hpp"
#include "resources/data_table.hpp"
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace d2x {
const std::string &PetPortraitDefinition::art(int monsterClass) const {
    const auto found = classArt.find(monsterClass);
    return found == classArt.end() ? baseArt : found->second;
}
std::map<int, PetPortraitDefinition> loadPetPortraitData(const DataTable &table, const ClassicStrings &strings) {
    std::map<int, PetPortraitDefinition> result;
    for (size_t row = 0; row < table.rows().size(); ++row) {
        const auto type = table.number(row, "idx");
        if (!type || *type < 0 || *type > 255) continue;
        PetPortraitDefinition entry;
        entry.hireling = table.value(row, "pet type") == "hireable";
        entry.iconType = table.number(row, "icontype").value_or(0);
        entry.drawLife = table.number(row, "drawhp").value_or(0) != 0;
        entry.name = strings.find(table.value(row, "name"));
        const auto path = [](std::string_view token) {
            return token.empty() ? std::string{} : "data/global/ui/hireables/" + std::string(token) + ".dc6";
        };
        entry.baseArt = path(table.value(row, "baseicon"));
        for (int variant = 1; variant <= 4; ++variant) {
            const auto suffix = std::to_string(variant);
            const auto monsterClass = table.number(row, "mclass" + suffix);
            auto art = path(table.value(row, "micon" + suffix));
            if (monsterClass && !art.empty()) entry.classArt.emplace(*monsterClass, std::move(art));
        }
        if (entry.iconType < 0 || entry.iconType > 3)
            throw std::runtime_error("Unsupported original PetType portrait mode");
        result.emplace(*type, std::move(entry));
    }
    return result;
}
} // namespace d2x
