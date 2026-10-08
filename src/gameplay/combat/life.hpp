#pragma once
#include <algorithm>
#include <cstdint>

namespace d2x {
// D2Common::UNITS_GetCurrentLifePercentage truncates fixed-point values
// before calculating the player's 0..100 wire percentage.
inline uint8_t playerLifePercentage(int64_t life, int64_t maximum) {
    const auto hp=life/256, maxHp=maximum/256;
    return maxHp>0?uint8_t(std::clamp<int64_t>(100*hp/maxHp,0,100)):0;
}
// D2Game::MonsterMode::sub_6FC62F50 uses 0..128. Assignment/motion use
// the raw ratio; MonsterMsg decrements it for the rank-bit-bearing 0C update.
inline uint8_t monsterLifeRatio(int64_t life, int64_t maximum) {
    const auto hp=life/256, maxHp=maximum/256;
    return maxHp>0 && hp<maxHp?uint8_t(std::clamp<int64_t>(128*hp/maxHp,0,128)):128;
}
}
