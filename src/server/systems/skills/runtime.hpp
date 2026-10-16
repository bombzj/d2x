#pragma once
#include "system.hpp"
#include "druid_active.hpp"
#include "server/systems/actions/release.hpp"
#include "server/runtime/combat_rules.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/actions/monster_timing.hpp"

namespace d2x::server::skills {
struct System::Runtime {
    struct Pending { ActorContext actor; Request request; };
    std::map<PlayerId,Pending> pending;
    uint64_t nativeKickOccurrence{};
    std::map<EntityId,Release> assassinStarts,druidStarts,weaponStarts;
    struct MonsterWeaponStart {uint64_t tick{};uint16_t skill{};uint8_t mode{};MonsterAttackRule program;};
    std::map<EntityId,MonsterWeaponStart> monsterWeaponStarts;
    std::map<FrameEventHandle,std::shared_ptr<DruidActiveExecution>> druidActive;
    struct MonsterMode {
        MonsterAttackRule program;
        MonsterAnimationCursor animation;
        uint64_t identity{},interruption{},lastFrame{};
        bool advanced{};
        MonsterAnimationStep step;
        std::optional<MonsterRelease> repeated;
    };
    std::map<EntityId,MonsterMode> monsterModes;
    std::map<EntityId,Release> &releases;
    std::map<EntityId,MonsterRelease> &monsters;
    Runtime(std::map<EntityId,Release> &queue, std::map<EntityId,MonsterRelease> &monsterQueue)
        : releases(queue), monsters(monsterQueue) {}
};
}
