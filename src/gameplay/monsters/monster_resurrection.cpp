#include "gameplay/simulation/simulation.hpp"
#include "gameplay/monsters/fallen_shaman_ai.hpp"
#include "gameplay/monsters/monster_wander.hpp"
#include <cmath>

namespace d2x {
void Simulation::resolveMonsterResurrection(Enemy &shaman) {
    const EntityId targetId = shaman.aiCorpse;
    shaman.aiCorpse = {};
    const auto skill = monsterResurrection_ ? monsterResurrection_(shaman) : std::nullopt;
    auto *corpse = targetId ? findEnemy(targetId) : nullptr;
    const auto ai = monsterAi_ ? monsterAi_(shaman) : std::nullopt;
    if (!skill || !corpse || !ai || ai->kind != MonsterAiKind::FallenShaman ||
        relation(shaman.id, corpse->id) != Relation::Allied ||
        !fallenShamanResurrectionTarget(shaman, *corpse, *skill) ||
        monsterAiDistance(shaman.pos, monsterSize_ ? monsterSize_(shaman) : 2, corpse->pos) >
            ai->params[3] * ai->params[3]) return;
    const auto duration = monsterDeathDuration_ ? monsterDeathDuration_(*corpse) : std::nullopt;
    if (!duration || corpse->deathAge < *duration) return;
    const auto reviveDuration = monsterResurrectionDuration_
        ? monsterResurrectionDuration_(*corpse) : std::nullopt;
    if (!reviveDuration || *reviveDuration <= 0) return;
    corpse->hp = corpse->maxHp;
    corpse->deathAge = corpse->chill = corpse->stun = corpse->hitFlash = corpse->hitDisplay = 0;
    corpse->knockbackRemaining = corpse->knockbackDuration = 0;
    corpse->knockbackDestination.reset();
    corpse->aiAlerted = false;
    corpse->aiWait = 0;
    corpse->approach.reset();
    corpse->aiPursuing = corpse->aiEscaping = corpse->aiCommanded = false;
    corpse->aiCircling = corpse->aiRunning = corpse->aiRetaliate = corpse->aiCharged = false;
    corpse->aiAdvanceRemaining = 0;
    corpse->aiPhase = corpse->aiLoop = 0;
    corpse->aiCorpse = {};
    corpse->resurrectionRemaining = corpse->resurrectionDuration = *reviveDuration;
    corpse->resurrected = true;
    corpse->route.clear();
    --state_.area.kills;
}
} // namespace d2x
