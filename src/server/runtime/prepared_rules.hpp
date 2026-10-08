#pragma once
#include "combat_rules.hpp"
#include "skill_rules.hpp"
#include "gameplay/consumables/potions.hpp"
#include "core/id.hpp"
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
#include <map>
#include <set>
#include <memory>
#include <vector>

namespace d2x::server {
using EffectRules = std::map<int, CombatStateDefinition>;
using TreasureRules = std::vector<TreasureClass>;
// Hosting resolves the admitted property instructions at each MPQ-supported
// character level. Runtime selects values; it never retains content callbacks.
struct ItemLevelValues {
    std::vector<ResolvedItemStat> stats;
    std::array<std::vector<ResolvedItemStat>, 5> setStats;
    unsigned maximumDurability{};
};
struct EquipmentValues { std::vector<ItemLevelValues> levels; bool singleCarry{}; };
struct EquipmentRules {
    std::map<EntityId, EquipmentValues> items;
    std::vector<EquipmentSetPiece> sets;
    std::map<std::pair<size_t, size_t>, std::vector<std::vector<ResolvedItemStat>>> setBonuses;
    const ItemLevelValues &at(EntityId, int level) const;
    void includeSets(const EquipmentRules &);
};
struct LearningRule {
    std::string classCode;
    int page{}, requiredLevel{}, maximumRank{};
    std::array<int, 4> requiredAttributes{};
    std::vector<int> prerequisites;
    SkillPassiveSpec passive;
    std::optional<std::pair<int, int>> manaRecoveryPerRank;
    bool selectable{}, leftAllowed{};
};
enum class ItemSkillAction { Identify, Portal };
struct ItemSkillRule { int skill{}; bool book{}; ItemSkillAction action{}; int cursor{-1}; };
struct CharacterRules {
    std::set<int> innateSkills;
    std::map<std::string,ItemSkillRule,std::less<>> itemSkills;
    std::vector<uint64_t> experience;
    std::map<int, LearningRule> learning;
    int resistancePenalty{};
    int deathTicks{}, deathExperiencePenalty{};
};
// Immutable value definitions prepared by hosting/content, with instance-long
// ownership. Null means not prepared, never "use invented default rules".
// Additional native tables belong in their domain's typed value contract.
using PotionRules = std::map<std::string, PotionDefinition, std::less<>>;
struct TransientAttributes { CharacterModifiers modifiers; std::set<int> states; };
struct PreparedRules {
    std::shared_ptr<const PotionRules> potions;
    std::shared_ptr<const MeleeRules> melee;
    std::shared_ptr<const ItemCatalog> items;
    std::shared_ptr<const EquipmentRules> equipment;
    std::shared_ptr<const CharacterRules> character;
    std::shared_ptr<const SkillRules> skills;
    std::shared_ptr<const EffectRules> effects;
    std::shared_ptr<const TreasureRules> treasure;
};
}
