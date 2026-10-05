#pragma once
#include "contracts/online.hpp"
#include "network/protocol/wire.hpp"

namespace d2x::net {
// Ordered server replica. No local simulation, MPQ, save data, GPU or device input.
void apply_world_packet(OnlineView &, const protocol::Packet &);
} // namespace d2x::net
