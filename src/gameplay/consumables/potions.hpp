#pragma once
#include "gameplay/effects/definition.hpp"
#include "gameplay/character/attributes.hpp"
#include <array>
#include <string_view>
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
// Empty class means a non-player unit. The loaded amount is the original
// Misc.calc base; class bonuses are engine rules, applied at consumption.
float potionRestorationAmount(const PotionDefinition &potion, std::string_view playerClass);
} // namespace d2x
