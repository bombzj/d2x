#pragma once
#include "gameplay/effects/definition.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/stats/stat_list.hpp"
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace d2x {
struct AuraDefinition {
    int skill = -1, rank = 0, periodFrames = 0;
    float radius = 0;
    CombatStateDefinition state, ownerState;
    CharacterModifiers modifiers;
    CharacterModifiers ownerModifiers;
    std::vector<std::pair<int, int>> synergies;
    int ownerDamageBonus = 0, elementalMultiplier = 0;
    int element = -1;
    int hitClass = 13;
    uint32_t resultFlags = 0;
    Fixed8 minimumDamage,maximumDamage;
    Fixed8 lifePerPulse,manaPerPulse;
    int harmfulDurationPercent = 100;
    int redemptionChance = 0;
    Fixed8 redemptionLife,redemptionMana;
    bool hostile = false;
    uint32_t filter = 0;
    // Native SetStat instruction order. Missing retains an unmigrated adapter;
    // present-empty is an authoritative state with no attribute writes.
    std::optional<std::vector<StatValue>> ownerWrites,targetWrites;
    size_t ownerCallbackWrites{}; // Following writes are direct owner passives.
    int serverDoFunction{};
};
inline CharacterModifiers auraOwnerModifiers(const AuraDefinition &a) {
    if(a.ownerWrites) return a.ownerModifiers;
    auto result=a.ownerModifiers;
    if(!a.hostile) mergeCharacterModifiers(result,a.modifiers);
    result.combat.damagePercent+=a.ownerDamageBonus;
    return result;
}
} // namespace d2x
