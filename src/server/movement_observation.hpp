#pragma once
#include "server/player_identity.hpp"
#include "core/entity_id.hpp"
#include "core/math.hpp"
#include "world/identity.hpp"
#include <string>

namespace d2x::server {
struct MovementObservation {
    GameHandle game;
    EntityId actor;
    uint32_t nativeGuid{};
    RegionId area{};
    uint64_t tick{}, teleportRevision{}, commandSequence{}, movementSequence{};
    Vec position, destination;
    EntityId target;
    uint8_t targetType{};
    bool moving{}, running{}, alive{}, knockback{}, paused{};
    uint64_t executedSequence{};
    bool simulationPending{};
    std::string pendingPhase;
};
}