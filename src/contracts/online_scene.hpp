#pragma once
#include "contracts/online_world.hpp"
#include <vector>

namespace d2x {
struct OnlineSceneView {
    bool available{}, movementAvailable{}, collisionVerified{};
    std::string reason{"Waiting for server world data"}, map;
    std::optional<OnlinePoint> origin;
    int width{}, height{}, candidates{}, landmarks{};
    int renderedUnits{}, unavailableUnits{};
    bool playerDisplayed{};
    std::optional<uint16_t> area;
    std::optional<OnlinePoint> layoutOrigin;
    std::string layoutReason;
    bool layoutMatched{};
    bool nativeMapReady{};
    std::string nativeMapReason;
    std::vector<uint16_t> cachedAreas;
    std::map<int, std::string> mapErrors;
};
} // namespace d2x
