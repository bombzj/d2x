#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include <optional>
#include <string>

namespace d2x {
enum class RegionId;

// Public presentation data, not a copy of a character or a wire format.
struct ActorView {
    EntityId id;
    RegionId region{};
    Vec position, look;
    bool moving = false, dead = false, chilled = false, poisoned = false;
    std::string animationMode;
    float animationRate = 0;
    float animationSpeed = 1;
    std::optional<int> actionFrame;
    float movementSpeed = 0;
    int lightRadius = 0;
};

struct ActorControlIntent {
    Vec direction;
    bool forceRun = false;
};

struct MoveIntent {
    Vec destination;
};
} // namespace d2x
