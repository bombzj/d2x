#pragma once
#include "resources/data_table.hpp"
#include <string>
#include <vector>
#include <array>
#include <cstdint>

namespace d2x {
struct HirelingSkillDefinition {
    int id = -1, mode = 0, requiredLevel = 0, level = 0, levelPerLevel = 0;
    int chance = 0, chancePerLevel = 0, aiType = 0, channelFrames = 0;
};
struct HirelingDefinition {
    int sourceRow = -1;
    int classId = -1, seller = -1, act = 0, difficulty = 0;
    int level = 0, life = 0, attackRating = 0, damageMin = 0, damageMax = 0;
    std::string subtype, nameFirst, nameLast;
    int id = -1, version = 0, gold = 0, experiencePerLevel = 0;
    int lifePerLevel = 0, defense = 0, defensePerLevel = 0;
    int strength = 0, strengthPerLevel = 0, dexterity = 0, dexterityPerLevel = 0;
    int attackPerLevel = 0, damagePerLevel = 0, resist = 0, resistPerLevel = 0;
    std::string description, weaponType1, weaponType2;
    int defaultChance = 0;
    std::vector<HirelingSkillDefinition> skills;
};
struct HirelingStats {
    int life = 0, defense = 0, strength = 0, dexterity = 0, attackRating = 0;
    int damageMin = 0, damageMax = 0, resist = 0;
    unsigned price = 0;
    uint64_t experience = 0, nextExperience = 0;
};
struct HirelingOffer {
    uint32_t slot = 0;
    int sourceRow = -1, level = 0;
    std::string nameKey;
    HirelingStats stats;
    uint32_t seed = 0;
};
struct HirelingLayout {
    // Head, torso, right arm, left arm; original Hireling row, panel-local pixels.
    std::array<std::array<int, 4>, 4> slots{};
};
std::vector<HirelingDefinition> loadHirelingDefinitions(const DataTable &table, const DataTable &skills);
HirelingLayout loadHirelingLayout(const DataTable &table);
HirelingStats deriveHirelingStats(const HirelingDefinition &definition, int level);
std::vector<HirelingOffer> planHirelingOffers(const std::vector<HirelingDefinition> &definitions,
                                            int seller, int difficulty, int playerLevel, uint64_t &seed);
} // namespace d2x
