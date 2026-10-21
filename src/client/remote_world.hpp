#pragma once
#include "contracts/online.hpp"
#include "network/protocol/wire.hpp"
#include "network/protocol/client_profile.hpp"

namespace d2x::net {
// Ordered server replica. No local simulation, MPQ, save data, GPU or device input.
void apply_world_packet(OnlineView &, const protocol::Packet &, protocol::ClientProfile = protocol::ClientProfile::Lod113c,
                        OnlineStashLayout = {});
} // namespace d2x::net
