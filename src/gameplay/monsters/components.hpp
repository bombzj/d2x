#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace d2x {
using MonsterComponents = std::array<uint8_t, 16>;
using MonsterComponentPalette = std::vector<MonsterComponents>;
// MonsterChoose::sub_6FC62020. The caller owns the region/unit random stream.
MonsterComponentPalette monsterComponentPalette(const MonsterComponents &counts, uint64_t &random);
MonsterComponents chooseMonsterComponents(const MonsterComponents &counts,
    const MonsterComponentPalette &palette, uint64_t &random);
}
