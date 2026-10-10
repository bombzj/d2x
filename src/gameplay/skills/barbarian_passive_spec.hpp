#pragma once
#include <algorithm>
#include <array>
#include <string>

namespace d2x {
// Native dmNN rounds the level ratio before applying the parameter range.
inline int barbarianDiminishing(int minimum, int maximum, int rank) {
    if (rank <= 0) return 0;
    return std::min(maximum, minimum + (maximum - minimum) * (110 * rank / (rank + 6)) / 100);
}
struct BarbarianPassiveSpec {
    enum class Kind { Mastery, Stamina, Speed, Defense, Resistance };
    Kind kind = Kind::Mastery;
    std::string weaponType;
    bool thrown = false;
    std::array<int, 6> parameters{};
};
}
