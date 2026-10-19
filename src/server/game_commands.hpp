#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "server/player_identity.hpp"
#include "world/interaction_geometry.hpp"
#include <cstdint>
#include <optional>

namespace d2x {
enum class MovementAction { Move, Stop, ToggleRunning, ApproachExit, ApproachCorpse, ApproachObject, ApproachNpc, Synchronize, ApproachItem };
struct MotionTarget { EntityId id; uint8_t type{1}; };
struct MovementCommand {
    MovementAction action = MovementAction::Move;
    Vec destination;
    bool forceRun{};
    EntityId exit{};
    std::optional<MotionTarget> target{};
    std::optional<InteractionTarget> arrival{};
};
enum class CommandStatus { Queued, Applied, NotImplemented, InvalidBinding, Stale, Paused, QueueFull, InvalidDestination, NoRoute, InvalidRequest, Unavailable, Conflict };
struct CommandResult {
    uint64_t sequence{};
    CommandStatus status = CommandStatus::Stale;
};
} // namespace d2x
