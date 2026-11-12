#pragma once
#include "server/player_identity.hpp"
#include "core/entity_id.hpp"
#include "core/math.hpp"
#include "world/identity.hpp"
#include <string>
#include <optional>
#include <vector>

namespace d2x::server {
struct NpcMovementObservation {
    uint32_t guid{};
    int nativeClass{};
    Vec position, goal;
    std::optional<Vec> next, end;
    std::optional<Vec> collisionCell;
    uint16_t collisionMask{};
    uint8_t collisionPattern{}, mode{}, pathType{};
    bool moving{};
};
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
    uint32_t mapSeed{};
    std::optional<Vec> next, end;
    std::vector<NpcMovementObservation> npcs;
};
}
