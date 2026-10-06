#pragma once
#include "contracts/online.hpp"
#include "network/protocol/wire.hpp"
namespace d2x::net {
// Returns false for messages outside this domain. No MPQ, UI or gameplay authority.
bool apply_social_packet(OnlineView &, const protocol::Packet &);
}
