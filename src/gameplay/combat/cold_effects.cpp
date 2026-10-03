#include "gameplay/combat/damage_resolution.hpp"
#include "gameplay/simulation/simulation.hpp"
#include <algorithm>

namespace d2x {
float Simulation::missileColdDuration(EntityId attacker, const RuntimeCombatUnit &target, int frames) const {
    const auto &mods = target.stats.attributes.combat;
    if (frames <= 0 || mods.cannotBeFrozen) return 0;
    // Total damage processing halves the duration before resistance rounding.
    if (mods.halfFreezeDuration) frames /= 2;
    int resistance = rawResistance(target.stats.attributes, MonsterDamageType::Cold);
    if (resistance >= 100) return 0;
    if (target.stats.monsterResistanceRules && coldPierce_) resistance -= coldPierce_(attacker);
    frames = int(int64_t(frames) * (100 - std::clamp(resistance, -100, 100)) / 100);
    if (frames <= 0) return 0;
    const int coldEffect = unitColdEffect_ ? unitColdEffect_(target) : 0;
    if (coldEffect == 0) return 0;
    return float(std::max(1, frames)) / 25.f;
}
void Simulation::applyMissileFreeze(EntityId attacker, RuntimeCombatUnit target, int frames) {
    if (!target.alive() || frames <= 0 ||
        (target.monster && target.effects->hasState(uninterruptableState_, state_.frame))) return;
    const auto rank = target.stats.rank;
    // ApplyFreezeState turns freeze into cold slow for players, hirelings and
    // boss/unique/champion monsters. Ordinary nonnegative ColdEffect gets neither.
    if (!target.monster || target.stats.boss || rank == MonsterRank::Boss ||
        rank == MonsterRank::Unique || rank == MonsterRank::SuperUnique || rank == MonsterRank::Champion) {
        applyChill(target.id, missileColdDuration(attacker, target, frames));
        return;
    }
    const auto &mods = target.stats.attributes.combat;
    if (frames <= 0 || mods.cannotBeFrozen || !target.stats.freezable ||
        !unitColdEffect_ || unitColdEffect_(target) >= 0) return;
    if (mods.halfFreezeDuration) frames /= 2;
    int resistance = rawResistance(target.stats.attributes, MonsterDamageType::Cold);
    if (resistance >= 100) return;
    if (target.stats.monsterResistanceRules && coldPierce_) resistance -= coldPierce_(attacker);
    frames = int(int64_t(frames) * (100 - std::clamp(resistance, -100, 100)) / 100);
    if (frames <= 0) return;
    // FreezeDiv is distinct from ColdDiv, with integer truncation and no one-frame floor.
    // The state bit is installed even if division truncates the duration to zero;
    // a lethal hit can still preserve it before the next remove-state tick.
    target.records.monster->freezeActive = true;
    frames /= monsterFreezeDivisor_;
    target.records.monster->freeze = std::max(target.records.monster->freeze, float(frames) / 25.f);
    if (frames > 0) target.records.monster->route.clear();
}
} // namespace d2x
