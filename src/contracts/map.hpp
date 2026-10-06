#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "world/identity.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace d2x {
struct AutomapStamp { int x = 0, y = 0, cel = -1; };
// Positions share the observer's world coordinate space; rendering has no
// dependency on either local authority or server packet coordinates.
struct AutomapDrawView {
    struct Stamp { Vec position; int cel = -1; };
    struct Town { int level = 0, variant = 0; Vec center; };
    struct Marker { Vec position; int cel = -1; std::string name; bool npc = false, showName = false; };
    Vec observer;
    std::vector<Stamp> stamps;
    std::vector<Town> towns;
    std::vector<Marker> markers;
};
struct MapRect { int x = 0, y = 0, width = 0, height = 0; };
struct MapMarkerView {
    Vec position;
    int objectClass = -1;
    std::string npcClass, name;
    bool showName = false;
};
struct MapRegionView {
    RegionId id = RegionId::Encampment;
    int width = 0, height = 0;
    bool safe = false;
    uint64_t layoutFingerprint = 0;
    std::vector<MapRect> revealRooms;
    std::vector<MapMarkerView> markers;
};
struct ExitView {
    int slot = 0;
    Vec position;
    MapRect selection;
    std::string name;
    bool enabled = false;
};
struct PortalView { Vec position; uint64_t revision = 0; bool cain = false; };
struct MapSceneView {
    uint64_t revision = 0;
    EntityId actor;
    RegionId region = RegionId::Encampment;
    int current = -1, act = 0, palette = 0;
    Vec observer;
    bool hasObserverRoom = false;
    std::vector<MapRegionView> regions;
    // Slot indices identify client caches only; offsets are in observer-region coordinates.
    std::vector<std::pair<int, Vec>> automapRegions;
    std::vector<ExitView> exits;
    std::vector<PortalView> portals;
    std::array<bool, 5> waypointActs{};
};
struct TravelEntryView {
    int level = 0;
    std::string name, status;
    std::optional<RegionId> destination;
};
struct TravelMenuView {
    uint64_t revision = 0;
    EntityId actor, source;
    int act = 0;
    std::vector<TravelEntryView> entries;
};
} // namespace d2x
