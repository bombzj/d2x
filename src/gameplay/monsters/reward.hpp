#pragma once

namespace d2x {
// Authority-side snapshot made at death, independent of live combat effects.
struct MonsterRewardModifiers {
    int levelBonus = 0;
    int experienceFactor = 1;
};
} // namespace d2x
