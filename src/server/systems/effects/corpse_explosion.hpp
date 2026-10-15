#pragma once
#include "server/runtime/contracts.hpp"
#include "gameplay/missiles/damage_data.hpp"
#include <vector>
#include <optional>
namespace d2x::server::effects {
struct CorpseExplosionPulse {
    bool prepared{},complete{};
    EntityId source,corpse;
    RegionId area;
    int skill{},rank{},physicalRadiusSquared{};
    Vec center;
    MissileHitDamage base;
    std::vector<EntityId> targets;
    size_t next{};
    std::optional<uint64_t> occurrence;
};
}
