#pragma once
#include "server/runtime/combat_rules.hpp"
#include "server/runtime/object_rules.hpp"
#include "server/runtime/npc_rules.hpp"
#include "world/navigation.hpp"
#include "world/identity.hpp"
#include "gameplay/monsters/components.hpp"
#include <map>
#include <stdexcept>
#include <utility>
#include <array>
#include <compare>

namespace d2x::server {
namespace world { class System; }
struct RoomView {
    RegionId area;
    int x{}, y{}; // Global game-tile anchor, the original 07/08 identity.
    auto operator<=>(const RoomView &) const = default;
};
// Prepared collision values only. No Map, DT1, Archives or renderer handles.
struct AreaExit { EntityId id; RegionId destination; int warp{}, slot{}; Vec position, arrival; bool requiresQuest{}; Vec exitWalk; };
struct AreaBoundary { RegionId destination; int side{}, plane{}, start{}, end{}; };
struct AreaObject { EntityId id; int type{}; Vec position; ObjectRule rule; bool questSpawn{}; };
struct PortalRule { int definition{},range{},openingTicks{}; };
struct TerrainCell { size_t index{}; uint8_t blocked{},light{},terrain{}; uint16_t full{}; };
struct AreaMetadata {
    int waypointIndex = -1;
    std::optional<Vec> waypointAnchor; // Native spawn marker, resolved against current collision at travel commit.
    std::optional<Vec> portalArrival;
    std::optional<Vec> questArrival; // Original quest object warp spawn (tile 0/11).
    std::optional<PortalRule> portalRule, specialPortalRule;
    std::map<int,PortalRule> questPortalRules;
    std::map<int,uint16_t> questObjectMessages;
    RegionId id = RegionId::Encampment;
    Vec spawn, origin;
    RegionId townRegion{};
    int act{};
    std::optional<int> staffTomb;
    std::vector<TerrainCell> openedTombWall;
    unsigned tombOpeningTicks{};
    bool town{}, teleportAllowed{};
    std::vector<AreaExit> exits;
    std::vector<AreaBoundary> boundaries;
    std::vector<AreaObject> objects;
    std::vector<AreaNpc> npcs;
    std::vector<int> objectDeferred; // Native preset callbacks awaiting object/quest authority.
};
struct AreaDefinition : AreaMetadata {
    std::map<std::string,PreparedMonster,std::less<>> environmentMonsters;
    std::map<std::string,std::vector<PreparedMonster>,std::less<>> environmentGroups;
    std::vector<std::string> populationRoster;
    std::map<int,ShrineRule> shrineRules;
    std::map<int,ObjectRule> objectRules;
    Grid collision;
    RoomLayout activation;
    std::vector<std::vector<RoomView>> roomNear; // Authored DRLG order, including cross-level links.
    std::vector<PreparedMonster> population;
    std::map<std::string,std::vector<PreparedMonster>,std::less<>> questGroups;
    std::map<std::string,NpcRule,std::less<>> questNpcs;
    std::map<int, MonsterComponentPalette> componentPalettes;
    std::vector<std::string> populationDeferred;
    size_t populationMissing{};
    std::vector<Vec> populationMissingPositions;
};
struct AreaView { AreaMetadata definition; uint64_t generation{}; std::vector<std::string> populationDeferred; size_t populationMissing{}; };
struct AreaState {
    AreaDefinition definition;
    uint64_t generation = 1;
    struct RoomState {
        std::array<uint16_t, 4> references{};
        uint8_t status{4};
        bool loaded{};
    };
    std::vector<RoomState> rooms;
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
            grid.fullTerrainCollision.size() != count)
            throw std::runtime_error("Incomplete prepared collision grid");
        if (initial.activation.rooms().empty() || initial.roomNear.size() != initial.activation.rooms().size())
            throw std::runtime_error("Missing original room topology");
        for (const auto &near : initial.roomNear)
            if (near.empty()) throw std::runtime_error("Missing original near-room list");
        // Cross-area borrows must be installed by this store, never imported.
        if (!initial.collision.neighbours.empty() || !initial.collision.navigationSources.empty() ||
            (initial.town && !initial.collision.walkable(initial.spawn, playerMovement)))
            throw std::runtime_error("Invalid prepared authority area");
        if (int(initial.id) < 1 || int(initial.id) > 255 || initial.act < 0 || initial.act > 4)
            throw std::runtime_error("Invalid native area identity");
        for(const auto &cell:initial.openedTombWall) if(cell.index>=count)
            throw std::runtime_error("Invalid prepared tomb collision patch");
    }
    explicit AreaStore(AreaDefinition initial) {
        validate(initial);
        const auto id = initial.id;
        areas_.emplace(id, AreaState{std::move(initial), 1, {}});
    }
    const AreaState *find(RegionId id) const {
        const auto found = areas_.find(id); return found == areas_.end() ? nullptr : &found->second;
    }
    const auto &all() const { return areas_; }
    const AreaState &at(RegionId id) const { return areas_.at(id); }
    bool loaded(RegionId, Vec) const;
    bool active(RegionId, Vec) const;
    bool nearby(RegionId observerArea, Vec observer, RegionId targetArea, Vec target) const;
};
} // namespace d2x::server
