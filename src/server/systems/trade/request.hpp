#pragma once
#include "server/player_identity.hpp"
#include <optional>

namespace d2x::server::trade {
// Two-player offers reference items; final exchange uses one atomic transaction.
enum class Action { Invite, Accept, Cancel, Agree, Revoke, OfferGold };
struct Request { Action action; std::optional<PlayerId> peer; uint64_t revision{}; unsigned gold{}; };
}
