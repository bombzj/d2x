#include "gameplay/skills/rank_bonus.hpp"
#include "gameplay/combat/shield.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace d2x {
int skillRankBonus(std::pair<int, int> curve, int rank) {
    return rank > 0 ? nativeAdd(curve.first,nativeMultiply(rank-1,curve.second)) : 0;
}
int nativeSkillDiminishing(std::pair<int,int> bounds,int rank) {
    const auto [minimum, maximum] = bounds;
    const auto ratio=nativeMultiplyDivide(110,rank,nativeAdd(rank,6));
    return std::min(maximum,nativeAdd(minimum,nativeMultiplyDivide(nativeSubtract(maximum,minimum),ratio,100)));
}
int skillDiminishingBonus(std::pair<int, int> bounds, int rank) {
    return rank>0?nativeSkillDiminishing(bounds,rank):0;
}
int skillDiminishingRatioBonus(std::pair<int, int> bounds, int rank) {
    return skillDiminishingBonus(bounds,rank);
}
int skillRank(const std::map<int, int> &ranks, int skill) {
    const auto found = ranks.find(skill);
    return found == ranks.end() ? 0 : found->second;
}
int64_t skillRankSum(std::span<const int> skills, const std::map<int, int> &ranks) {
    int64_t result = 0;
    for (const int skill : skills) result = nativeAdd(nativeChecked(result),skillRank(ranks,skill));
    return result;
}
namespace {
template<class Synergies>
int64_t synergyBonus(const Synergies &synergies, const std::map<int, int> &ranks) {
    int64_t result = 0;
    for (const auto &[skill, percent] : synergies) result = nativeAdd(nativeChecked(result),nativeMultiply(skillRank(ranks,skill),percent));
    return result;
}
}
int64_t skillSynergyBonus(std::span<const std::pair<int, int>> synergies,
                        const std::map<int, int> &ranks) {
    return synergyBonus(synergies, ranks);
}
int64_t skillSynergyBonus(const std::map<int, int> &synergies, const std::map<int, int> &ranks) {
    return synergyBonus(synergies, ranks);
}
ShieldAbsorption absorbSkillShield(float damage, float manaValue, int percent, int manaFactor) {
    const auto result = absorbManaShieldFixed(int64_t(damage * 256.f), int64_t(manaValue * 256.f), percent, manaFactor);
    return {float(result.damage) / 256.f, float(result.mana) / 256.f};
}
} // namespace d2x
