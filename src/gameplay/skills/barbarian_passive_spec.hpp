#pragma once
#include <array>
#include <string>

namespace d2x {
struct BarbarianPassiveSpec {
    enum class Kind { Mastery, Stamina, Speed, Defense, Resistance };
    Kind kind = Kind::Mastery;
    std::string weaponType;
    bool thrown = false;
    std::array<int, 6> parameters{};
};
struct BarbarianPassiveValues {
    int attackRating = 0, damagePercent = 0, criticalChance = 0, amount = 0;
};
BarbarianPassiveValues evaluateBarbarianPassive(const BarbarianPassiveSpec &, int rank);
}
