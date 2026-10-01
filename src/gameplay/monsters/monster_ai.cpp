#include "gameplay/simulation/simulation.hpp"
#include "gameplay/monsters/skeleton_ai.hpp"
#include "gameplay/monsters/brute_ai.hpp"
#include "gameplay/monsters/zombie_ai.hpp"
#include "gameplay/monsters/fallen_ai.hpp"
#include "gameplay/monsters/corrupt_rogue_ai.hpp"
#include "gameplay/monsters/goatman_ai.hpp"
#include "gameplay/monsters/quill_rat_ai.hpp"
#include "gameplay/monsters/wraith_ai.hpp"
#include "gameplay/monsters/corrupt_lancer_ai.hpp"
#include "gameplay/monsters/corrupt_archer_ai.hpp"
#include "gameplay/monsters/skeleton_bow_ai.hpp"
#include "gameplay/monsters/monster_wander.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void Simulation::updateMonsters(float dt) {
    std::vector<MonsterSpawn> nestSpawns;
    auto beginFallenShout = [&](Enemy &enemy) {
        const auto duration = monsterSkill2Duration_ ? monsterSkill2Duration_(enemy) : std::nullopt;
        if (!duration || *duration <= 0) return false;
        enemy.route.clear();
        enemy.skill2Remaining = enemy.skill2Duration = *duration;
        emit(EnemySkill2{enemy.id});
        return true;
    };
    for (auto &enemy : state_.area.enemies) {
        if (enemy.hp <= 0 || !active(enemy.pos))
            continue;
        const auto ai = monsterAi_ ? monsterAi_(enemy) : std::nullopt;
        const bool approachAi = ai && (ai->kind == MonsterAiKind::CorruptRogue ||
            ai->kind == MonsterAiKind::CorruptLancer || ai->kind == MonsterAiKind::Skeleton ||
            ai->kind == MonsterAiKind::Goatman || ai->kind == MonsterAiKind::Wraith);
        if (enemy.attack <= 0 && !enemy.approach) {
            const auto target = chooseTarget(enemy.id);
            if (target != enemy.combatTarget) { enemy.route.clear(); enemy.rethink = 0; enemy.aiPursuing = false; }
            enemy.combatTarget = target;
        }
        const Vec targetPosition = monsterTargetPosition(enemy);
        if (enemy.hp < enemy.maxHp && enemy.poisonRemaining <= 0 &&
            enemy.openWoundsRemaining <= 0 && monsterDamageRegen_)
            if (auto rate = monsterDamageRegen_(enemy, state_.area.region)) {
                const auto perFrame = int(enemy.maxHp * 256.f * *rate / 4096.f);
                enemy.hp = std::min(enemy.maxHp, enemy.hp + perFrame / 256.f * dt * 25.f);
            }
        enemy.chill = std::max(0.f, enemy.chill - dt);
        enemy.webSlowRemaining = std::max(0.f, enemy.webSlowRemaining - dt);
        enemy.stun = std::max(0.f, enemy.stun - dt);
        enemy.freeze = std::max(0.f, enemy.freeze - dt);
        enemy.freezeActive = enemy.freeze > 0;
        enemy.rethink = std::max(0.f, enemy.rethink - dt);
        enemy.aiWait = std::max(0.f, enemy.aiWait - dt);
        enemy.movementVelocityPercent.reset();
        if (enemy.attack <= 0 && enemy.rethink <= 0 && enemy.stun <= 0 && enemy.freeze <= 0 &&
            enemy.hitFlash <= 0 && enemy.skill2Remaining <= 0 && enemy.resurrectionRemaining <= 0 &&
            tryMonsterTeleport(enemy)) continue;
        enemy.webAuraRemaining = std::max(0.f, enemy.webAuraRemaining - dt);
        if (enemy.webAuraRemaining == 0) enemy.webTrailDistance = 0;
        if (!combatUnit(enemy.combatTarget).alive() || !canAttack(enemy.id, enemy.combatTarget)) {
            monsterStopApproach(enemy);
            enemy.route.clear();
            enemy.aiPursuing = false;
            enemy.aiEscaping = false;
            enemy.aiCommanded = false;
            enemy.aiCircling = false;
            enemy.aiRunning = false;
            enemy.aiRetaliate = false;
            enemy.aiCharged = false;
            enemy.aiAdvanceRemaining = 0;
            enemy.aiPhase = 0;
            if (enemy.kind != MonsterKind::FoulCrowNest) enemy.aiLoop = 0;
            enemy.aiCorpse = {};
            enemy.skill2Remaining = enemy.skill2Duration = 0;
            enemy.attack = enemy.attackDuration = 0;
            enemy.attackImpact = -1;
            enemy.teleportTarget.reset();
            enemy.attackMode = 1;
            continue;
        }
        if (enemy.stun > 0 || enemy.freeze > 0 ||
            (approachAi && enemy.hitFlash > 0)) {
            monsterStopApproach(enemy);
            if (approachAi) {
                enemy.aiPursuing = false;
                enemy.aiRunning = false;
                enemy.route.clear();
            }
            enemy.attack = enemy.attackDuration = 0;
            enemy.attackImpact = -1;
            enemy.teleportTarget.reset();
            enemy.attackMode = 1;
            enemy.skill2Remaining = enemy.skill2Duration = 0;
            enemy.aiCorpse = {};
            continue;
        }
        if (enemy.kind == MonsterKind::QuillRat && enemy.hitFlash > 0) continue;
        if (enemy.skill2Remaining > 0) {
            enemy.skill2Remaining = std::max(0.f, enemy.skill2Remaining - dt);
            if (enemy.skill2Remaining == 0) enemy.skill2Duration = 0;
            continue;
        }
        if (enemy.resurrectionRemaining > 0) {
            enemy.resurrectionRemaining = std::max(0.f, enemy.resurrectionRemaining - dt);
            if (enemy.resurrectionRemaining == 0) enemy.resurrectionDuration = 0;
            continue;
        }
        if (enemy.attack > 0) {
            enemy.attack = std::max(0.f, enemy.attack - dt);
            if (enemy.attackImpact >= 0) {
                enemy.attackImpact -= dt;
                if (enemy.attackImpact <= 0) {
                    enemy.attackImpact = -1;
                    if (enemy.teleportTarget) {
                        if (grid_->walkable(*enemy.teleportTarget, movementRule(enemy))) enemy.pos = *enemy.teleportTarget;
                        enemy.teleportTarget.reset();
                    }
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
                    else if (enemy.attackMode >= 3)
                        launchMonsterSpell(enemy);
                    else if (monsterProjectile_ && monsterProjectile_(enemy, enemy.attackMode))
                        launchMonsterProjectile(enemy);
                    else
                        resolveMonsterAttack(enemy);
                }
            }
            if (enemy.attack == 0) {
                if (enemy.kind == MonsterKind::QuillRat)
                    enemy.aiWait = std::max(enemy.aiWait, 15.f / 25.f);
                enemy.attackDuration = 0;
                enemy.attackImpact = -1;
                enemy.attackMode = 1;
                enemy.aiCorpse = {};
            }
            continue;
        }
        if (enemy.aiRetaliate && enemy.hitFlash <= 0) {
            enemy.aiRetaliate = false;
            const auto ai = monsterAi_ ? monsterAi_(enemy) : std::nullopt;
            if (ai && ai->kind == MonsterAiKind::QuillRat && enemy.aiWait == 0 &&
                (targetPosition - enemy.pos).length() < monsterDefinition(enemy.kind).sightRange &&
                grid_->missileSegment(enemy.pos, targetPosition, {0x04, 1})) {
                enemy.route.clear();
                enemy.aiEscaping = false;
                beginMonsterAttack(enemy, 2);
                continue;
            }
        }
        if (enemy.aiCircling) {
            const Vec before = enemy.pos;
            const auto originalSpeed = monsterWalkSpeed_ ? monsterWalkSpeed_(enemy) : std::nullopt;
            const float speed = originalSpeed.value_or(monsterDefinition(enemy.kind).speed) *
                                (enemy.kind == MonsterKind::Brute ? bruteWalkMultiplier(enemy) : 1.f) *
                                (enemy.chill > 0 ? .42f : 1.f);
            monsterAdvanceCircle(enemy, *grid_, speed, dt, movementRule(enemy));
            leaveSpiderWeb(enemy, (enemy.pos - before).length());
            continue;
        }
        const bool fallenAi = ai && ai->kind == MonsterAiKind::Fallen;
        const bool rogueAi = ai && ai->kind == MonsterAiKind::CorruptRogue;
        const bool lancerAi = ai && ai->kind == MonsterAiKind::CorruptLancer;
        const bool archerAi = ai && ai->kind == MonsterAiKind::CorruptArcher;
        if (fallenAi && !enemy.aiEscaping && monsterDeathDuration_)
            for (const auto &corpse : state_.area.enemies) {
                if (relation(enemy.id, corpse.id) != Relation::Allied || !corpse.corpseAvailable() || corpse.id == enemy.id || corpse.id == enemy.aiCorpse ||
                    (corpse.pos - enemy.pos).length() >= 15.f) continue;
                const auto duration = monsterDeathDuration_(corpse);
                if (duration && corpse.deathAge <= *duration &&
                    fallenStartEscape(enemy, targetPosition, *grid_, movementRule(enemy))) {
                    enemy.aiCorpse = corpse.id;
                    break;
                }
            }
        if (enemy.aiEscaping) {
            const Vec before = enemy.pos;
            const auto originalSpeed = monsterWalkSpeed_ ? monsterWalkSpeed_(enemy) : std::nullopt;
            const float speed = originalSpeed.value_or(monsterDefinition(enemy.kind).speed) *
                                (enemy.kind == MonsterKind::Bighead ? .5f :
                                 enemy.kind == MonsterKind::SkeletonMage ? .25f :
                                 enemy.kind == MonsterKind::Fetish ? .5f :
                                 enemy.kind == MonsterKind::Vampire ? 1.f :
                                 enemy.kind == MonsterKind::BloodHawk && ai ?
                                     1.f + float(ai->params[3]) / 100.f :
                                 enemy.kind == MonsterKind::QuillRat ? 1.f : 1.5f) *
                                (enemy.chill > 0 ? .42f : 1.f);
            fallenAdvanceEscape(enemy, *grid_, speed, dt, movementRule(enemy));
            leaveSpiderWeb(enemy, (enemy.pos - before).length());
            continue;
        }
        const auto &definition = monsterDefinition(enemy.kind);
        auto delta = targetPosition - enemy.pos;
        float distance = delta.length();
        if (fallenAi && !enemy.aiCommanded && enemy.aiWait == 0 && enemy.route.empty() &&
            distance < 15.f &&
            std::none_of(state_.area.enemies.begin(), state_.area.enemies.end(),
                         [&](const Enemy &other) {
                             return other.identity.group == enemy.identity.group &&
                                    other.id.value < enemy.id.value;
                         }) &&
            monsterAiRandom(enemy) % 100 < unsigned(ai->params[0]) &&
            beginFallenShout(enemy)) {
            for (auto &other : state_.area.enemies)
                if (relation(enemy.id, other.id) == Relation::Allied && other.hp > 0 && other.identity.group == enemy.identity.group &&
                    other.kind == MonsterKind::Fallen && monsterAi_ && monsterAi_(other))
                    other.aiCommanded = true;
            continue;
        }
        if (distance >= (ai && ai->kind == MonsterAiKind::Vampire
                             ? std::max(definition.sightRange, float(ai->params[2]) + 2.f)
                             : definition.sightRange)) {
            monsterStopApproach(enemy);
            enemy.route.clear();
            enemy.rethink = 0;
            enemy.aiPursuing = false;
            enemy.aiRunning = false;
            enemy.aiAdvanceRemaining = 0;
            if (enemy.kind == MonsterKind::BloodHawk) enemy.aiCharged = false;
            continue;
        }
        const bool skeletonAi = ai && ai->kind == MonsterAiKind::Skeleton;
        const bool bruteAi = ai && ai->kind == MonsterAiKind::Brute;
        const bool zombieAi = ai && ai->kind == MonsterAiKind::Zombie;
        const bool goatmanAi = ai && ai->kind == MonsterAiKind::Goatman;
        const bool quillRatAi = ai && ai->kind == MonsterAiKind::QuillRat;
        if (quillRatAi && enemy.aiWait > 0) {
            enemy.route.clear();
            continue;
        }
        const bool wraithAi = ai && ai->kind == MonsterAiKind::Wraith;
        const bool skeletonBowAi = ai && ai->kind == MonsterAiKind::SkeletonBow;
        const bool bigheadAi = ai && ai->kind == MonsterAiKind::Bighead;
        const bool skeletonMageAi = ai && ai->kind == MonsterAiKind::SkeletonMage;
        const bool fetishAi = ai && ai->kind == MonsterAiKind::Fetish;
        const bool vampireAi = ai && ai->kind == MonsterAiKind::Vampire;
        // Native AI missile-barrier LOS is independent of ground walkability.
        bool clear = grid_->missileSegment(enemy.pos, targetPosition, {0x04, 1});
        auto approachFinished = [&]() {
            if (!enemy.approach) return false;
            if (const auto &destination = enemy.approach->destination)
                return std::floor(enemy.pos.x) == std::floor(destination->x) &&
                       std::floor(enemy.pos.y) == std::floor(destination->y);
            const auto target = combatUnit(enemy.combatTarget);
            const int size = monsterSize_ ? monsterSize_(enemy) : 2;
            return target.alive() && meleeDistance(enemy.pos, size, *target.position,
                target.stats.collisionSize) <= enemy.approach->stopDistance;
        };
        if (approachFinished()) monsterStopApproach(enemy);
        const bool inCombat = approachAi ? monsterMeleeReach(enemy)
                                         : distance < definition.attackRange && clear;
        // AITHINK rolls choose an action. Advance the accepted WL/RN action
        // until PathMisc's arrival condition or a real interruption, rather
        // than rerolling approach/run chances at every 25 Hz movement tick.
        if (ai && handleMonsterSpecialAi(enemy, *ai, distance, clear)) continue;
        if (skeletonBowAi) {
            const auto action = skeletonBowThink(enemy, *ai, distance, clear);
            if (action == SkeletonBowAction::Shoot) {
                enemy.route.clear();
                beginMonsterAttack(enemy, 1);
                continue;
            }
            if (action == SkeletonBowAction::Idle) {
                enemy.route.clear();
                continue;
            }
        }
        if (archerAi) {
            if (enemy.aiWait > 0) {
                enemy.route.clear();
                enemy.aiRunning = false;
                continue;
            }
            if (clear && distance < 6.f && corruptArcherRetreats(enemy, *ai) &&
                monsterStartRetreat(enemy, targetPosition, 12, *grid_, movementRule(enemy))) {
                enemy.aiRunning = false;
                continue;
            }
            if (clear && distance <= float(ai->params[4]) &&
                !corruptArcherApproaches(enemy, *ai, distance)) {
                enemy.route.clear();
                enemy.aiRunning = false;
                if (corruptArcherShoots(enemy, *ai)) beginMonsterAttack(enemy, 1);
                continue;
            }
        }
        if (quillRatAi && distance >= definition.attackRange &&
            distance < float(ai->params[0]) && clear) {
            if (quillRatShoots(enemy, *ai)) {
                beginMonsterAttack(enemy, 2);
                continue;
            }
            if (monsterStartRetreat(enemy, targetPosition, ai->params[3], *grid_, movementRule(enemy))) continue;
            if (distance < 4.f) {
                beginMonsterAttack(enemy, 2);
                continue;
            }
        }
        if (enemy.approach || !inCombat) {
            if (archerAi) enemy.aiRunning = distance > float(ai->params[4]);
            if (lancerAi && !enemy.approach) {
                const auto action = corruptLancerMovement(enemy, *ai,
                    float(missileDistance(enemy.pos, targetPosition)));
                if (action == CorruptLancerMovement::Idle) {
                    enemy.route.clear();
                    enemy.aiRunning = false;
                    continue;
                }
                const bool running = action == CorruptLancerMovement::Run;
                monsterStartApproach(enemy, running ? std::max(0, ai->meleeRange - 1) : 2,
                                     running ? 175 : 75, running);
            }
            if (rogueAi && !enemy.approach) {
                const auto action = corruptRogueMovement(
                    enemy, *ai, float(missileDistance(enemy.pos, targetPosition)), state_.population.difficulty);
                if (action == CorruptRogueMovement::Idle) {
                    enemy.route.clear();
                    enemy.aiRunning = false;
                    continue;
                }
                const bool running = action == CorruptRogueMovement::Run;
                monsterStartApproach(enemy, running ? 2 : 0,
                                     75 + (running ? ai->params[3] : 0), running);
            }
            if (skeletonAi && !enemy.approach) {
                if (!skeletonApproaches(enemy, *ai)) {
                    enemy.route.clear();
                    continue;
                }
                monsterStartApproach(enemy, 0, 75, false);
            }
            if (goatmanAi && !enemy.approach) {
                if (!goatmanApproaches(enemy, *ai)) {
                    enemy.route.clear();
                    continue;
                }
                monsterStartApproach(enemy, 0, 75, false);
            }
            if (wraithAi && !enemy.approach) {
                if (!wraithApproaches(enemy, *ai)) {
                    enemy.route.clear();
                    continue;
                }
                monsterStartApproach(enemy, 0, 75, false, monsterRadiusApproachTarget(
                    enemy.pos, monsterSize_ ? monsterSize_(enemy) : 2, targetPosition, 12));
            }
            // A newly requested action can already satisfy its native arrival
            // threshold. Finish it without manufacturing a one-tick footstep.
            if (approachFinished()) {
                monsterStopApproach(enemy);
                continue;
            }
            const auto fallenMove = fallenAi ? fallenMovement(enemy, *ai, distance)
                                             : FallenMovement::Approach;
            if (fallenMove == FallenMovement::Idle) {
                enemy.route.clear();
                continue;
            }
            const bool zombieWanders = zombieAi && !zombiePursues(
                enemy, *ai, distance,
                zombieForcedPursuit_ && zombieForcedPursuit_(state_.area.region));
            const bool wanders = zombieWanders || fallenMove == FallenMovement::Wander;
            const Vec movementTarget = enemy.approach && enemy.approach->destination
                ? *enemy.approach->destination : targetPosition;
            Vec destination = movementTarget;
            if (wanders) {
                while (!enemy.route.empty() && (enemy.route.front() - enemy.pos).length() < .25f)
                    enemy.route.pop_front();
                if (!enemy.route.empty() && !grid_->segment(enemy.pos, enemy.route.front(), {}, movementRule(enemy)))
                    enemy.route.clear();
                if (enemy.route.empty())
                    if (auto target = monsterWanderTarget(enemy, *grid_, 3, movementRule(enemy))) enemy.route.push_back(*target);
                if (enemy.route.empty()) continue;
                destination = enemy.route.front();
                enemy.rethink = 0;
            } else if (grid_->segment(enemy.pos, movementTarget, {}, movementRule(enemy))) {
                enemy.route.clear();
                enemy.rethink = 0;
            } else {
                while (!enemy.route.empty() && (enemy.route.front() - enemy.pos).length() < .25f)
                    enemy.route.pop_front();
                if (!enemy.route.empty() && !grid_->segment(enemy.pos, enemy.route.front(), {}, movementRule(enemy))) {
                    enemy.route.clear();
                    enemy.rethink = 0;
                }
                if (enemy.rethink <= 0) {
                    enemy.route = grid_->path(enemy.pos, movementTarget, false, movementRule(enemy));
                    enemy.rethink = .7f;
                }
                if (enemy.route.empty()) {
                    monsterStopApproach(enemy);
                    if (fallenAi) enemy.aiCommanded = false;
                    if (rogueAi) {
                        enemy.aiRunning = false;
                        enemy.aiPursuing = false;
                    }
                    if (lancerAi) enemy.aiRunning = false;
                    if (archerAi) enemy.aiRunning = false;
                    continue;
                }
                destination = enemy.route.front();
            }
            auto offset = destination - enemy.pos;
            auto originalSpeed = monsterWalkSpeed_ ? monsterWalkSpeed_(enemy) : std::nullopt;
            float speed = originalSpeed.value_or(definition.speed) * (enemy.chill > 0 ? .42f : 1.f);
            if (enemy.approach) {
                enemy.aiRunning = enemy.approach->running;
                enemy.movementVelocityPercent = enemy.approach->velocityPercent;
                const auto runSpeed = monsterMoveSpeed_
                    ? monsterMoveSpeed_(enemy, *enemy.movementVelocityPercent) : std::nullopt;
                speed = runSpeed.value_or(speed);
            }
            if (archerAi && enemy.aiRunning) {
                // CorruptArcher sets an engine +100 velocity stat.
                enemy.movementVelocityPercent = 175;
                const auto runSpeed = monsterMoveSpeed_ ? monsterMoveSpeed_(enemy, 175) : std::nullopt;
                speed = runSpeed.value_or(speed);
            }
            if (bruteAi) speed *= bruteWalkMultiplier(enemy);
            if (enemy.kind == MonsterKind::BloodHawk && enemy.aiCharged && ai)
                speed *= 1.f + float(ai->params[4]) / 100.f;
            if (zombieAi && !zombieWanders) speed *= 4.f / 3.f;
            auto next = enemy.pos + offset.unit() * std::min(speed * dt, offset.length());
            if (grid_->segment(enemy.pos, next, {}, movementRule(enemy))) {
                const float moved = (next - enemy.pos).length();
                enemy.pos = next;
                if (enemy.webAuraRemaining > 0) leaveSpiderWeb(enemy, moved);
                if (skeletonBowAi || skeletonMageAi) {
                    enemy.aiAdvanceRemaining = std::max(0.f, enemy.aiAdvanceRemaining - moved);
                }
            } else {
                monsterStopApproach(enemy);
                enemy.route.clear();
                enemy.rethink = 0;
                if (fallenAi) enemy.aiCommanded = false;
                if (rogueAi) {
                    enemy.aiRunning = false;
                    enemy.aiPursuing = false;
                }
                if (lancerAi) enemy.aiRunning = false;
                if (archerAi) enemy.aiRunning = false;
            }
        } else {
            monsterStopApproach(enemy);
            enemy.route.clear();
            enemy.rethink = 0;
            if (rogueAi) {
                enemy.aiRunning = false;
                enemy.aiPursuing = false;
            }
            if (lancerAi) enemy.aiRunning = false;
            if (archerAi) enemy.aiRunning = false;
        }
        if (approachFinished()) monsterStopApproach(enemy);
        if (!enemy.approach && !skeletonBowAi && !skeletonMageAi && !bigheadAi && !fetishAi && !vampireAi &&
            (approachAi ? monsterMeleeReach(enemy) :
                (targetPosition - enemy.pos).length() < definition.attackRange && grid_->segment(enemy.pos, targetPosition))) {
            if (skeletonAi && !skeletonAttacks(enemy, *ai))
                continue;
            if (fallenAi) {
                const auto action = fallenCombat(enemy, *ai);
                if (action == FallenCombat::Idle) continue;
                if (action == FallenCombat::Shout) {
                    beginFallenShout(enemy);
                    continue;
                }
            }
            if (bruteAi) {
                const auto action = bruteCombat(enemy, *ai);
                if (action == BruteCombat::Idle) continue;
                if (action == BruteCombat::Circle) {
                    if (!monsterStartCircle(enemy, targetPosition, 4, *grid_, movementRule(enemy)))
                        enemy.aiWait = 15.f / 25.f;
                    continue;
                }
            }
            if (rogueAi) {
                enemy.aiPursuing = enemy.aiRunning = false;
                if (!corruptRogueAttacks(enemy, *ai)) continue;
            }
            if (goatmanAi && !goatmanAttacks(enemy, *ai)) continue;
            if (wraithAi && !wraithAttacks(enemy, *ai)) continue;
            if (lancerAi && !corruptLancerAttacks(enemy, *ai)) continue;
            beginMonsterAttack(enemy);
        }
    }
    if (!nestSpawns.empty()) spawnEnemies(nestSpawns);
}
} // namespace d2x
