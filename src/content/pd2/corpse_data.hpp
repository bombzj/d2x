#pragma once
#include <array>
#include <vector>

namespace d2x {
struct ClassicData;
// First entry is the target area; the remaining entries are Do161's fallback
// Levels records. Preserve all 25 slots for the native rejection draws.
std::vector<std::array<int,25>> preparePd2CorpsePools(const ClassicData &,int level,int difficulty);
}
