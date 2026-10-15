#pragma once
#include "system.hpp"
#include "druid_active.hpp"
#include "server/systems/actions/release.hpp"
#include "server/runtime/combat_rules.hpp"
#include "gameplay/skills/cast_spec.hpp"

namespace d2x::server::skills {
struct System::Runtime {
    struct Pending { ActorContext actor; Request request; };
    std::map<PlayerId,Pending> pending;
    uint64_t nativeKickOccurrence{};
    std::map<EntityId,Release> assassinStarts,druidStarts;
    std::map<FrameEventHandle,std::shared_ptr<DruidActiveExecution>> druidActive;
    std::map<EntityId,Release> &releases;
    std::map<EntityId,MonsterRelease> &monsters;
    Runtime(std::map<EntityId,Release> &queue, std::map<EntityId,MonsterRelease> &monsterQueue)
        : releases(queue), monsters(monsterQueue) {}
};
}
