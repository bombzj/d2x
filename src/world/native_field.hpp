#pragma once
#include "core/bytes.hpp"
#include <array>
#include <cstdint>
#include <cstddef>

namespace d2x {
// Prepared DATA/GLOBAL/expfield.d2. The native lookup has a 256-byte stride.
struct CollisionField {int width{},height{};Bytes directions;};
template<class Mask> bool nativeFieldBlocked(const CollisionField &field,int originX,int originY,int x,int y,uint16_t mask,const Mask &sample) {
    const auto direction=[&](int column,int row)->int {
        const auto atX=int64_t(field.width/2)+column-originX,atY=int64_t(field.height/2)+row-originY;
        if(atX<0 || atY<0 || atX>=field.width || atY>=field.height) return -1;
        return field.directions[size_t(atY)*256+size_t(atX)];
    };
    if(sample(x,y)&mask) return true;
    constexpr std::array<int,9> dx{0,1,1,1,0,-1,-1,-1,0},dy{-1,-1,0,1,1,1,0,-1,0};
    // D2Common_11098 steps through the imported field. 11099 deliberately
    // excludes its final center cell unless the initial candidate was there.
    for(size_t step=0;step<field.directions.size();++step) {
        const auto next=direction(x,y);if(next<0 || next>8) return true;
        x+=dx[size_t(next)];y+=dy[size_t(next)];
        const auto after=direction(x,y);if(after<0) return true;if(after==8) return false;
        if(sample(x,y)&mask) return true;
    }
    return true;
}
}
