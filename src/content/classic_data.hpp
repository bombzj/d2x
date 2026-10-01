#pragma once
#include "gameplay/items/definitions.hpp"
#include "gameplay/items/modifiers.hpp"
#include "gameplay/consumables/potions.hpp"
#include "gameplay/loot/loot.hpp"
#include "gameplay/loot/affix.hpp"
#include "gameplay/loot/grade.hpp"
#include "gameplay/loot/special.hpp"
#include "gameplay/character/attributes.hpp"
#include "content/npc/npc_dialogue.hpp"
#include "content/npc/hireling_data.hpp"
#include "content/skills/skill_data.hpp"
#include "content/skills/state_data.hpp"
#include "content/world/shrine_data.hpp"
#include "content/npc/vendor_data.hpp"
#include "resources/data_table.hpp"
#include "world/navigation.hpp"
#include <array>
#include <cstdint>
#include <set>
#include <utility>

namespace d2x {
class Archives;
using ClassicTreasureClass = TreasureClass;
struct ClassicMonsterData {
    std::string name, token;
    std::array<std::array<std::optional<unsigned>, 4>, 3> treasureClasses{};
    std::array<int, 3> drain{};
};
struct StashLayout {
    int columns = 0, rows = 0, left = 0, top = 0, cellSize = 0;
    bool expansion = false;
};
// Version-specific MPQ adapter. Rules consume typed records, not archive handles or TXT cells.
struct MonsterSpecialMissile {
    SkillSpec spec;
    ProjectileResource visual;
    int element = -1;
    bool killOnHit = true;
};
struct ClassicData {
    std::map<int, MissileCollisionRule> missileCollisions;
    std::map<int, bool> missileReturnFire;
    ClassicData(ItemCatalog itemCatalog, std::map<std::string, DataTable, std::less<>> sourceTables,
                std::string sourceProfile)
        : items(std::move(itemCatalog)), tables(std::move(sourceTables)),
          profile(std::move(sourceProfile)) {}
    ItemCatalog items;
    std::map<std::string, DataTable, std::less<>> tables;
    std::vector<CharacterDefinition> characters;
    SkillCatalog skills;
    CombatStateCatalog states;
    ShrineCatalog shrines;
    std::map<int, MonsterSpecialMissile> monsterSpecialMissiles;
    std::set<int> noMultiShotMissiles, unspreadMultiShotMissiles;
    std::vector<std::string> monsterNamePrefixes, monsterNameSuffixes;
    std::map<int, std::string> monsterModifierNames;
    std::map<int, int> teleportByLevel;
    std::array<int, 3> staticFieldMinimum{};
    std::array<int, 3> monsterFreezeDivisor{};
    std::array<int, 3> monsterColdDivisor{};
    std::array<int, 3> resistancePenalty{};
    std::array<int, 3> lifeStealDivisor{1, 1, 1}, manaStealDivisor{1, 1, 1};
    std::array<int, 3> hirelingBossDamagePercent{};
    StashLayout stashLayout;
    StashLayout cubeLayout;
    std::string cubeCode;
    // Class name -> level-indexed cumulative XP thresholds from Experience.txt.
    std::map<std::string, std::vector<uint64_t>, std::less<>> experienceByClass;
    std::vector<ClassicTreasureClass> treasures;
    std::vector<ClassicMonsterData> monsters;
    NpcDialogues npcDialogues;
    std::map<std::string, std::string, std::less<>> actOneQuestStrings;
    std::map<std::string, std::string, std::less<>> hirelingStrings;
    std::map<std::string, std::string, std::less<>> itemStrings;
    std::vector<HirelingDefinition> hirelings;
    HirelingLayout hirelingLayout;
    std::map<std::string, std::string> hirelingDescriptions;
    std::map<std::string, VendorDefinition, std::less<>> vendors;
    std::string profile;
    std::vector<std::string> armorTypes;
    std::vector<SpecialItemRecord> uniqueItems, setItems;
    std::vector<MagicAffixRecord> magicPrefixes, magicSuffixes;
    std::vector<RareNameRecord> rarePrefixes, rareSuffixes;
    std::vector<PropertyDefinition> properties;
    std::vector<ItemStatDefinition> itemStats;
    std::vector<QualityGradeRecord> superiorGrades, inferiorGrades;
    std::map<std::string, PotionDefinition, std::less<>> potions;
    std::set<std::string, std::less<>> portalScrolls;
    std::set<std::string, std::less<>> identifyScrolls;
    const PotionDefinition *potion(std::string_view code) const {
        auto found = potions.find(code);
        return found == potions.end() ? nullptr : &found->second;
    }
    bool isPortalScroll(std::string_view code) const { return portalScrolls.contains(code); }
    bool isIdentifyScroll(std::string_view code) const { return identifyScrolls.contains(code); }
};
ClassicData loadClassicData(Archives &archives);
void loadLodTreasureData(ClassicData &data);
} // namespace d2x
