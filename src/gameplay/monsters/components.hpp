#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <vector>
#include <map>

namespace d2x {
using MonsterComponents = std::array<uint8_t, 16>;
using MonsterComponentPalette = std::vector<MonsterComponents>;
// MonsterChoose::sub_6FC62020. The caller owns the region/unit random stream.
MonsterComponentPalette monsterComponentPalette(const MonsterComponents &counts, uint64_t &random);
MonsterComponents chooseMonsterComponents(const MonsterComponents &counts,
    const MonsterComponentPalette &palette, uint64_t &random);
int32_t rollMonsterLife(int minimum,int maximum,uint64_t &random);
MonsterComponents chooseRegionMonsterComponents(const MonsterComponents &counts,int nativeClass,int totalPieces,
    std::map<int,MonsterComponentPalette> &palettes,uint64_t &random);
void applyNecroSkeletonComponents(MonsterComponents &,int level,bool shield,std::optional<uint8_t> element={});
}
