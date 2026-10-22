#include "core/random.hpp"
#include "hireling_data.hpp"
#include <stdexcept>
#include <algorithm>
#include <charconv>
#include <iomanip>
#include <sstream>

namespace d2x {
std::vector<HirelingDefinition> loadHirelingDefinitions(const DataTable &table, const DataTable &skills) {
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
        result.emplace_back();
        auto &entry = result.back();
        entry.sourceRow = int(row);
        entry.classId = *actor;
        entry.seller = *seller;
        entry.act = *act;
        entry.difficulty = *difficulty;
        entry.level = *level;
        entry.life = *life;
        entry.attackRating = *rating;
        entry.damageMin = *minimum;
        entry.damageMax = *maximum;
        entry.subtype = table.value(row, "SubType");
        entry.nameFirst = table.value(row, "NameFirst");
        entry.nameLast = table.value(row, "NameLast");
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
        entry.defaultChance = number("DefaultChance");
        for (int i = 1; i <= 6; ++i) {
            const auto suffix = std::to_string(i);
            const auto name = table.value(row, "Skill" + suffix);
            if (name.empty()) break;
            size_t skill = 0;
            while (skill < skills.rows().size() && skills.value(skill, "skill") != name) ++skill;
            if (skill == skills.rows().size()) throw std::runtime_error("Unknown original hireling skill");
            auto n = [&](std::string_view field) {
                const auto value = table.number(row, field);
                if (!value) throw std::runtime_error("Missing original hireling skill parameter: " + std::string(field));
                return *value;
            };
            // Resolve state identity from the skill catalog at selection time.
            entry.skills.push_back({skills.number(skill, "Id").value(), n("Mode" + suffix),
                skills.number(skill, "reqlevel").value_or(0), n("Level" + suffix), n("LvlPerLvl" + suffix),
                n("Chance" + suffix), n("ChancePerLvl" + suffix), skills.number(skill, "aitype").value_or(0),
                name == "Inferno" ? skills.number(skill, "calc2").value() : 0});
            if (entry.skills.back().mode < 0 || entry.skills.back().mode >= 16)
                throw std::runtime_error("Unsupported original hireling skill mode");
        }
    }
    if (result.empty()) throw std::runtime_error("Original expansion hireling records are missing");
    return result;
}
HirelingLayout loadHirelingLayout(const DataTable &table) {
    HirelingLayout layout;
    for (auto &box : layout.slots) box.fill(-1);
    for (size_t row = 0; row < table.rows().size(); ++row) {
        if (table.value(row, "class") != "Hireling") continue;
        size_t index = 0;
        for (auto slot : {"head", "torso", "rArm", "lArm", "belt", "feet", "gloves"}) {
            auto &box = layout.slots[index++];
            size_t coordinate = 0;
            for (auto edge : {"Left", "Top", "Right", "Bottom"}) {
                auto value = table.number(row, std::string(slot) + edge);
                if (!value || *value < 0) {
                    if (index <= 4) throw std::runtime_error("Missing original hireling slot");
                    box.fill(-1); break; // Original resources omit the extra PD2 slots.
                }
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
std::optional<HirelingOffer> resolveHirelingOffer(const std::vector<HirelingDefinition> &definitions,
    int seller, int difficulty, int playerLevel, uint32_t seed, uint32_t slot) {
    std::vector<const HirelingDefinition *> pool;
    int firstLevel = -1;
    for (const auto &entry : definitions) {
        if (entry.seller != seller || entry.difficulty != difficulty + 1) continue;
        if (firstLevel < 0) firstLevel = entry.level;
        if (entry.level == firstLevel) pool.push_back(&entry);
    }
    if (pool.empty() || !slot || playerLevel < 1) return {};
    const auto &firstKey = pool.front()->nameFirst;
    const auto &lastKey = pool.front()->nameLast;
    const auto prefix = firstKey.find_last_not_of("0123456789") + 1;
    if (firstKey.size() < 2 || firstKey.size() != lastKey.size() || prefix >= firstKey.size()) return {};
    int first = 0, last = 0;
    const auto firstParsed = std::from_chars(firstKey.data() + prefix, firstKey.data() + firstKey.size(), first);
    const auto lastParsed = std::from_chars(lastKey.data() + prefix, lastKey.data() + lastKey.size(), last);
    if (firstParsed.ec != std::errc{} || lastParsed.ec != std::errc{} || first > last || slot > unsigned(last - first + 1)) return {};
    auto random = initialRandom(seed);
    rollRandom(random);
    const auto &definition = *pool[uint32_t(random) % pool.size()];
    rollRandom(random);
    const int delta = std::min(int32_t(uint32_t(random)) % 5, playerLevel - definition.level);
    const int level = std::max(1, definition.level + delta);
    std::ostringstream key;
    key << firstKey.substr(0, prefix) << std::setw(int(firstKey.size() - prefix))
        << std::setfill('0') << first + int(slot - 1);
    return HirelingOffer{slot, definition.sourceRow, level, key.str(), deriveHirelingStats(definition, level), seed};
}
} // namespace d2x
