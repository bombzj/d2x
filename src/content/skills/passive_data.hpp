#pragma once
#include "gameplay/skills/passive.hpp"
#include "gameplay/effects/state.hpp"
#include <map>

namespace d2x {
struct SkillCatalog;
void applySkillPassives(CharacterModifiers &modifiers, const SkillCatalog &skills,
    const std::map<int, int> &ranks, const CombatEffectSet &effects, EffectFrame frame);
} // namespace d2x
