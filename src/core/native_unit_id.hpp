#pragma once
#include <compare>
#include <cstdint>

namespace d2x {
enum class NativeUnitType : uint8_t {Player,Monster,Object,Missile,Item,Tile};
struct NativeUnitId {
    NativeUnitType type{};
    uint32_t guid{};
    auto operator<=>(const NativeUnitId &) const = default;
};
}
