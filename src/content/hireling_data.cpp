#include "core/random.hpp"
#include "hireling_data.hpp"
#include <stdexcept>
#include <algorithm>
#include <charconv>
#include <iomanip>
#include <sstream>

namespace d2x {
std::vector<HirelingDefinition> loadHirelingDefinitions(const DataTable &table) {
    for (auto column : {"Version", "Class", "Seller", "Act", "Difficulty", "Level", "HP", "SubType",
                        "NameFirst", "NameLast", "AR", "Dmg-Min", "Dmg-Max"})
        if (!table.has(column)) throw std::runtime_error("Original Hireling table lacks a required column");
    std::vector<HirelingDefinition> result;
    for (size_t row = 0; row < table.rows().size(); ++row) {
        if (table.number(row, "Version") != 100) continue;
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
        auto &entry = result.back();
        auto number = [&](const char *column) {
            auto value = table.number(row, column);
            if (!value) throw std::runtime_error(std::string("Missing hireling value: ") + column);
            return *value;
        };
        entry.id = number("Id"); entry.version = number("Version");
        entry.gold = number("Gold"); entry.experiencePerLevel = number("Exp/Lvl");
        entry.lifePerLevel = number("HP/Lvl");
        entry.defense = number("Defense"); entry.defensePerLevel = number("Def/Lvl");
        entry.strength = number("Str"); entry.strengthPerLevel = number("Str/Lvl");
        entry.dexterity = number("Dex"); entry.dexterityPerLevel = number("Dex/Lvl");
        entry.attackPerLevel = number("AR/Lvl"); entry.damagePerLevel = number("Dmg/Lvl");
        entry.resist = number("Resist"); entry.resistPerLevel = number("Resist/Lvl");
        entry.description = table.value(row, "HireDesc");
        entry.weaponType1 = table.value(row, "WType1"); entry.weaponType2 = table.value(row, "WType2");
    }
    if (result.empty()) throw std::runtime_error("Original expansion hireling records are missing");
    return result;
}
HirelingLayout loadHirelingLayout(const DataTable &table) {
    HirelingLayout layout;
    for (size_t row = 0; row < table.rows().size(); ++row) {
        if (table.value(row, "class") != "Hireling") continue;
        size_t index = 0;
        for (auto slot : {"head", "torso", "rArm", "lArm"}) {
            auto &box = layout.slots[index++];
            size_t coordinate = 0;
            for (auto edge : {"Left", "Top", "Right", "Bottom"}) {
                auto value = table.number(row, std::string(slot) + edge);
                if (!value || *value < 0) throw std::runtime_error("Missing original hireling slot");
                box[coordinate++] = *value;
            }
        }
        return layout;
    }
    throw std::runtime_error("Missing original Hireling inventory row");
}
HirelingStats deriveHirelingStats(const HirelingDefinition &d, int level) {
    // D2Common MONSTERS_HirelingInit / D2Game MONSTERAI_UpdateMercStatsAndSkills.
    const int delta = level - d.level;
    HirelingStats s;
    s.life = std::max(40, d.life + delta * d.lifePerLevel);
    s.defense = std::max(0, d.defense + delta * d.defensePerLevel);
    s.strength = std::max(10, d.strength + delta * d.strengthPerLevel / 8);
    s.dexterity = std::max(10, d.dexterity + delta * d.dexterityPerLevel / 8);
    s.attackRating = std::max(0, d.attackRating + delta * d.attackPerLevel);
    s.damageMin = std::max(0, d.damageMin + delta * d.damagePerLevel / 8);
    s.damageMax = std::max(1, d.damageMax + delta * d.damagePerLevel / 8);
    s.resist = std::max(0, d.resist + delta * d.resistPerLevel / 4);
    s.price = unsigned(std::max(d.gold, d.gold * (100 + delta * 15) / 100));
    auto experience = [&](uint64_t l) { return l * l * (l + 1) * uint64_t(d.experiencePerLevel); };
    s.experience = experience(uint64_t(level));
    s.nextExperience = level < 99 ? experience(uint64_t(level + 1)) : 0;
    return s;
}
std::vector<HirelingOffer> planHirelingOffers(const std::vector<HirelingDefinition> &definitions,
    int seller, int difficulty, int playerLevel, uint64_t &seed) {
    std::vector<const HirelingDefinition *> pool;
    int firstLevel = -1;
    for (const auto &entry : definitions) {
        if (entry.seller != seller || entry.difficulty != difficulty + 1) continue;
        if (firstLevel < 0) firstLevel = entry.level;
        if (entry.level == firstLevel) pool.push_back(&entry);
    }
    if (pool.empty()) return {};
    const auto &firstKey = pool.front()->nameFirst;
    const auto &lastKey = pool.front()->nameLast;
    if (firstKey.size() < 2 || firstKey.size() != lastKey.size()) return {};
    const size_t prefix = firstKey.find_last_not_of("0123456789") + 1;
    int first = 0, last = 0;
    auto a = std::from_chars(firstKey.data() + prefix, firstKey.data() + firstKey.size(), first);
    auto b = std::from_chars(lastKey.data() + prefix, lastKey.data() + lastKey.size(), last);
    if (a.ec != std::errc{} || b.ec != std::errc{} || first > last) return {};
    auto random = [](uint64_t &value, unsigned bound) {
        rollRandom(value);
        return bound ? uint32_t(value) % bound : uint32_t(value);
    };
    std::vector<uint32_t> seeds(size_t(last - first + 1));
    for (auto &value : seeds)
        do { value = random(seed, 0); } while (!value); // D2S uses zero for no mercenary.
    std::vector<bool> available(seeds.size());
    // SUnitNpc: ten distinct candidates selected from the full original name range.
    for (size_t count = 0; count < std::min<size_t>(10, seeds.size()); ++count) {
        size_t index = random(seed, unsigned(seeds.size()));
        while (available[index]) index = (index + 1) % seeds.size();
        available[index] = true;
    }
    std::vector<HirelingOffer> result;
    for (size_t index = 0; index < seeds.size(); ++index) {
        if (!available[index]) continue;
        uint64_t local = initialRandom(seeds[index]);
        const auto &d = *pool[random(local, unsigned(pool.size()))];
        const int levelRoll = int32_t(random(local, 0)) % 5;
        const int level = std::max(1, d.level + std::min(levelRoll, playerLevel - d.level));
        std::ostringstream key;
        key << firstKey.substr(0, prefix) << std::setw(int(firstKey.size() - prefix))
            << std::setfill('0') << first + int(index);
        result.push_back({uint32_t(index + 1), d.sourceRow, level, key.str(), deriveHirelingStats(d, level), seeds[index]});
    }
    return result;
}
} // namespace d2x
