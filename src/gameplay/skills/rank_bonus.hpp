#pragma once
#include <utility>

namespace d2x {
// MPQ supplies the base and per-level values; the caller supplies effective rank.
int skillRankBonus(std::pair<int, int> curve, int rank);
struct ShieldAbsorption {
    float damage, mana;
};
ShieldAbsorption absorbSkillShield(float damage, float mana, int percent, int manaFactor);
} // namespace d2x
