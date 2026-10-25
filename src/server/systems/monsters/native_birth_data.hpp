#pragma once
#include "native_birth.hpp"
#include "system.hpp"
#include "server/area_store.hpp"

namespace d2x::server::monsters {
// Domain-private preparation state shared by the native and PD2 birth paths.
struct NativeBirthPublication::Data {
    System *system{};
    RegionId area;uint64_t generation{};
    std::optional<RoomRandomLoan> room;
    std::optional<RoomRandomLoan> sourceRoom;
    uint64_t randomExpected{},randomNext{};
    ActorPublication actors;
    ComponentPaletteState palettes;
    EntityId actor;
    std::optional<Vec> position;
    bool published{};
};
}
