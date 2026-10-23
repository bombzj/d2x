#pragma once
#include "contracts/online.hpp"
#include "network/protocol/wire.hpp"
#include "network/protocol/client_profile.hpp"
#include <cstdint>
#include <optional>

namespace d2x::net {
// Ordered server replica. No local simulation, MPQ, save data, GPU or device input.
void apply_world_packet(OnlineView &, const protocol::Packet &, protocol::ClientProfile = protocol::ClientProfile::Lod113c,
                        const OnlineStashLayout & = {}, std::optional<uint16_t> playerDefaultSkill = {});
void invalidateCubeHostPd2(OnlineView &);
} // namespace d2x::net
