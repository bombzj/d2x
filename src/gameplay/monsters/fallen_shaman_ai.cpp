#include "fallen_shaman_ai.hpp"
#include "monster_wander.hpp"

namespace d2x {
bool fallenShamanResurrectionTarget(const Enemy &shaman, const Enemy &corpse,
                                   const MonsterResurrection &skill) {
    return shaman.kind == MonsterKind::FallenShaman && corpse.hp <= 0 &&
           corpse.id != shaman.id && corpse.kind == MonsterKind::Fallen &&
           !skill.minion.empty() && corpse.identity.monster == skill.minion &&
           corpse.identity.group == shaman.identity.group &&
           (corpse.identity.rank == MonsterRank::Normal ||
            corpse.identity.rank == MonsterRank::Minion) &&
           !monsterImplementation(corpse.identity.monster).substitute;
}
FallenShamanDecision fallenShamanThink(Enemy &enemy, const MonsterAiProfile &rules,
                                      float distance, bool inCombat, bool hasCorpse) {
    if (enemy.aiWait > 0) return {FallenShamanAction::Idle, false};
    auto roll = [&](int chance) {
        return monsterAiRandom(enemy) % 100 < unsigned(chance);
    };
    if (inCombat && roll(rules.params[2])) return {FallenShamanAction::Melee, false};
    const bool command = roll(rules.params[0]);
    if (hasCorpse && roll(rules.params[0]))
        return {FallenShamanAction::Resurrect, command};
    if (distance < float(rules.params[4]) && roll(rules.params[1]))
        return {FallenShamanAction::Fire, command};
    if (roll(rules.params[2])) return {FallenShamanAction::Circle, command};
    enemy.aiWait = 10.f / 25.f;
    return {FallenShamanAction::Idle, command};
}
} // namespace d2x
