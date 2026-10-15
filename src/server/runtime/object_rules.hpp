#pragma once
#include "gameplay/loot/chest.hpp"
#include "gameplay/effects/definition.hpp"
#include "gameplay/loot/tower_reward.hpp"
#include "combat_rules.hpp"
#include <array>
#include <optional>
namespace d2x::server {
struct ShrineRule { int code{}, argument0{}, argument1{}, duration{}, reset{}; CombatStateDefinition state; std::map<int,MonsterAttackRule> projectiles; MonsterHitStates hitStates; };
struct ShrineGenerationEntry { int code{},effectClass{},minimumLevel{}; };
struct ObjectGroupEntry { int definition{},probability{},density{}; };
struct ObjectGenerationRule {
    int function{},gore{},orientation{},spacingX{},spacingY{},subClass{};
};
struct ObjectPopulationRules {
    uint32_t themes{};
    struct ItemCandidate {std::string code;int level{},rarity{};bool spawnable{},quest{},body{};};
    std::array<std::vector<ItemCandidate>,3> itemCandidates;
    std::string firstItemCode;
    int itemSelectionAct{};
    std::array<int,8> groups{},probabilities{};
    std::map<int,std::array<ObjectGroupEntry,8>> entries;
    std::map<int,ObjectGenerationRule> objects;
};
struct ObjectRule {
    int operation{}, width{}, height{}, range{};
    int initialization{},damage{},initialMode{};
    int initializationLevel{};
    bool lockable{};
    int shrinePreset{};
    bool preOperate{},specialChest{};
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
    bool openingMode{};
};
}
