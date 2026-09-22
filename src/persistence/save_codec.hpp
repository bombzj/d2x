#pragma once
#include "core/bytes.hpp"
#include "gameplay/session_snapshot.hpp"
#include <span>

namespace d2x {
inline constexpr size_t maxSaveBytes = 16 * 1024 * 1024;
Bytes encodeSave(SessionSnapshot snapshot);
SessionSnapshot decodeSave(std::span<const uint8_t> bytes);
} // namespace d2x
