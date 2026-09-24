#pragma once
#include "archive.hpp"
#include <array>

namespace d2x {
// MonStats.TransLvl selects one of the five usable monster color maps.
std::array<uint8_t, 256> monsterPalshift(const Bytes &data, int transLevel);
}
