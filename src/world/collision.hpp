#pragma once
#include <cstdint>
#include <cstdlib>

namespace d2x {
constexpr bool nativeCollisionSizeCell(int size,int dx,int dy) {
    if(size<0 || size>3 || dx<-1 || dx>1 || dy<-1 || dy>1) return false;
    return size<=1?dx==0 && dy==0:size==2?!dx || !dy:true;
}
constexpr uint16_t nativeMissileContactMask(int mode) {
    switch(mode) {
    case 1:return 0x84;
    case 2:case 5:return 0x104;
    case 3:return 0x184;
    case 6:return 4;
    case 7:return 0x40;
    case 8:return 0x185;
    default:return 0;
    }
}
// D2MOO 1.10f #10407 / retail 1.13c #10389 uses this discrete pair table
// after the mask gate. It differs from SetMaskWithPattern's unit footprint.
inline bool nativeMissileUnitOverlap(int missileSize,int unitSize,int dx,int dy) {
    if(missileSize<1 || missileSize>3 || unitSize<1) return false;
    if(unitSize>3) unitSize=3;
    const int x=std::abs(dx),y=std::abs(dy);
    switch(unitSize+3*(missileSize-1)) {
    case 1:return !x && !y;
    case 2:case 4:return x+y<=1;
    case 5:return x+y<=2;
    case 3:case 7:return x<=1 && y<=1;
    case 6:case 8:return (x<=2 && y<=1) || (y<=2 && x<=1);
    case 9:return x<=2 && y<=2;
    default:return false;
    }
}
enum class NativeCollisionPattern : uint8_t { None,SmallUnit,BigUnit,SmallPet,BigPet,SmallNoPresence };
constexpr NativeCollisionPattern nativeUnitCollisionPattern(int size,bool pet=false) {
    if(size==1 || size==2) return pet?NativeCollisionPattern::SmallPet:NativeCollisionPattern::SmallUnit;
    return size==3?(pet?NativeCollisionPattern::BigPet:NativeCollisionPattern::BigUnit):NativeCollisionPattern::None;
}
inline uint16_t nativeCollisionFootprint(NativeCollisionPattern pattern,int dx,int dy,uint16_t mask) {
    if(!mask || pattern==NativeCollisionPattern::None || std::abs(dx)>1 || std::abs(dy)>1) return 0;
    const bool big=pattern==NativeCollisionPattern::BigUnit || pattern==NativeCollisionPattern::BigPet;
    const bool cross=!dx || !dy;
    uint16_t result=big || cross?mask:0;
    if((big && cross) || (!dx && !dy)) {
        if(pattern==NativeCollisionPattern::SmallUnit || pattern==NativeCollisionPattern::BigUnit) result|=0x1000;
        if(pattern==NativeCollisionPattern::SmallPet || pattern==NativeCollisionPattern::BigPet) result|=0x2000;
    }
    return result;
}
struct MissileCollisionRule {
    uint16_t mask = 0;
    int size = 0;
};
struct MovementCollisionRule {
    uint16_t mask = 0x1c09; // WALL | NOPLAYER | OBJECT | DOOR | NO_PATH.
    int size = 1; // Point for spatial queries; dynamic path patterns use cross/square.
};
inline constexpr MovementCollisionRule playerMovement{0x1c09, 2};
} // namespace d2x
