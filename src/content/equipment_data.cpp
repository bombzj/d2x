#include "equipment_data.hpp"
#include <functional>
#include <set>
#include <stdexcept>

namespace d2x {
void loadEquipmentDefinitions(std::vector<ItemDefinition> &items, const DataTable &types,
                              const std::map<std::string, DataTable, std::less<>> &tables) {
    for (auto column : {"Code", "Equiv1", "Equiv2", "Body", "BodyLoc1", "BodyLoc2", "Class", "Shoots",
                        "Quiver", "Throwable", "Repair"})
        if (!types.has(column))
            throw std::runtime_error("ItemTypes lacks equipment column: " + std::string(column));
    std::map<std::string, size_t, std::less<>> rows;
    for (size_t row = 0; row < types.rows().size(); ++row) {
        auto code = types.value(row, "Code");
        if (!code.empty() && !rows.emplace(std::string(code), row).second)
            throw std::runtime_error("Duplicate equipment item type: " + std::string(code));
    }
    for (auto &item : items) {
        auto &equipment = item.equipment;
        const auto found = rows.find(item.base.type);
        if (found == rows.end())
            throw std::runtime_error("Unknown primary item type: " + item.code);
        size_t row = found->second;
        const int graphics = types.number(row, "VarInvGfx").value_or(0);
        if (graphics < 0 || graphics > 6)
            throw std::runtime_error("Invalid original inventory graphic count: " + item.code);
        for (int index = 1; index <= graphics; ++index) {
            const auto file = types.value(row, "InvGfx" + std::to_string(index));
            if (file.empty())
                throw std::runtime_error("Missing original inventory graphic: " + item.code);
            item.inventoryIcons.push_back("data/global/items/" + std::string(file) + ".dc6");
        }
        if (types.number(row, "Body").value_or(0))
            for (auto column : {"BodyLoc1", "BodyLoc2"}) {
                auto code = types.value(row, column);
                if (code.empty())
                    continue;
                auto slot = equipmentSlotFromCode(code);
                if (!slot)
                    throw std::runtime_error("Unknown body location: " + std::string(code));
                equipment.slots[size_t(*slot)] = true;
            }
        equipment.shoots = types.value(row, "Shoots");
        equipment.quiver = types.value(row, "Quiver");
        equipment.throwable = types.number(row, "Throwable").value_or(0) != 0;
        equipment.repairable = types.number(row, "Repair").value_or(0) != 0;
        std::set<std::string, std::less<>> visiting, complete;
        std::function<void(std::string_view)> inherit = [&](std::string_view code) {
            if (code.empty() || complete.contains(code))
                return;
            auto type = rows.find(code);
            if (type == rows.end() || !visiting.emplace(code).second)
                throw std::runtime_error("Invalid ItemTypes inheritance: " + std::string(code));
            equipment.types.emplace_back(code);
            auto character = types.value(type->second, "Class");
            if (!character.empty()) {
                if (!equipment.requiredClass.empty() && equipment.requiredClass != character)
                    throw std::runtime_error("Conflicting equipment class: " + item.code);
                equipment.requiredClass = character;
            }
            inherit(types.value(type->second, "Equiv1"));
            inherit(types.value(type->second, "Equiv2"));
            visiting.erase(std::string(code));
            complete.emplace(code);
        };
        inherit(item.base.type);
        inherit(item.base.secondaryType);
        const auto &source = tables.at(item.base.sourceTable);
        equipment.twoHanded = source.number(item.base.sourceRow, "2handed").value_or(0) != 0;
        equipment.oneOrTwoHanded = source.number(item.base.sourceRow, "1or2handed").value_or(0) != 0;
        equipment.twoHandWeaponClass = source.value(item.base.sourceRow, "2handedwclass");
        equipment.known = true;
    }
}
} // namespace d2x
