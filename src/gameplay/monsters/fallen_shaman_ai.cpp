#include "gameplay/model/state.hpp"
#include "gameplay/monsters/implementation.hpp"
#include "fallen_shaman_ai.hpp"
#include "monster_wander.hpp"

namespace d2x {
bool fallenShamanResurrectionTarget(const Enemy &shaman, const Enemy &corpse,
                                   const MonsterResurrection &skill) {
    const bool unique = shaman.identity.rank == MonsterRank::Unique ||
                        shaman.identity.rank == MonsterRank::SuperUnique;
    return shaman.kind == MonsterKind::FallenShaman && corpse.corpseAvailable() &&
           corpse.id != shaman.id && (unique ?
               corpse.kind == MonsterKind::Fallen || corpse.kind == MonsterKind::FallenShaman :
               corpse.kind == MonsterKind::Fallen && !skill.minion.empty() &&
               corpse.identity.monster == skill.minion && !shaman.identity.spawnKey.empty() &&
               corpse.identity.ownerSpawnKey == shaman.identity.spawnKey) &&
           (corpse.identity.rank == MonsterRank::Normal ||
            corpse.identity.rank == MonsterRank::Minion) &&
           !monsterImplementation(corpse.identity.monster).substitute;
}
int fallenShamanCorpseDistance(const Enemy &shaman, const Enemy &corpse, int size) {
    if (shaman.identity.rank == MonsterRank::Unique || shaman.identity.rank == MonsterRank::SuperUnique) {
        const int horizontal = int(shaman.pos.x) - int(corpse.pos.x);
        const int vertical = int(shaman.pos.y) - int(corpse.pos.y);
        return horizontal * horizontal + vertical * vertical;
    }
    return monsterAiDistance(shaman.pos, size, corpse.pos);
}
FallenShamanDecision fallenShamanThink(Enemy &enemy, const MonsterAiProfile &rules,
                                      float distance, bool inCombat, bool hasCorpse, float alternateDistance) {
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
    if (alternateDistance >= 0 && alternateDistance < float(rules.params[4]) && roll(rules.params[1]))
        return {FallenShamanAction::Fire, command, true};
    if (roll(rules.params[2])) return {FallenShamanAction::Circle, command};
    enemy.aiWait = 10.f / 25.f;
    return {FallenShamanAction::Idle, command};
}
} // namespace d2x
