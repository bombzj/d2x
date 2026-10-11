#pragma once
#include <utility>
#include <cstdint>
#include <map>
#include <span>

namespace d2x {
// MPQ supplies the base and per-level values; the caller supplies effective rank.
int skillRankBonus(std::pair<int, int> curve, int rank);
// D2Common_11033: dmNN diminishing parameter curve.
int skillDiminishingBonus(std::pair<int, int> bounds, int rank);
// Some existing MPQ programs truncate the level ratio before scaling the range.
// Keep this operation separate from D2MOO's product-first 11033 grouping.
int skillDiminishingRatioBonus(std::pair<int, int> bounds, int rank);
int skillRank(const std::map<int, int> &ranks, int skill);
int64_t skillRankSum(std::span<const int> skills, const std::map<int, int> &ranks);
int64_t skillSynergyBonus(std::span<const std::pair<int, int>> synergies,
                        const std::map<int, int> &ranks);
int64_t skillSynergyBonus(const std::map<int, int> &synergies,
                        const std::map<int, int> &ranks);
struct ShieldAbsorption {
    float damage, mana;
};
ShieldAbsorption absorbSkillShield(float damage, float mana, int percent, int manaFactor);
} // namespace d2x
