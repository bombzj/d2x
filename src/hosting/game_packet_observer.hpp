#pragma once
#include "core/bytes.hpp"
#include <functional>
#include <span>

namespace d2x::hosting {
enum class GamePacketDirection { ClientToServer, ServerToClient };
// Diagnostics only: borrowed native bytes, never an alternate command channel.
using GamePacketObserver = std::function<void(uint64_t, GamePacketDirection, std::span<const uint8_t>)>;
}
