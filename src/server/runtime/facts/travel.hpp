#pragma once
#include "core/entity_id.hpp"
#include "core/math.hpp"
#include "world/identity.hpp"
#include "server/player_identity.hpp"
#include <vector>
#include <cstdint>
#include <optional>

namespace d2x::server {
struct RepositionFact {
    EntityId actor; RegionId area; Vec position; bool notifyOwner=true; uint8_t flags{}; bool revealRoom{};
    // Operate27 sends the counterpart's room after the relocation helper's
    // destination room, even when both refer to the same room.
    struct Room {RegionId area;uint16_t x{},y{};};
    std::optional<Room> operatedRoom{};
};
struct TravelFact { PlayerId player; EntityId actor; RegionId from, to; uint64_t areaGeneration{}; Vec position; bool walking{}, revived{}; };
struct WaypointFact { EntityId source; std::vector<RegionId> unlocked; };
}
