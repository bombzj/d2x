#include "shape_timing.hpp"
#include "timing.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace d2x {
uint8_t shapeMonsterMode(uint8_t playerMode,uint16_t enabledModes) {
    constexpr std::array<uint8_t,20> modes{0,1,2,15,3,1,2,4,5,6,7,4,11,8,9,10,11,12,14,13};
    if(playerMode>=modes.size()) throw std::invalid_argument("Invalid native player mode conversion");
    auto mode=modes[playerMode];
    for(;;) {
        if(enabledModes&(uint16_t{1}<<mode)) return mode;
        switch(mode) {
        case 1:return 1;
        case 5:case 7:mode=4;break;
        case 6:mode=3;break;
        case 9:case 10:case 11:mode=8;break;
        case 15:mode=2;break;
        default:mode=1;break;
        }
    }
}
int shapeAttackSpeed(int shapeFrames,int shapeSpeed,int humanFrames,int humanSpeed,
    int itemIAS,int weaponSpeed,int skillRate,int weaponItemSpeed,int weaponItemIAS,bool equipped) {
    if(shapeFrames<1 || shapeSpeed<1 || (equipped && (humanFrames<1 || humanSpeed<1)))
        throw std::invalid_argument("Missing native shape animation metadata");
    int period=19;
    if(equipped) {
        const auto rate=nativeAdd(nativeSubtract(100,weaponItemSpeed),weaponItemIAS);
        const auto speed=nativeMultiplyDivide(humanSpeed,rate,100);
        period=speed>0?nativeDivide(nativeShiftLeft(humanFrames,8),speed):0;
    }
    const auto base=period>0?nativeDivide(nativeShiftLeft(shapeFrames,8),period):0;
    return effectiveAttackSpeed(base?base:shapeSpeed,itemIAS,weaponSpeed,skillRate);
}
}
