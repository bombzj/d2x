#pragma once
#include <utility>

namespace d2x {
// MPQ supplies the base and per-level values; the caller supplies effective rank.
int skillRankBonus(std::pair<int, int> curve, int rank);
// D2Common_11033: dmNN diminishing parameter curve.
int skillDiminishingBonus(std::pair<int, int> bounds, int rank);
struct ShieldAbsorption {
    float damage, mana;
};
ShieldAbsorption absorbSkillShield(float damage, float mana, int percent, int manaFactor);
} // namespace d2x
