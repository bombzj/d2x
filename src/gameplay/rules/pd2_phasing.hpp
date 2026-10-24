#pragma once
#include <cstdint>
namespace d2x {
// Current core 0x37EC90 / 0x373980: Blaze and Phasing change the
// dynamic path's movement test, while retaining the unit's own footprint.
constexpr bool pd2PhasingState(int state) {return state==13 || state==244;}
constexpr uint16_t pd2PhasingMovementMask(bool monster) {return monster?0x0c01:0x0c09;}
}
