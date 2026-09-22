#include "classic_data.hpp"
#include <stdexcept>

namespace d2x {
void loadLodTreasureData(ClassicData &data) {
    const auto &table = data.tables.at("treasureclassex");
    std::map<std::string, unsigned, std::less<>> indices;
    for (size_t row = 0; row < table.rows().size(); ++row) {
        auto name = table.value(row, "Treasure Class");
        if (name.empty() || name == "Expansion")
            continue;
        ClassicTreasureClass record;
        record.name = name;
        record.picks = table.number(row, "Picks");
        record.noDrop = table.number(row, "NoDrop");
        record.group = table.number(row, "group");
        record.level = table.number(row, "level");
        const char *qualities[] = {"Unique", "Set", "Rare", "Magic"};
        for (int i = 0; i < 4; ++i)
            record.quality[i] = table.number(row, qualities[i]);
        for (int slot = 1; slot <= 10; ++slot) {
            auto item = table.value(row, "Item" + std::to_string(slot));
            if (item.empty())
                continue;
            record.codes.emplace_back(item);
            record.weights.push_back(table.number(row, "Prob" + std::to_string(slot)).value_or(0));
        }
        if (!indices.emplace(record.name, unsigned(data.treasures.size())).second)
            throw std::runtime_error("Duplicate TreasureClassEx: " + record.name);
        data.treasures.push_back(std::move(record));
    }
    const auto &monsters = data.tables.at("monstats");
    for (size_t row = 0; row < monsters.rows().size(); ++row) {
        auto name = monsters.value(row, "Id");
        if (name.empty() || name == "Expansion")
            continue;
        ClassicMonsterData record;
        record.name = name;
        record.token = monsters.value(row, "Code");
        const char *suffix[] = {"", "(N)", "(H)"};
        for (int difficulty = 0; difficulty < 3; ++difficulty)
            for (int slot = 0; slot < 4; ++slot) {
                auto tc =
                    monsters.value(row, "TreasureClass" + std::to_string(slot + 1) + suffix[difficulty]);
                if (tc.empty())
                    continue;
                auto found = indices.find(tc);
                if (found == indices.end())
                    throw std::runtime_error("Unknown monster TreasureClassEx: " + std::string(tc));
                record.treasureClasses[difficulty][slot] = found->second;
            }
        data.monsters.push_back(std::move(record));
    }
}
} // namespace d2x
