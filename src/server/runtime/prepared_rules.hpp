#pragma once
#include "combat_rules.hpp"
#include "skill_rules.hpp"
#include "equipment_rules.hpp"
#include "gameplay/consumables/potions.hpp"
#include "gameplay/stats/stat_list.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "core/id.hpp"
#include "world/identity.hpp"
#include "gameplay/items/definitions.hpp"
#include "gameplay/items/modifiers.hpp"
#include "gameplay/items/equipment_set.hpp"
#include "gameplay/skills/passive.hpp"
#include <optional>
#include <array>
#include <cstddef>
#include <utility>
#include <string>
#include "gameplay/loot/loot.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/effects/definition.hpp"
#include "gameplay/monsters/region_generation.hpp"
#include <map>
#include <set>
#include <memory>
#include <vector>

namespace d2x {class CombatEffectSet;}
namespace d2x::pd2 {struct MapCatalog;}
namespace d2x::server {
using EffectRules = std::map<int, CombatStateDefinition>;
using TreasureRules = std::vector<TreasureClass>;
struct LearningRule {
    std::string classCode;
    int page{}, requiredLevel{}, maximumRank{};
    std::array<int, 4> requiredAttributes{};
    std::vector<int> prerequisites;
    SkillPassiveSpec passive;
    std::optional<std::pair<int, int>> manaRecoveryPerRank;
    bool selectable{}, leftAllowed{};
    std::optional<std::pair<int,int>> fireMasteryPerRank,lightningMasteryPerRank,coldMasteryPerRank;
};
enum class ItemSkillAction { Identify, Portal };
struct ItemSkillRule { int skill{}; bool book{}; ItemSkillAction action{}; int cursor{-1}; };
enum class QuestConsumable { SkillBook, LifePotion, ResistanceScroll, RespecToken };
struct CharacterRules {
    std::shared_ptr<const pd2::MapCatalog> maps;
    std::shared_ptr<const CombatStateRules> states;
    std::shared_ptr<const StatRuleCatalog> stats;
    std::shared_ptr<const SkillFormulaCatalog> formulas;
    int tradeColumns{}, tradeRows{}; // Original Inventory.Trade Page 2-2.
    struct ActTwoItems {
        std::string cube, scroll, shaft, amulet, staff, book;
        bool artifact(std::string_view code) const { return code==scroll || code==shaft || code==amulet || code==staff; }
    } actTwo;
    struct LaterQuestItems {
        std::string figurine,bird,lifePotion,gidbinn,tome,soulstone,hammer,defrostPotion,resistanceScroll;
        std::array<std::string,4> khalimParts;
        std::string khalimWill;
    } laterQuests;
    std::map<RegionId,int> waypointIndices; // Immutable MPQ Levels identities, including unloaded areas.
    std::set<RegionId> areaIds; // Prepared content; administrative travel never invents a level.
    std::set<int> innateSkills;
    std::vector<int> innateSkillOrder; // CharStats order, following native Attack initialization.
    std::map<std::string,ItemSkillRule,std::less<>> itemSkills;
    std::vector<uint64_t> experience;
    std::map<int, LearningRule> learning;
    int resistancePenalty{};
    int deathTicks{}, deathExperiencePenalty{};
    std::set<int> healerCureStates;
    int noManaRegenState = -1;
    int bloodManaState = -1;
    std::map<std::string,QuestConsumable,std::less<>> questConsumables;
};
// Immutable value definitions prepared by hosting/content, with instance-long
// ownership. Null means not prepared, never "use invented default rules".
// Additional native tables belong in their domain's typed value contract.
using PotionRules = std::map<std::string, PotionDefinition, std::less<>>;
struct TransientAttributes {
    CharacterModifiers modifiers;std::set<int> states;
    std::optional<std::vector<ModifierStatSource>> sources;
    std::shared_ptr<const CombatEffectSet> effects; // Runtime lifecycle snapshot; never serialized.
    std::optional<std::span<const ModifierStatSource>> statSources() const {
        if(sources) return std::span<const ModifierStatSource>{*sources};
        return {};
    }
};
struct PreparedRules {
    std::shared_ptr<const MonsterRegionRules> monsterRegions;
    std::shared_ptr<const PotionRules> potions;
    std::shared_ptr<const ItemCatalog> items;
    std::shared_ptr<const EquipmentRules> equipment;
    std::shared_ptr<const CharacterRules> character;
    std::shared_ptr<const SkillRules> skills;
    std::shared_ptr<const EffectRules> effects;
    std::shared_ptr<const TreasureRules> treasure;
};
}
