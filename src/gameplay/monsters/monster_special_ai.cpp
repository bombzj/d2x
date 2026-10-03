#include "gameplay/monsters/implementation.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/monsters/bighead_ai.hpp"
#include "gameplay/monsters/skeleton_mage_ai.hpp"
#include "gameplay/monsters/fetish_ai.hpp"
#include "gameplay/monsters/vampire_ai.hpp"
#include "gameplay/monsters/fallen_shaman_ai.hpp"
#include "gameplay/monsters/blood_hawk_ai.hpp"
#include "gameplay/monsters/arach_ai.hpp"
#include "gameplay/monsters/monster_wander.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
bool Simulation::handleMonsterSpecialAi(Enemy &enemy, const MonsterAiProfile &ai,
                                        float distance, bool clear) {
    const Vec targetPosition = monsterTargetPosition(enemy);
    const bool inCombat = monsterMeleeReach(enemy);
    if (ai.kind == MonsterAiKind::Andariel) {
        auto chance = [&](int percent) { return monsterAiRandom(enemy) % 100 < unsigned(percent); };
        if (inCombat) beginMonsterAttack(enemy, chance(ai.params[0]) ? 3 : 1);
        else if (chance(ai.params[1])) enemy.aiWait = 5.f / 25.f;
        else if (chance(ai.params[2])) beginMonsterAttack(enemy, chance(ai.params[3]) ? 3 : 4);
        else monsterStartApproach(enemy, 1, 75, false);
        return true;
    }
    if (ai.kind == MonsterAiKind::Countess) {
        const auto *homeRoom = rooms_->room(enemy.aiHome);
        if (homeRoom != rooms_->room(enemy.pos) || homeRoom != rooms_->room(targetPosition) ||
            missileDistance(enemy.pos, enemy.aiHome) > 40) {
            if (missileDistance(enemy.pos, enemy.aiHome) > 0) {
                monsterStartApproach(enemy, 0, 75, true, enemy.aiHome);
                return true;
            }
            if (distance >= 25) { enemy.aiWait = 10.f / 25.f; return true; }
        }
        if (enemy.aiPhase >= int(enemy.skillPositions.size()) && state_.frame - enemy.skillCycleFrame > 700)
            enemy.aiPhase = 0;
        if (enemy.aiPhase < int(enemy.skillPositions.size()) && countessFirewall_) {
            enemy.skillPosition = enemy.skillPositions[size_t(enemy.aiPhase++)];
            enemy.skillCycleFrame = state_.frame;
            beginMonsterAttack(enemy, 3);
            return true;
        }
        if (inCombat) {
            if (monsterAiRandom(enemy) % 100 < unsigned(ai.params[2] + 10)) beginMonsterAttack(enemy, 1);
            else enemy.aiWait = float(ai.params[1]) / 25.f;
        } else if (monsterAiRandom(enemy) % 100 < unsigned(ai.params[0]))
            monsterStartApproach(enemy, 1, 175, true);
        else enemy.aiWait = float(ai.params[1]) / 25.f;
        return true;
    }
    if (ai.kind == MonsterAiKind::BloodRaven) {
        auto chance = [&](int percent) { return monsterAiRandom(enemy) % 100 < unsigned(percent); };
        const int homeDistance = missileDistance(enemy.pos, enemy.aiHome);
        const int targetHomeDistance = missileDistance(targetPosition, enemy.aiHome);
        if (distance > 45) { enemy.aiWait = 5.f / 25.f; return true; }
        if (homeDistance > 50 || targetHomeDistance >= 50) enemy.aiPhase = 1;
        if (enemy.aiPhase && homeDistance > 5) {
            monsterStartApproach(enemy, 0, 175, true, enemy.aiHome);
            return true;
        }
        enemy.aiPhase = 0;
        if (distance > 20 && targetHomeDistance < 50) {
            monsterStartApproach(enemy, std::max(12, int(distance) / 2) - 1, 175, true);
            return true;
        }
        enemy.aiAdvanceRemaining += 3;
        if (!inCombat && enemy.aiLoop < 8 + 2 * state_.population.difficulty &&
            chance(int(enemy.aiAdvanceRemaining))) {
            const int length = int(monsterAiRandom(enemy) % 15) + 5;
            int horizontal = 0, vertical = 0;
            if (monsterAiRandom(enemy) & 1) {
                horizontal = length;
                vertical = int(monsterAiRandom(enemy) % unsigned(length));
            } else {
                horizontal = int(monsterAiRandom(enemy) % unsigned(length));
                vertical = length;
            }
            if (monsterAiRandom(enemy) & 1) horizontal = -horizontal;
            if (monsterAiRandom(enemy) & 1) vertical = -vertical;
            enemy.nestSpawnPosition = Vec{std::floor(targetPosition.x) + horizontal + .5f,
                                         std::floor(targetPosition.y) + vertical + .5f};
            enemy.aiAdvanceRemaining = 0;
            ++enemy.aiLoop;
            beginMonsterAttack(enemy, 3);
            return true;
        }
        if (distance > 5) {
            if (chance(5) && targetHomeDistance < 50) {
                monsterStartApproach(enemy, 11, 175, true);
                return true;
            }
            if (clear && chance(80)) {
                beginMonsterAttack(enemy, chance(10 * (state_.population.difficulty + 4)) ? 4 : 1);
                return true;
            }
            enemy.movementVelocityPercent = 125;
            if (monsterStartCircle(enemy, targetPosition, 4, *grid_, movementRule(enemy))) return true;
        }
        if (distance < 12 && chance(30) &&
            monsterStartRetreat(enemy, targetPosition, 12 - int(distance), *grid_, movementRule(enemy))) {
            enemy.aiRunning = true;
            return true;
        }
        beginMonsterAttack(enemy, 1);
        return true;
    }
    if (ai.kind == MonsterAiKind::Smith || ai.kind == MonsterAiKind::Griswold) {
        if (inCombat) {
            if (ai.kind == MonsterAiKind::Smith || monsterAiRandom(enemy) % 100 < 80)
                beginMonsterAttack(enemy, 1);
            else enemy.aiWait = 10.f / 25.f;
        } else if (ai.kind == MonsterAiKind::Smith || monsterAiRandom(enemy) % 100 < 50) {
            const int health = std::clamp(int(enemy.hp * 100 / enemy.maxHp), 0, 100);
            monsterStartApproach(enemy, 1, 75 + (ai.kind == MonsterAiKind::Smith ? (100 - health) / 2 : 0), false);
        } else enemy.aiWait = 10.f / 25.f;
        return true;
    }
    if (ai.kind == MonsterAiKind::Arach) {
        const auto action = arachThink(enemy, ai, distance, inCombat);
        if (action == ArachAction::Attack || action == ArachAction::Web) {
            enemy.route.clear();
            beginMonsterAttack(enemy, action == ArachAction::Web ? 3 : 1);
            return true;
        }
        if (action == ArachAction::Retreat) {
            monsterStartRetreat(enemy, targetPosition, int(enemy.aiAdvanceRemaining), *grid_, movementRule(enemy));
            return true;
        }
        if (action == ArachAction::Circle) {
            monsterStartCircle(enemy, targetPosition, int(enemy.aiAdvanceRemaining), *grid_, movementRule(enemy));
            return true;
        }
        if (action == ArachAction::Wander) {
            if (auto destination = monsterWanderTarget(enemy, *grid_, 6, movementRule(enemy)))
                monsterStartApproach(enemy, 0, 75, false, *destination);
            return !enemy.approach;
        }
        if (action == ArachAction::Idle) {
            enemy.route.clear();
            return true;
        }
        return false;
    }
    if (ai.kind == MonsterAiKind::FoulCrowNest) {
        enemy.route.clear();
        if (enemy.aiWait > 0) return true;
        if (distance > 20.f) { enemy.aiWait = 1.f; return true; }
        if (enemy.aiLoop >= ai.params[2]) { enemy.noTreasure = true; return true; }
        if (state_.frame - enemy.nestLastCastFrame < EffectFrame(ai.params[0]) ||
            !monsterNest_ || !monsterNest_(enemy)) {
            enemy.aiWait = float(20 + monsterAiRandom(enemy) % 10) / 25.f;
            return true;
        }
        enemy.nestLastCastFrame = state_.frame;
        const Vec permission{std::floor(enemy.pos.x) + .5f, std::floor(enemy.pos.x) + 3.5f};
        if (!monsterAttackTiming_ || !monsterAttackTiming_(enemy, 3) ||
            !grid_->walkable(permission, {0x3c01, 2})) {
            enemy.aiWait = float(20 + monsterAiRandom(enemy) % 10) / 25.f;
            return true;
        }
        ++enemy.aiLoop;
        const auto nest = monsterNest_(enemy);
        enemy.nestSpawnPosition = Vec{std::floor(enemy.pos.x) + nest->spawnX + .5f,
                                     std::floor(enemy.pos.y) + nest->spawnY + .5f};
        beginMonsterAttack(enemy, 3);
        return true;
    }
    if (ai.kind == MonsterAiKind::BloodHawk) {
        const auto action = bloodHawkThink(enemy, ai, distance, inCombat);
        if (action == BloodHawkAction::Attack) {
            enemy.route.clear();
            beginMonsterAttack(enemy, 1);
            return true;
        }
        if (action == BloodHawkAction::Retreat) {
            if (!monsterStartRetreat(enemy, targetPosition, 4, *grid_, movementRule(enemy)))
                beginMonsterAttack(enemy, 1);
            return true;
        }
        if (action == BloodHawkAction::Circle || action == BloodHawkAction::Approach) {
            const bool slow = action == BloodHawkAction::Circle;
            if (auto destination = monsterWanderTarget(enemy, *grid_, slow ? 4 : 3, movementRule(enemy)))
                monsterStartApproach(enemy, 0, slow ? 25 : 75, false, *destination);
            return !enemy.approach;
        }
        return false;
    }
    if (ai.kind == MonsterAiKind::FallenShaman) {
        const auto skill = monsterResurrection_ ? monsterResurrection_(enemy) : std::nullopt;
        Enemy *corpse = nullptr;
        float closest = float(ai.params[3] * ai.params[3]);
        for (auto &candidate : state_.area.enemies) {
            if (!rooms_->nearby(enemy.pos, candidate.pos) || relation(enemy.id, candidate.id) != Relation::Allied ||
                !skill || !fallenShamanResurrectionTarget(enemy, candidate, *skill)) continue;
            const int size = monsterSize_ ? monsterSize_(enemy) : 2;
            const float separation = float(fallenShamanCorpseDistance(enemy, candidate, size));
            if (separation > closest) continue;
            const auto duration = monsterDeathDuration_
                ? monsterDeathDuration_(candidate) : std::nullopt;
            if (!duration || candidate.deathAge < *duration) continue;
            corpse = &candidate;
        }
        const auto alternate = combatUnit(chooseTarget(enemy.id));
        const float alternateDistance = alternate.alive()
            ? float(monsterAiDistance(*alternate.position, alternate.stats.collisionSize, enemy.pos)) : -1.f;
        const auto decision = fallenShamanThink(enemy, ai, distance, inCombat, corpse != nullptr, alternateDistance);
        if (decision.commandMinions)
            for (auto &other : state_.area.enemies)
                if (relation(enemy.id, other.id) == Relation::Allied && other.hp > 0 && other.kind == MonsterKind::Fallen &&
                    other.identity.ownerSpawnKey == enemy.identity.spawnKey &&
                    !monsterImplementation(other.identity.monster).substitute)
                    other.aiCommanded = true;
        if (decision.action == FallenShamanAction::Resurrect && corpse) {
            enemy.aiCorpse = corpse->id;
            enemy.route.clear();
            beginMonsterAttack(enemy, 3);
            return true;
        }
        if (decision.action == FallenShamanAction::Fire) {
            if (decision.alternateTarget) enemy.combatTarget = alternate.id;
            enemy.route.clear();
            beginMonsterAttack(enemy, 4);
            return true;
        }
        if (decision.action == FallenShamanAction::Melee) {
            enemy.route.clear();
            beginMonsterAttack(enemy, 1);
            return true;
        }
        if (decision.action == FallenShamanAction::Circle &&
            !monsterStartCircle(enemy, targetPosition, 3, *grid_, movementRule(enemy)))
            enemy.aiWait = 10.f / 25.f;
        if (!enemy.aiCircling) enemy.route.clear();
        return true;
    }
    if (ai.kind == MonsterAiKind::Vampire) {
        const auto target = combatUnit(chooseTarget(enemy.id));
        const float spellDistance = target.alive()
            ? float(monsterAiDistance(*target.position, target.stats.collisionSize, enemy.pos)) : -1.f;
        const auto action = vampireThink(enemy, ai, distance, inCombat, spellDistance, [&] {
            return monsterStartRetreat(enemy, targetPosition, 8, *grid_, movementRule(enemy));
        });
        if (action == VampireAction::Circle) {
            if (!monsterStartCircle(enemy, targetPosition, 4, *grid_, movementRule(enemy)))
                enemy.aiWait = 10.f / 25.f;
            return true;
        }
        if (action == VampireAction::Attack || action == VampireAction::CastFirst ||
            action == VampireAction::CastFourth) {
            if (action != VampireAction::Attack && target.alive()) enemy.combatTarget = target.id;
            enemy.route.clear();
            beginMonsterAttack(enemy, action == VampireAction::CastFirst ? 3 :
                                      action == VampireAction::CastFourth ? 6 : 1);
            return true;
        }
        if (action == VampireAction::Idle) {
            if (!enemy.aiEscaping) enemy.route.clear();
            return true;
        }
    }
    if (ai.kind == MonsterAiKind::Fetish) {
        const auto target = combatUnit(enemy.combatTarget);
        const auto targetStats = target.stats.attributes;
        const float life = target.alive() ? *target.life : 0;
        const int lifePercent = targetStats.maxLife > 0
            ? std::clamp(int(life * 100.f / float(targetStats.maxLife)), 0, 100) : 0;
        const auto action = fetishThink(enemy, ai, distance, inCombat, lifePercent);
        if (action == FetishAction::Retreat) {
            if (!monsterStartRetreat(enemy, targetPosition, 14, *grid_, movementRule(enemy)))
                fetishRetreatFailed(enemy);
            return true;
        }
        if (action == FetishAction::Circle) {
            if (!monsterStartCircle(enemy, targetPosition, 4, *grid_, movementRule(enemy)))
                enemy.aiWait = 10.f / 25.f;
            return true;
        }
        if (action == FetishAction::Attack) {
            enemy.route.clear();
            beginMonsterAttack(enemy, 1);
            return true;
        }
        if (action == FetishAction::Idle) {
            enemy.route.clear();
            return true;
        }
    }
    if (ai.kind == MonsterAiKind::SkeletonMage) {
        auto action = skeletonMageThink(enemy, ai, distance, clear);
        if (action == SkeletonMageAction::Retreat)
            action = monsterStartRetreat(enemy, targetPosition, 5, *grid_, movementRule(enemy))
                ? SkeletonMageAction::Idle : SkeletonMageAction::Fire;
        if (action == SkeletonMageAction::Circle) {
            if (!monsterStartCircle(enemy, targetPosition, 4, *grid_, movementRule(enemy)))
                enemy.aiWait = float(ai.params[7]) / 25.f;
            action = SkeletonMageAction::Idle;
        }
        if (action == SkeletonMageAction::Fire) {
            enemy.route.clear();
            beginMonsterAttack(enemy, 1);
            return true;
        }
        if (action == SkeletonMageAction::Idle) {
            if (!enemy.aiEscaping && !enemy.aiCircling) enemy.route.clear();
            return true;
        }
    }
    if (ai.kind == MonsterAiKind::Bighead) {
        auto action = bigheadThink(enemy, ai, distance, clear, inCombat);
        if (action == BigheadAction::Approach) {
            const bool healthy = enemy.maxHp <= 0 || enemy.hp * 100 >= enemy.maxHp * ai.params[0];
            monsterStartApproach(enemy, healthy ? 0 : 5, 75, false);
        }
        if (action == BigheadAction::Retreat)
            action = monsterStartRetreat(enemy, targetPosition, 5, *grid_, movementRule(enemy))
                ? BigheadAction::Idle : BigheadAction::Fire;
        if (action == BigheadAction::Circle) {
            if (!monsterStartCircle(enemy, targetPosition, 3, *grid_, movementRule(enemy)))
                enemy.aiWait = 10.f / 25.f;
            action = BigheadAction::Idle;
        }
        if (action == BigheadAction::Melee || action == BigheadAction::Fire) {
            enemy.route.clear();
            beginMonsterAttack(enemy, action == BigheadAction::Fire ? 2 : 1);
            return true;
        }
        if (action == BigheadAction::Idle) {
            if (!enemy.aiEscaping && !enemy.aiCircling) enemy.route.clear();
            return true;
        }
    }
    return false;
}
} // namespace d2x
