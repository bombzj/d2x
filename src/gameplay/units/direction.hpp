#pragma once
#include <cstdint>
namespace d2x {
struct NativeDirection {int32_t x{},y{};uint8_t index{};};
// PATH_GetDirectionVector's 4096-scale vector and 64-way facing.
NativeDirection nativeDirectionVector(int64_t deltaX,int64_t deltaY);
// D2Common ordinal 11053: the eight directional ProgOverlay slots.
constexpr uint8_t nativeOverlayDirection(uint8_t direction) {return uint8_t(((direction+4)&63)/8);}
}
