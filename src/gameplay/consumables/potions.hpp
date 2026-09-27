#pragma once
#include "gameplay/effects/state.hpp"
#include <array>
namespace d2x {
enum class PotionKind { Healing, Mana, Rejuvenation, Stamina, Remedy };
struct PotionDefinition {
    PotionKind kind;
    float amount, seconds;
    CombatStateDefinition state;
    CharacterModifiers modifiers;
    std::array<int, 2> cureStates{-1, -1};
    EffectFrame durationFrames = 0;
    bool curesPoison = false, curesCold = false;
};
} // namespace d2x
