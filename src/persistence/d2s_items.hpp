#pragma once
#include "content/classic_data.hpp"
#include "d2s_stats.hpp"
#include "gameplay/items/native_identity.hpp"
#include <span>

namespace d2x {
D2sItemRead readD2sItem(std::span<const uint8_t> bytes, const ClassicData &content);
Bytes writeD2sItem(const D2sItem &item, const ClassicData &content);
} // namespace d2x
