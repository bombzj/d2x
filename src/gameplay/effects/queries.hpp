#pragma once
#include "values.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include <span>
#include <vector>

namespace d2x {
bool hasCombatState(std::span<const ActiveCombatEffect> effects, int stateId, EffectFrame frame);
bool hasCombatLifeState(std::span<const ActiveCombatEffect>,EffectFrame);
CharacterModifiers combatEffectModifiers(std::span<const ActiveCombatEffect> effects, EffectFrame frame,bool includePendingRemoval=false);
std::optional<std::map<StatKey,int32_t>> combatEffectNativeValues(const CombatEffectSpec &);
std::vector<ModifierStatSource> combatEffectStatSources(std::span<const ActiveCombatEffect>,EffectFrame,bool includePendingRemoval=false);
std::vector<TriggeredCombatEffect> combatEffectReactions(std::span<const ActiveCombatEffect> effects,
                                                      CombatEffectEvent event, EffectFrame frame);
}
