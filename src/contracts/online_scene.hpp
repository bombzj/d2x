#pragma once
#include "contracts/online_world.hpp"
#include <vector>

namespace d2x {
enum class OnlineMapInteraction { Exit, Door, Portal, TeleportPad, Waypoint, Npc, Stash, Corpse };
struct OnlineMapTarget {
    OnlineUnitKey unit;
    OnlinePoint position;
    OnlineMapInteraction interaction{};
    std::string name;
    std::optional<uint16_t> destination;
    int collisionWidth{}, collisionHeight{};
};
struct OnlineWaypointDestination {
    uint16_t level{};
    uint8_t act{}, number{};
    std::string name;
    bool unlocked{}, current{};
};
struct OnlineAutomapStamp {
    uint16_t level{}, tileX{}, tileY{}; // Global tile coordinates, independent of snapshot origin.
    int cel{};
};
struct OnlineAutomapTown {
    uint16_t level{};
    int variant{};
    OnlinePoint center;
};
struct OnlineNpcText {
    uint16_t stringId{};
    uint8_t menu{};
    std::string text;
    bool acknowledged{};
};
struct OnlineNpcDialogView {
    uint32_t source{};
    uint64_t revision{};
    OnlinePoint position;
    std::string speaker, travelLabel;
    std::vector<OnlineNpcText> messages;
};
struct OnlineSceneView {
    bool available{}, movementAvailable{}, collisionVerified{};
    std::string reason{"Waiting for server world data"}, map;
    std::optional<OnlinePoint> origin;
    int width{}, height{}, candidates{}, landmarks{};
    int renderedUnits{}, unavailableUnits{};
    std::vector<std::string> effectLimitations;
    bool playerDisplayed{};
    std::optional<uint16_t> area;
    std::optional<uint8_t> palette;
    std::optional<OnlinePoint> layoutOrigin;
    std::string layoutReason;
    bool layoutMatched{};
    bool nativeMapReady{};
    std::string nativeMapReason;
    std::vector<uint16_t> cachedAreas;
    std::map<int, std::string> mapErrors;
    std::vector<OnlineMapTarget> mapTargets;
    std::vector<OnlineWaypointDestination> waypoints;
    std::vector<OnlineAutomapStamp> automapStamps;
    std::vector<OnlineAutomapTown> automapTowns;
    std::map<uint16_t, size_t> automapRevealedCells;
    bool automapVisible{}, automapLarge{true};
    bool town{};
    std::vector<uint16_t> townPortalSkills;
    std::optional<OnlineNpcDialogView> npcConversation;
};
} // namespace d2x
