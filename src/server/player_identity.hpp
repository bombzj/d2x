#pragma once
#include <compare>
#include <cstdint>

namespace d2x {
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
} // namespace d2x
