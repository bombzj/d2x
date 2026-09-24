#include "gameplay/simulation/simulation.hpp"

namespace d2x {
void Simulation::resolveMonsterResurrection(Enemy &shaman) {
    const EntityId targetId = shaman.aiCorpse;
    shaman.aiCorpse = {};
    const auto skill = monsterResurrection_ ? monsterResurrection_(shaman) : std::nullopt;
    auto *corpse = targetId ? findEnemy(targetId) : nullptr;
    const auto ai = monsterAi_ ? monsterAi_(shaman) : std::nullopt;
    if (!skill || !corpse || !ai || ai->kind != MonsterAiKind::FallenShaman ||
        corpse->hp > 0 || (corpse->kind != MonsterKind::Fallen &&
                           corpse->kind != MonsterKind::FallenShaman) ||
        monsterImplementation(corpse->identity.monster).substitute ||
        (corpse->identity.rank != MonsterRank::Normal &&
         corpse->identity.rank != MonsterRank::Minion) ||
        (corpse->pos - shaman.pos).length() > float(ai->params[3])) return;
    const auto duration = monsterDeathDuration_ ? monsterDeathDuration_(*corpse) : std::nullopt;
    if (!duration || corpse->deathAge < *duration) return;
    corpse->hp = corpse->maxHp;
    corpse->deathAge = corpse->chill = corpse->stun = corpse->hitFlash = 0;
    corpse->aiWait = 0;
    corpse->aiPursuing = corpse->aiEscaping = corpse->aiCommanded = false;
    corpse->aiCircling = corpse->aiRunning = corpse->aiRetaliate = corpse->aiCharged = false;
    corpse->aiAdvanceRemaining = 0;
    corpse->aiPhase = corpse->aiLoop = 0;
    corpse->aiCorpse = {};
    corpse->resurrected = true;
    corpse->route.clear();
    --state_.area.kills;
}
} // namespace d2x
