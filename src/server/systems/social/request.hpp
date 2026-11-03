#pragma once
#include "server/player_identity.hpp"
#include <optional>
#include <string>

namespace d2x::server::social {
// Player relationships, party membership and chat routing; no wire encoding.
enum class Action { Chat, Invite, CancelInvite, Accept, LeaveParty, Hostility, Ignore, Squelch, AllowLoot, Voice };
struct Request { Action action; std::optional<PlayerId> target; std::string text; std::string receiver{}; bool overhead{}; uint8_t language{}; bool enabled{}; uint16_t sound{}; };
}
