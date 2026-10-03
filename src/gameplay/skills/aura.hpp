#pragma once
#include "gameplay/effects/definition.hpp"
#include "gameplay/character/attributes.hpp"
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
    float minimumDamage = 0, maximumDamage = 0;
    float lifePerPulse = 0, manaPerPulse = 0;
    int harmfulDurationPercent = 100;
    int redemptionChance = 0;
    float redemptionLife = 0, redemptionMana = 0;
    bool hostile = false;
    uint32_t filter = 0;
};
struct ActiveAura {
    AuraDefinition definition;
    EffectFrame nextFrame = 0;
};
}