#pragma once
#include "server/game_messages.hpp"
#include "server/player_state.hpp"

namespace d2x::server {
PlayerSnapshot projectPlayer(const PlayerState &);
} // namespace d2x::server
