#pragma once
#include "contracts/online_world.hpp"

namespace d2x {
struct OnlineSceneView {
    bool available{}, movementAvailable{}, collisionVerified{};
    std::string reason{"Waiting for server world data"}, map;
    std::optional<OnlinePoint> origin;
    int width{}, height{}, candidates{}, landmarks{};
    int renderedUnits{}, unavailableUnits{};
    bool playerDisplayed{};
};
} // namespace d2x
