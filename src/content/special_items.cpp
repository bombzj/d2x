#include "special_items.hpp"
#include "item_properties.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
void loadSpecialItemData(ClassicData &data) {
    if (data.profile != "lod-named-txt-v1")
        return;
    auto unique = data.tables.find("uniqueitems");
    auto sets = data.tables.find("setitems");
    auto setDefinitions = data.tables.find("sets");
    if (unique != data.tables.end())
        for (auto column : {"index", "version", "enabled", "rarity", "lvl", "code"})
            if (!unique->second.has(column))
                throw std::runtime_error("UniqueItems lacks field: " + std::string(column));
    if (sets != data.tables.end())
        for (auto column : {"index", "set", "item", "rarity", "lvl"})
            if (!sets->second.has(column))
                throw std::runtime_error("SetItems lacks field: " + std::string(column));
    if (sets != data.tables.end() &&
        (setDefinitions == data.tables.end() || !setDefinitions->second.has("index")))
        throw std::runtime_error("SetItems requires original Sets definitions");
    // The original set ID 29 has a dedicated cow-only drop rule. Resolve its
    // identity through Sets.txt, so the set name remains MPQ data.
    std::string cowSet;
    if (setDefinitions != data.tables.end()) {
        size_t setId = 0;
        for (size_t row = 0; row < setDefinitions->second.rows().size(); ++row) {
            auto name = setDefinitions->second.value(row, "index");
            if (name.empty() || name == "Expansion")
                continue;
            if (setId++ == 29) {
                cowSet = name;
                break;
            }
        }
    }
    if (sets != data.tables.end() && cowSet.empty())
        throw std::runtime_error("Sets lacks original special drop set ID 29");
    const auto append = [&](const DataTable &table, bool uniqueItem,
                            std::vector<SpecialItemRecord> &destination) {
        for (size_t row = 0; row < table.rows().size(); ++row) {
            auto name = table.value(row, "index");
            auto code = table.value(row, uniqueItem ? "code" : "item");
            if (name.empty() || code.empty() || name == "Expansion")
                continue;
            if (uniqueItem && !table.number(row, "enabled").value_or(0))
                continue;
            auto version = table.number(row, "version").value_or(0);
            if (version > 100)
                continue;
            auto level = table.number(row, "lvl");
            auto rarity = table.number(row, "rarity");
            if (level && *level > 99)
                continue; // Original event-only rows cannot be reached by the current 1–99 item levels.
            if (!level || !rarity || *level < 0 || *rarity < 0 ||
                !data.items.find(code))
                throw std::runtime_error("Invalid original special item: " + std::string(name));
            SpecialItemRecord record;
            record.row = row;
            record.name = name;
            record.code = code;
            if (auto file = table.value(row, "invfile"); !file.empty())
                record.icon = "data/global/items/" + std::string(file) + ".dc6";
            if (auto file = table.value(row, "flippyfile"); !file.empty())
                record.groundAnimation = "data/global/items/" + std::string(file) + ".dc6";
            record.level = *level;
            record.rarity = std::max(1, *rarity);
            record.requiredLevel = table.number(row, "lvl req").value_or(0);
            if (record.requiredLevel < 0 || record.requiredLevel > 99)
                throw std::runtime_error("Invalid original special item requirement: " + record.name);
            for (int slot = 1; slot <= (uniqueItem ? 12 : 9); ++slot) {
                auto suffix = std::to_string(slot);
                auto property = table.value(row, "prop" + suffix);
                if (property.empty())
                    continue;
                record.properties.push_back({std::string(property),
                                             std::string(table.value(row, "par" + suffix)),
                                             table.number(row, "min" + suffix),
                                             table.number(row, "max" + suffix),
                                             isDirectPropertyRoll(data, property)});
            }
            if (uniqueItem) {
                record.noLimit = table.number(row, "nolimit").value_or(0) != 0;
                record.ladder = table.number(row, "ladder").value_or(0) != 0;
            } else {
                record.set = table.value(row, "set");
                record.cowOnly = !cowSet.empty() && record.set == cowSet;
                for (int tier = 1; tier <= 4; ++tier)
                    for (auto half : {"a", "b"}) {
                        auto suffix = std::to_string(tier) + half;
                        auto property = table.value(row, "aprop" + suffix);
                        if (!property.empty())
                            record.setBonuses.push_back({std::to_string(tier + 1) + " pieces",
                                                         {std::string(property),
                                                          std::string(table.value(row, "apar" + suffix)),
                                                          table.number(row, "amin" + suffix),
                                                          table.number(row, "amax" + suffix)}});
                    }
                for (size_t setRow = 0; setRow < setDefinitions->second.rows().size(); ++setRow) {
                    if (setDefinitions->second.value(setRow, "index") != record.set)
                        continue;
                    const auto &setTable = setDefinitions->second;
                    for (int pieces = 2; pieces <= 5; ++pieces)
                        for (auto half : {"a", "b"}) {
                            auto suffix = std::to_string(pieces) + half;
                            auto property = setTable.value(setRow, "PCode" + suffix);
                            if (!property.empty())
                                record.setBonuses.push_back({std::to_string(pieces) + " pieces",
                                                             {std::string(property),
                                                              std::string(setTable.value(setRow, "PParam" + suffix)),
                                                              setTable.number(setRow, "PMin" + suffix),
                                                              setTable.number(setRow, "PMax" + suffix)}});
                        }
                    for (int slot = 1; slot <= 8; ++slot) {
                        auto suffix = std::to_string(slot);
                        auto property = setTable.value(setRow, "FCode" + suffix);
                        if (!property.empty())
                            record.setBonuses.push_back({"Full set",
                                                         {std::string(property),
                                                          std::string(setTable.value(setRow, "FParam" + suffix)),
                                                          setTable.number(setRow, "FMin" + suffix),
                                                          setTable.number(setRow, "FMax" + suffix)}});
                    }
                    break;
                }
            }
            destination.push_back(std::move(record));
        }
    };
    if (unique != data.tables.end())
        append(unique->second, true, data.uniqueItems);
    if (sets != data.tables.end())
        append(sets->second, false, data.setItems);
}
} // namespace d2x
