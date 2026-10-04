#pragma once
#include "core/id.hpp"
#include "world/identity.hpp"
#include <cstdint>
#include <variant>

namespace d2x {
struct Travel {
    RegionId destination;
};
struct WaypointTravel {
    EntityId source;
    RegionId destination;
};
struct UseExit {
    int slot = 0;
};
struct UseTownPortal {
    uint64_t revision;
};
struct UseCainPortal {};
using MapIntent = std::variant<WaypointTravel, UseExit, UseTownPortal, UseCainPortal>;
} // namespace d2x
