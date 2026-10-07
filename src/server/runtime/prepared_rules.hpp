#pragma once
#include "gameplay/items/definitions.hpp"
#include "gameplay/loot/loot.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/effects/definition.hpp"
#include <map>
#include <memory>
#include <vector>

namespace d2x::server {
using SkillRules = std::map<int, SkillSpec>;
using EffectRules = std::map<int, CombatStateDefinition>;
using TreasureRules = std::vector<TreasureClass>;
// Immutable value definitions prepared by hosting/content, with instance-long
// ownership. Null means not prepared, never "use invented default rules".
// Additional native tables belong in their domain's typed value contract.
struct PreparedRules {
    std::shared_ptr<const ItemCatalog> items;
    std::shared_ptr<const SkillRules> skills;
    std::shared_ptr<const EffectRules> effects;
    std::shared_ptr<const TreasureRules> treasure;
};
}
