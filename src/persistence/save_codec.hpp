#pragma once
#include "core/bytes.hpp"
#include "gameplay/session/session_snapshot.hpp"
#include "content/classic_data.hpp"
#include <span>

namespace d2x {
inline constexpr size_t maxSaveBytes = 16 * 1024 * 1024;
Bytes encodeSave(const SessionSnapshot &snapshot, const ClassicData &content);
SessionSnapshot decodeSave(std::span<const uint8_t> bytes, const ClassicData &content);
} // namespace d2x
