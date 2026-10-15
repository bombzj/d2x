#pragma once
#include <utility>
#include <cstdint>
#include <map>
#include <span>

namespace d2x {
// MPQ supplies the base and per-level values; the caller supplies effective rank.
int skillRankBonus(std::pair<int, int> curve, int rank);
// Retail 1.13c D2Common RVA 4DC30, ordinal 10115: divide the level ratio first.
// The raw parameter function permits zero rank (dm78 and missile parameters).
int nativeSkillDiminishing(std::pair<int,int> bounds,int rank);
int skillDiminishingBonus(std::pair<int, int> bounds, int rank);
// Compatibility name; both positive-rank adapters use the same retail rule.
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
