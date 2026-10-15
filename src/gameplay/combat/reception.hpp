#pragma once
#include "damage_resolution.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace d2x {
struct ResourceDrain {
    int64_t mana{}, stamina{};
    bool shieldEligible{};
    int64_t life{};
};
int64_t subtractContactResource(int64_t current, int64_t damage);
struct DamageReception {
    std::array<FixedDamageResult, 6> channels{};
    int64_t dealt{}, absorbed{}, mana{};
    ResourceDrain drain;
    int coldResistance{},poisonPierce{};
    std::vector<int> directionalOverlays;
};
// Absorption has already run against the actual recipient's private state.
DamageReception receiveDamageChannels(const std::array<int64_t,6> &,const CharacterAttributes &,
    DamageReception,bool monsterRules=false,const std::array<int,6> &pierce={},bool bypass=false,bool sanctuary=false);
}
