#include "curse_resolve.hpp"
#include "rank_bonus.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
int32_t nativeSlowPercentage(int32_t amount,bool player,bool championOrUnique) {
    return std::min(amount,player || championOrUnique?50:90);
}
std::optional<int32_t> curseStateDuration(int32_t frames,int32_t resistance,bool curse) {
    if(!curse) return frames;
    if(resistance>=100) return {};
    return nativeSubtract(frames,nativePercentage(frames,resistance,100));
}
int32_t curseResistanceAdjustment(int32_t amount,int32_t baseResistance,bool monster,bool hireling) {
    return amount<=0 && monster && !hireling && baseResistance>=100?amount/5:amount;
}
CurseSpec evaluateCurse(const CurseSpec &spec, int rank) {
    if (rank < 1 || rank > 255) throw std::runtime_error("Unsupported original curse rank");
    auto result = spec;
    result.radius += (rank - 1) * result.radiusPerLevel;
    result.frames += (rank - 1) * result.framesPerLevel;
    result.modifiers.combat.attackRatingPercent+=(rank-1)*result.attackRatingPerLevel;
    result.modifiers.combat.damagePercent+=(rank-1)*result.damagePerLevel;
    result.modifiers.combat.defensePercent+=(rank-1)*result.defensePerLevel;
    result.modifiers.combat.ironMaidenPercent = result.reflectPercent + (rank - 1) * result.reflectPerLevel;
    result.modifiers.combat.lifeTapPercent = result.lifeTapPercent + (rank - 1) * result.lifeTapPerLevel;
    if (result.resistMaximum > 0) {
        // The shared retail parameter rule truncates the level ratio first.
        const int amount = -skillDiminishingRatioBonus({result.resistMinimum, result.resistMaximum}, rank);
        auto &modifiers = result.modifiers;
        modifiers.fireResist = modifiers.coldResist = modifiers.lightningResist = modifiers.poisonResist = amount;
    }
    return result;
}
} // namespace d2x
