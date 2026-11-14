#pragma once
#include <cstdint>
#include <optional>
#include <set>
#include <vector>

namespace d2x {
// Client room lifetimes published together with a native map snapshot.
// No live generator references: a copied Map retains the same immutable view.
struct RoomActivationSnapshot {
    struct Room {
        uint64_t serial{};
        std::optional<uint64_t> initialRandom;
    };
    // Same indices as MapTerrain::rooms / Map::activation: only the loaded
    // rooms queryable in this snapshot's walking component.
    std::vector<Room> rooms;
    // Whole-act lifetimes, including rooms outside that component. Leaving
    // the component alone must not retire or reseed a client's critters.
    std::set<uint64_t> activeSerials;
};
} // namespace d2x
