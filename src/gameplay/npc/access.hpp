#pragma once
#include "core/id.hpp"
#include "world/identity.hpp"

namespace d2x {
// Prepared from authoritative state at the operation, not supplied by the UI.
struct NpcAccess {
    EntityId actor, npc, engagedNpc;
    RegionId region;
    bool alive = false, reachable = false, safe = false;
    bool contact() const { return actor && npc && engagedNpc == npc && alive && reachable; }
    bool townService() const { return contact() && safe; }
};
} // namespace d2x
