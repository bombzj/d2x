#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/items/handle.hpp"
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
    enum class Action { Cast, Interact, Pickup } action = Action::Cast;
    Vec point;
    EntityId target;
    std::optional<Vec> displayOrigin; // Interaction projection hint, never a native position.
    std::optional<ItemHandle> item;
    bool right = false, stationary = false, repeat = false, toCursor = false, forceRun = false;
};

struct MoveIntent {
    Vec destination;
    std::optional<Vec> displayOrigin; // Projection hint; never replaces authoritative position.
    bool forceRun = false;
};
} // namespace d2x
