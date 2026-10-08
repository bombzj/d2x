#pragma once
#include "server/runtime/combat_rules.hpp"
#include "world/navigation.hpp"
#include "world/identity.hpp"
#include <map>
#include <stdexcept>
#include <utility>

namespace d2x::server {
namespace world { class System; }
// Prepared collision values only. No Map, DT1, Archives or renderer handles.
struct AreaExit { EntityId id; RegionId destination; int warp{}, slot{}; Vec position, arrival; bool requiresQuest{}; Vec exitWalk; };
struct AreaBoundary { RegionId destination; int side{}, plane{}, start{}, end{}; };
struct AreaObject { EntityId id; int type{}; Vec position; };
struct AreaMetadata {
    RegionId id = RegionId::Encampment;
    Vec spawn, origin;
    int act{};
    bool town{}, teleportAllowed{};
    std::vector<AreaExit> exits;
    std::vector<AreaBoundary> boundaries;
    std::vector<AreaObject> objects;
    std::vector<int> objectDeferred; // Native preset callbacks awaiting object/quest authority.
};
struct AreaDefinition : AreaMetadata {
    Grid collision;
    RoomLayout activation;
    std::vector<PreparedMonster> population;
    std::vector<std::string> populationDeferred;
};
struct AreaView { AreaMetadata definition; uint64_t generation{}; };
struct AreaState {
    AreaDefinition definition;
    uint64_t generation = 1;
};
class AreaStore {
    friend class world::System;
    std::map<RegionId, AreaState> areas_;
  public:
    static void validate(const AreaDefinition &initial) {
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
            (initial.town && !initial.collision.walkable(initial.spawn, playerMovement)))
            throw std::runtime_error("Invalid prepared authority area");
        if (int(initial.id) < 1 || int(initial.id) > 255 || initial.act < 0 || initial.act > 4)
            throw std::runtime_error("Invalid native area identity");
    }
    explicit AreaStore(AreaDefinition initial) {
        validate(initial);
        const auto id = initial.id;
        areas_.emplace(id, AreaState{std::move(initial)});
    }
    const AreaState *find(RegionId id) const {
        const auto found = areas_.find(id); return found == areas_.end() ? nullptr : &found->second;
    }
    const auto &all() const { return areas_; }
    const AreaState &at(RegionId id) const { return areas_.at(id); }
};
} // namespace d2x::server
