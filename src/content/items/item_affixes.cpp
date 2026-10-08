#include "item_affixes.hpp"
#include "item_properties.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
void loadMagicAffixData(ClassicData &data) {
    if (data.profile != "lod-named-txt-v1")
        return;
    for (auto name : {"magicprefix", "magicsuffix", "automagic"}) {
        auto found = data.tables.find(name);
        if (found == data.tables.end())
            continue;
        const auto &table = found->second;
        for (auto column : {"Name", "version", "spawnable", "rare", "level", "maxlevel",
                            "frequency", "group", "itype1", "etype1", "mod1code"})
            if (!table.has(column))
                throw std::runtime_error(std::string(name) + " lacks field: " + column);
        auto &records = std::string_view(name) == "magicprefix" ? data.magicPrefixes : std::string_view(name)=="magicsuffix"?data.magicSuffixes:data.autoMagic;
        for (size_t row = 0; row < table.rows().size(); ++row) {
            auto label = table.value(row, "Name");
            if (label.empty())
                continue;
            auto version = table.number(row, "version").value_or(0);
            if (version > 100 || !table.number(row, "spawnable").value_or(0))
                continue;
            auto level = table.number(row, "level");
            auto frequency = table.number(row, "frequency");
            auto group = table.number(row, "group");
            if (!frequency || *frequency == 0)
                continue; // Original zero/empty frequency cannot be selected.
            if (!level || *level < 0 || *frequency < 0 ||
                !group || *group < 0)
                throw std::runtime_error("Invalid original magic affix: " + std::string(label));
            if (*level > 99)
                continue;
            MagicAffixRecord record;
            record.row = row;
            record.name = label;
            record.level = *level;
            record.maxLevel = table.number(row, "maxlevel").value_or(0);
            record.requiredLevel = std::max(table.number(row, "levelreq").value_or(0),
                                            table.number(row, "classlevelreq").value_or(0));
            record.frequency = *frequency;
            record.group = *group;
            record.rareAllowed = table.number(row, "rare").value_or(0) != 0;
            record.characterClass = table.value(row, "class");
            if (record.maxLevel < 0 || record.requiredLevel < 0 || record.requiredLevel > 99)
                throw std::runtime_error("Invalid original magic affix maximum level");
            for (int slot = 1; slot <= 7; ++slot) {
                auto type = table.value(row, "itype" + std::to_string(slot));
                if (!type.empty())
                    record.includedTypes.emplace_back(type);
            }
            for (int slot = 1; slot <= 5; ++slot) {
                auto type = table.value(row, "etype" + std::to_string(slot));
                if (!type.empty())
                    record.excludedTypes.emplace_back(type);
            }
            for (int slot = 1; slot <= 3; ++slot) {
                auto suffix = std::to_string(slot);
                auto code = table.value(row, "mod" + suffix + "code");
                if (code.empty())
                    continue;
                auto minimum = table.number(row, "mod" + suffix + "min");
                auto maximum = table.number(row, "mod" + suffix + "max");
                record.properties.push_back({std::string(code),
                                             std::string(table.value(row, "mod" + suffix + "param")),
                                             minimum, maximum, isDirectPropertyRoll(data, code)});
            }
            records.push_back(std::move(record));
        }
    }
    for (auto name : {"rareprefix", "raresuffix"}) {
        auto found = data.tables.find(name);
        if (found == data.tables.end())
            continue;
        const auto &table = found->second;
        auto &records = std::string_view(name) == "rareprefix" ? data.rarePrefixes : data.rareSuffixes;
        for (size_t row = 0; row < table.rows().size(); ++row) {
            auto label = table.value(row, "name");
            if (label.empty() || table.number(row, "version").value_or(0) > 100)
                continue;
            RareNameRecord record;
            record.row = row;
            record.name = label;
            for (int slot = 1; slot <= 7; ++slot) {
                auto type = table.value(row, "itype" + std::to_string(slot));
                if (!type.empty()) record.includedTypes.emplace_back(type);
            }
            for (int slot = 1; slot <= 4; ++slot) {
                auto type = table.value(row, "etype" + std::to_string(slot));
                if (!type.empty()) record.excludedTypes.emplace_back(type);
            }
            records.push_back(std::move(record));
        }
    }
}
} // namespace d2x
