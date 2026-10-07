#pragma once
#include "world/navigation.hpp"
#include "world/identity.hpp"
#include <map>
#include <stdexcept>
#include <utility>

namespace d2x::server {
namespace world { class System; }
// Prepared collision values only. No Map, DT1, Archives or renderer handles.
struct AreaDefinition {
    RegionId id = RegionId::Encampment;
    Grid collision;
    Vec spawn;
};
struct AreaState {
    AreaDefinition definition;
    uint64_t generation = 1;
};
class AreaStore {
    friend class world::System;
    std::map<RegionId, AreaState> areas_;
  public:
    explicit AreaStore(AreaDefinition initial) {
        const auto &grid = initial.collision;
        if (grid.width <= 0 || grid.height <= 0)
            throw std::runtime_error("Empty prepared authority area");
        const auto count = size_t(grid.width) * size_t(grid.height);
        if (grid.blocked.size() != count || grid.lightBlocked.size() != count ||
            grid.terrainCollision.size() != count || grid.objectCollision.size() != count ||
            grid.objectLightBlocked.size() != count ||
            (!grid.fullTerrainCollision.empty() && grid.fullTerrainCollision.size() != count))
            throw std::runtime_error("Incomplete prepared collision grid");
        // Cross-area borrows must be installed by this store, never imported.
        if (!initial.collision.neighbours.empty() ||
            !initial.collision.walkable(initial.spawn, playerMovement))
            throw std::runtime_error("Invalid prepared authority area");
        const auto id = initial.id;
        areas_.emplace(id, AreaState{std::move(initial)});
    }
    const AreaState &at(RegionId id) const { return areas_.at(id); }
};
} // namespace d2x::server
