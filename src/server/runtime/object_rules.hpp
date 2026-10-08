#pragma once
#include "gameplay/loot/chest.hpp"
#include "gameplay/effects/definition.hpp"
#include <array>
#include <optional>
namespace d2x::server {
struct ShrineRule { int code{}, argument0{}, argument1{}, duration{}, reset{}; CombatStateDefinition state; };
struct ObjectRule {
    int operation{}, width{}, height{}, range{};
    uint16_t collisionMask{};
    std::array<bool, 8> collision{}, light{};
    uint64_t openingTicks{};
    std::array<int, 8> parameters{};
    bool door{}, stash{};
    std::optional<ChestState> chest;
    std::optional<ShrineRule> shrine;
};
}
