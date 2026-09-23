#pragma once
#include "gameplay/items/definitions.hpp"
#include "gameplay/items/modifiers.hpp"
#include "gameplay/consumables/potions.hpp"
#include "gameplay/loot/loot.hpp"
#include "gameplay/loot/affix.hpp"
#include "gameplay/loot/grade.hpp"
#include "gameplay/loot/special.hpp"
#include "npc_dialogue.hpp"
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include <array>
#include <set>
#include <utility>

namespace d2x {
using ClassicTreasureClass = TreasureClass;
struct ClassicMonsterData {
    std::string name, token;
    std::array<std::array<std::optional<unsigned>, 4>, 3> treasureClasses{};
};
// Version-specific MPQ adapter. Rules consume typed records, not archive handles or TXT cells.
struct ClassicData {
    ClassicData(ItemCatalog itemCatalog, std::map<std::string, DataTable, std::less<>> sourceTables,
                std::string sourceProfile)
        : items(std::move(itemCatalog)), tables(std::move(sourceTables)),
          profile(std::move(sourceProfile)) {}
    ItemCatalog items;
    std::map<std::string, DataTable, std::less<>> tables;
    std::vector<ClassicTreasureClass> treasures;
    std::vector<ClassicMonsterData> monsters;
    NpcDialogues npcDialogues;
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
    const PotionDefinition *potion(std::string_view code) const {
        auto found = potions.find(code);
        return found == potions.end() ? nullptr : &found->second;
    }
    bool isPortalScroll(std::string_view code) const { return portalScrolls.contains(code); }
};
ClassicData loadClassicData(Archives &archives);
void loadLodTreasureData(ClassicData &data);
} // namespace d2x
