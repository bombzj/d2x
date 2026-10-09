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
float potionRestorationAmount(const PotionDefinition &, std::string_view playerClass);
int64_t rollPotionRestoration(const PotionDefinition &, std::string_view playerClass, int attribute, uint64_t &random);
} // namespace d2x
