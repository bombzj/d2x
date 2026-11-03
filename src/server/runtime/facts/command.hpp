#pragma once
#include "server/player_identity.hpp"
#include "server/game_commands.hpp"
#include <cstdint>

namespace d2x::server {
struct CommandFact { PlayerId player; uint64_t sequence{}; CommandStatus result; };
}
