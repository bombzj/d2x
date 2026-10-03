#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace d2x {
// Immutable definition facts; indices identify content property instructions.
struct EquipmentSetBonus {
    int pieces = 0;
    bool perItem = false, fixedValue = false;
    size_t instruction = 0;
};
struct EquipmentSetPiece {
    int32_t row = -1;
    std::string set;
    int addFunction = 0, fullPieces = 0;
    size_t instruction = 0;
    std::vector<EquipmentSetBonus> bonuses;
};
} // namespace d2x
