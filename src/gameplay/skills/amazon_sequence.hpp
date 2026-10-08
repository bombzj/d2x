#pragma once
#include "spear_spec.hpp"
#include <string_view>
namespace d2x {
// D2Common SequenceTbls: seq 1 (Jab) and seq 8 (Impale).
inline SpearSequence amazonWeaponSequence(int sequence, std::string_view weapon) {
    SpearSequence result;
    if (weapon != "1ht" && weapon != "2ht") return result;
    const bool two = weapon == "2ht";
    if (sequence == 1) {
        constexpr int one[]{5,6,8,9,10,11,13,6,8,9,10,11,13,6,8,9,10,13};
        constexpr int both[]{2,7,9,10,12,13,15,4,6,9,10,12,13,15,4,6,9,10,11,13,15};
        for (int i=0; i<(two?21:18); ++i)
            result.frames.push_back({two?both[i]:one[i],two?((i>=7 && i<=10)||i>=14):((i>=7 && i<=9)||i>=13),two?(i==3||i==10||i==17):(i==3||i==9||i==15)});
    } else if (sequence == 8) {
        constexpr int frames[]{0,1,1,1,2,2,2,3,3,4,4,5,6,7,8,9,10,11,12,13,14,15,16,17};
        for(int i=0;i<(two?24:21);++i) result.frames.push_back({frames[i],false,i==(two?15:13)});
    }
    return result;
}
inline int weaponSequenceTick(int frame, int speed) { return std::max(1,(frame*256+speed-1)/speed); }
}
