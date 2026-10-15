#pragma once
#include "gameplay/loot/chest.hpp"
#include "gameplay/effects/definition.hpp"
#include "gameplay/loot/tower_reward.hpp"
#include "combat_rules.hpp"
#include <array>
#include <optional>
namespace d2x::server {
struct ShrineRule { int code{}, argument0{}, argument1{}, duration{}, reset{}; CombatStateDefinition state; std::map<int,MonsterAttackRule> projectiles; MonsterHitStates hitStates; };
struct ObjectRule {
    int operation{}, width{}, height{}, range{};
    int initialization{},damage{},initialMode{};
    uint16_t collisionMask{};
    std::array<bool, 8> collision{}, light{};
    std::array<bool, 8> selectable{};
    uint64_t openingTicks{},closingTicks{};
    int trap{};
    std::array<int, 8> parameters{};
    bool door{}, stash{};
    bool monsterUsable{};
    std::vector<NativeStateCure> cureStates;
    std::optional<ChestState> chest;
    std::optional<ShrineRule> shrine;
    std::optional<TowerReward> towerReward;
};
}
