#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "world/identity.hpp"
#include <compare>
#include <cstdint>

namespace d2x {
// Internal authority/host values. These are not client messages or wire structs.
struct GameHandle {
    uint64_t slot{}, generation{};
    auto operator<=>(const GameHandle &) const = default;
};
struct PlayerId {
    uint64_t value{};
    auto operator<=>(const PlayerId &) const = default;
};
// Issued by the host. Transports bind this to a connection, never trust a
// player or game identity supplied in a command payload.
struct PlayerBinding {
    GameHandle game;
    PlayerId player;
};
enum class MovementAction { Move, Stop, ToggleRunning };
struct MovementCommand {
    MovementAction action = MovementAction::Move;
    Vec destination;
    bool forceRun{};
};
enum class CommandStatus { Queued, Applied, NotImplemented, InvalidBinding, Stale, Paused, QueueFull, InvalidDestination, NoRoute, InvalidRequest, Unavailable, Conflict };
struct CommandResult {
    uint64_t sequence{};
    CommandStatus status = CommandStatus::Stale;
};
struct PlayerSnapshot {
    GameHandle game;
    PlayerId recipient;
    uint64_t tick{}, revision{}, areaGeneration{};
    uint64_t rulesFingerprint{};
    struct Motion {
        EntityId id;
        RegionId region{};
        Vec position, look, nextPosition;
        bool moving{}, running{};
        float movementSpeed{};
    } actor;
    // Coalesced diagnostic result, not a reliable transaction acknowledgement.
    CommandResult command;
    uint64_t movementSequence{};
    bool paused{};
};
} // namespace d2x
