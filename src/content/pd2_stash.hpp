#pragma once
#include <array>
#include <string>
#include <string_view>

namespace d2x {
std::string pd2CnMaterialCode(std::string_view source);
// mdll1 SHA-256 6d6b8bf3...e90a, RVA 0x23190. Program layout, not item rules.
// Names, dimensions and artwork are resolved from the selected MPQ tables.
struct Pd2CnMaterialSlot { std::string_view code; int x, y; };
extern const std::array<Pd2CnMaterialSlot, 98> pd2CnMaterialSlots;
} // namespace d2x
