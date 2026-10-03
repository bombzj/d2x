#include <utility>
#include "gameplay/units/actions.hpp"
#include "gameplay/simulation/simulation.hpp"

namespace d2x {
void Simulation::advanceMonsterAction(Enemy &enemy, float dt, CurseAi curseAi, std::vector<MonsterSpawn> &nestSpawns) {
    refreshMonsterAttackRate(enemy);
    if (advanceTimedAction({enemy.attack, enemy.attackDuration, enemy.attackImpact}, dt, .00001f, 0.f)) {
        if (enemy.teleportTarget) {
            const int size = monsterSize_ ? monsterSize_(enemy) : 2;
            bool clear = grid_->walkable(*enemy.teleportTarget, {0x3c01, size});
            for (const auto &unit : combatUnits())
                if (unit.id != enemy.id && unit.alive() &&
                    meleeDistance(*enemy.teleportTarget, size, *unit.position, unit.stats.collisionSize) <= 0)
                    clear = false;
            if (clear) enemy.pos = *enemy.teleportTarget;
            enemy.teleportTarget.reset();
        }
        else if (enemy.attackMode == 3 && enemy.identity.superUnique == "The Countess")
            launchCountessFirewall(enemy);
        else if (enemy.attackMode == 3 && monsterResurrection_ &&
            monsterResurrection_(enemy))
            resolveMonsterResurrection(enemy);
        else if (enemy.attackMode == 3 && monsterWeb_ &&
                 monsterWeb_(enemy))
            activateSpiderWeb(enemy);
        else if (enemy.attackMode == 3 && monsterNest_ &&
                 monsterNest_(enemy)) {
            if (auto hatchling = nestSpawn(enemy, nestSpawns))
                nestSpawns.push_back(std::move(*hatchling));
        }
        else if (enemy.attackMode == 4 && enemy.kind == MonsterKind::BloodRaven)
            launchMonsterProjectile(enemy);
        else if (enemy.attackMode >= 3)
            launchMonsterSpell(enemy);
        else if (curseAi != CurseAi::DimVision && monsterProjectile_ && monsterProjectile_(enemy, enemy.attackMode))
            launchMonsterProjectile(enemy);
        else
            resolveMonsterAttack(enemy);
        if (enemy.kind == MonsterKind::Andariel && enemy.attackMode == 3)
            if (auto timing = monsterAttackTiming_(enemy, 3); timing &&
                ++enemy.attackEventIndex < timing->eventTimes.size())
                enemy.attackImpact = timing->eventTimes[enemy.attackEventIndex] -
                                     (enemy.attackDuration - enemy.attack);
    }
    if (enemy.attack == 0) {
        enemy.attackDuration = 0;
        enemy.attackImpact = -1;
        enemy.attackMode = 1;
        enemy.aiCorpse = {};
        if (enemy.kind == MonsterKind::Andariel) enemy.skillPosition.reset();
    }
}
} // namespace d2x
