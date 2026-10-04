#include "gameplay/units/actions.hpp"
#include "gameplay/units/movement.hpp"
#include "gameplay/units/resources.hpp"
#include "gameplay/units/impairments.hpp"
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
        if (enemy.kind == MonsterKind::PrisonDoor) { enemy.route.clear(); continue; }
        if (enemy.conversion && (state_.frame >= enemy.conversion->expiresAt || enemy.hp <= 0)) {
            const auto conversion = *enemy.conversion;
            if (conversion.level > conversion.convertedLevel) {
                enemy.hp = std::max(0.f, float(int(conversion.maximumLife) * int(enemy.hp) / std::max(1, int(enemy.maxHp))));
                enemy.maxHp = conversion.maximumLife;
            }
            enemy.allegiance = conversion.original;
            enemy.combatEffects.removeState(conversion.state);
            std::vector<EffectHandle> auras;
            for (const auto &effect : enemy.combatEffects.entries())
                if (effect.spec.stacking == EffectStacking::AuraLevel) auras.push_back(effect.handle);
            for (const auto handle : auras) enemy.combatEffects.remove(handle);
            enemy.conversion.reset(); enemy.combatTarget = {};
            enemy.route.clear(); cancelTimedAction({enemy.attack, enemy.attackDuration, enemy.attackImpact});
            enemy.aiPursuing = enemy.aiEscaping = enemy.aiCircling = enemy.aiRunning = false;
            enemy.aiCorpse = {};
            enemy.skill2Remaining = enemy.skill2Duration = 0;
            enemy.rethink = 0;
            monsterStopApproach(enemy);
        }
        if (enemy.hp <= 0 || !active(enemy.pos))
            continue;
        if (enemy.questDeathFrame && state_.frame >= enemy.questDeathFrame) {
            enemy.questDeathFrame = 0;
            damageEnemy(enemy, enemy.hp, {}, 0, true, MonsterDamageType::Physical, true);
            continue;
        }
        if (advanceAuraKnockback(enemy, dt)) continue;
        const auto attracted = combatUnit(enemy.attractedTarget);
        if (enemy.attractedTarget && (enemy.attractedUntil <= state_.frame || !attracted.alive())) {
            enemy.attractedTarget = {}; enemy.attractedUntil = 0;
            enemy.attractionSource = {};
            monsterStopApproach(enemy); enemy.route.clear(); enemy.combatTarget = {};
            cancelTimedAction({enemy.attack, enemy.attackDuration, enemy.attackImpact});
            enemy.rethink = 0;
        }
        CurseAi curseAi = CurseAi::None;
        for (const auto &effect : enemy.combatEffects.entries())
            if (effect.activeAt(state_.frame) && effect.spec.curseAi != CurseAi::None) {
                curseAi = effect.spec.curseAi;
            }
        if (enemy.activeCurseAi != curseAi) {
            const bool enteringBlind = curseAi == CurseAi::DimVision;
            const bool finishEscape = enemy.activeCurseAi == CurseAi::Terror && curseAi == CurseAi::None &&
                enemy.terrorMovement && enemy.aiEscaping && !enemy.route.empty();
            if (!enteringBlind) {
                monsterStopApproach(enemy);
                if (!finishEscape) enemy.route.clear();
                if (!finishEscape) { enemy.combatTarget = {}; enemy.terrorMovement.reset(); }
                cancelTimedAction({enemy.attack, enemy.attackDuration, enemy.attackImpact});
                enemy.skill2Remaining = enemy.skill2Duration = 0;
                enemy.teleportTarget.reset(); enemy.nestSpawnPosition.reset(); enemy.aiCorpse = {};
                if (!finishEscape) enemy.aiEscaping = enemy.aiRunning = false;
                enemy.aiCircling = false;
            }
            enemy.rethink = 0;
            enemy.activeCurseAi = curseAi;
        }
        const auto ai = monsterAi_ ? monsterAi_(enemy) : std::nullopt;
        if (curseAi != CurseAi::Terror && enemy.attack <= 0 && !enemy.approach && !enemy.aiEscaping && !enemy.aiCircling &&
            !(curseAi == CurseAi::DimVision && !enemy.route.empty())) {
            // AiUtil's Confuse coordinate search uses a 35-unit candidate range.
            const auto target = chooseTarget(enemy.id, curseAi == CurseAi::DimVision ? 4.f :
                curseAi == CurseAi::Confuse ? 35.f : 25.f);
            if (target != enemy.combatTarget) { enemy.route.clear(); enemy.rethink = 0; enemy.aiPursuing = false; }
            enemy.combatTarget = target;
        }
        const Vec targetPosition = monsterTargetPosition(enemy);
        if (enemy.hp < enemy.maxHp && enemy.poisonRemaining <= 0 &&
            enemy.openWoundsRemaining <= 0 && monsterDamageRegen_)
            if (auto rate = monsterDamageRegen_(enemy, state_.area.region))
                advanceLifeRegeneration(enemy.hp, enemy.maxHp, *rate, dt, LifeRegenOrder::StepThenFrameRate);
        advanceImpairments({&enemy.chill, &enemy.freeze, &enemy.stun, &enemy.freezeActive,
                           {&enemy.webSlowRemaining, &enemy.webSlowPercent}},
                          dt, WebSlowExpiry::RetainMetadata);
        enemy.hitDisplay = std::max(0.f, enemy.hitDisplay - dt);
        enemy.rethink = std::max(0.f, enemy.rethink - dt);
        enemy.aiWait = std::max(0.f, enemy.aiWait - dt);
        enemy.movementVelocityPercent.reset();
        if (curseAi == CurseAi::None && enemy.attack <= 0 && !enemy.approach && !enemy.aiEscaping && !enemy.aiCircling &&
            enemy.rethink <= 0 && enemy.stun <= 0 && enemy.freeze <= 0 &&
            enemy.hitFlash <= 0 && enemy.skill2Remaining <= 0 && enemy.resurrectionRemaining <= 0 &&
            tryMonsterTeleport(enemy)) continue;
        enemy.webAuraRemaining = std::max(0.f, enemy.webAuraRemaining - dt);
        if (enemy.webAuraRemaining == 0) enemy.webTrailDistance = 0;
        const bool finishingTerror = (curseAi == CurseAi::None || curseAi == CurseAi::DimVision) &&
            enemy.terrorMovement && enemy.aiEscaping && !enemy.route.empty();
        if ((curseAi == CurseAi::Terror || finishingTerror) && enemy.terrorMovement &&
            enemy.attack <= 0 && enemy.stun <= 0 && enemy.freeze <= 0 && enemy.hitFlash <= 0) {
            auto &flight = *enemy.terrorMovement;
            const auto threat = combatUnit(flight.threat);
            if (curseAi == CurseAi::Terror && threat.alive()) {
                enemy.combatTarget = flight.threat;
                if (enemy.rethink <= 0 && (enemy.route.empty() || !flight.beganEscape)) {
                    if (missileDistance(enemy.pos, *threat.position) <= 30) {
                        if (flight.beganEscape && monsterMeleeReach(enemy, flight.threat, 0)) {
                            enemy.route.clear(); enemy.aiEscaping = false;
                            beginMonsterAttack(enemy, 1);
                        } else if (!monsterStartRetreat(enemy, *threat.position, 30, *grid_, movementRule(enemy)) && flight.beganEscape) {
                            if (const auto destination = monsterWanderTarget(enemy, *grid_, 6, movementRule(enemy)))
                                enemy.route = grid_->path(enemy.pos, *destination, false, movementRule(enemy));
                        }
                        flight.beganEscape = true;
                    }
                    enemy.rethink = 10.f / 25.f;
                }
            }
            if (!enemy.route.empty() && enemy.attack <= 0) {
                const int percentage = 75 + flight.velocityBonus;
                enemy.movementVelocityPercent = percentage;
                enemy.aiRunning = flight.running; enemy.aiEscaping = true;
                const float speed = monsterMoveSpeed_ ? monsterMoveSpeed_(enemy, percentage).value_or(0) : 0;
                const Vec before = enemy.pos;
                const auto result = advanceRouteMovement(enemy.pos, enemy.route, speed, dt, .05f,
                    [&](Vec from, Vec to) { return grid_->segment(from, to, {}, movementRule(enemy)); });
                leaveSpiderWeb(enemy, (enemy.pos - before).length());
                if (result != RouteMovement::Moving) enemy.aiEscaping = false;
            }
            if (finishingTerror && (!enemy.aiEscaping || enemy.route.empty())) enemy.terrorMovement.reset();
            if (enemy.attack <= 0) continue;
        }
        if (curseAi == CurseAi::DimVision && enemy.attack <= 0 && enemy.skill2Remaining <= 0 &&
            enemy.resurrectionRemaining <= 0 && enemy.stun <= 0 && enemy.freeze <= 0 && enemy.hitFlash <= 0) {
            if (enemy.approach) {
                const auto target = combatUnit(enemy.combatTarget);
                const auto destination = enemy.approach->destination;
                const bool arrived = destination
                    ? std::floor(enemy.pos.x) == std::floor(destination->x) &&
                      std::floor(enemy.pos.y) == std::floor(destination->y)
                    : target.alive() && meleeDistance(enemy.pos, monsterSize_ ? monsterSize_(enemy) : 2,
                          *target.position, target.stats.collisionSize) <= enemy.approach->stopDistance;
                if (arrived || (!destination && (!target.alive() || !canAttack(enemy.id, target.id)))) {
                    monsterStopApproach(enemy);
                } else {
                    // Continue the accepted pursuit; blindness supplies no new detour around a barrier.
                    discardReachedWaypoints(enemy.pos, enemy.route, .05f);
                    const Vec next = enemy.route.empty() ? destination.value_or(targetPosition) : enemy.route.front();
                    const auto delta = next - enemy.pos;
                    enemy.movementVelocityPercent = enemy.approach->velocityPercent;
                    enemy.aiRunning = enemy.approach->running;
                    const float speed = monsterMoveSpeed_
                        ? monsterMoveSpeed_(enemy, *enemy.movementVelocityPercent).value_or(0) : 0;
                    const Vec before = enemy.pos;
                    if (!advanceMovement(enemy.pos, delta.unit(), std::min(delta.length(), speed * dt),
                        [&](Vec from, Vec to) { return grid_->segment(from, to, {}, movementRule(enemy)); }).accepted)
                        monsterStopApproach(enemy);
                    leaveSpiderWeb(enemy, (enemy.pos - before).length());
                    if (enemy.approach) continue;
                }
            }
            if (enemy.rethink <= 0) {
                if (enemy.combatTarget && monsterMeleeReach(enemy, enemy.combatTarget, 0)) beginMonsterAttack(enemy, 1);
                else if (enemy.route.empty() && monsterAiRandom(enemy) % 100 < 20)
                    if (const auto destination = monsterWanderTarget(enemy, *grid_, 3, movementRule(enemy)))
                        enemy.route = grid_->path(enemy.pos, *destination, false, movementRule(enemy));
                enemy.rethink = 10.f / 25.f;
            }
            if (!enemy.route.empty()) {
                const auto delta = enemy.route.front() - enemy.pos;
                const float speed = monsterMoveSpeed_ ? monsterMoveSpeed_(enemy, 75).value_or(0) : 0;
                if (!advanceMovement(enemy.pos, delta.unit(), std::min(delta.length(), speed * dt),
                    [&](Vec from, Vec to) { return grid_->segment(from, to, {}, movementRule(enemy)); }).accepted)
                    enemy.route.clear();
                if (!enemy.route.empty() && (enemy.route.front() - enemy.pos).length() < .05f) enemy.route.pop_front();
            }
            if (enemy.route.empty()) enemy.aiEscaping = enemy.aiCircling = enemy.aiRunning = false;
            if (enemy.attack <= 0) continue;
        }
        if (!combatUnit(enemy.combatTarget).alive() || !canAttack(enemy.id, enemy.combatTarget)) {
            monsterStopApproach(enemy);
            enemy.route.clear();
            enemy.aiPursuing = false;
            enemy.aiEscaping = false;
            enemy.aiCommanded = false;
            enemy.aiCircling = false;
            enemy.aiRunning = false;
            enemy.aiRetaliate = false;
            enemy.aiAlerted = false;
            enemy.aiCharged = false;
            enemy.aiAdvanceRemaining = 0;
            enemy.aiPhase = 0;
            if (enemy.kind != MonsterKind::FoulCrowNest) enemy.aiLoop = 0;
            enemy.aiCorpse = {};
            enemy.skill2Remaining = enemy.skill2Duration = 0;
            cancelTimedAction({enemy.attack, enemy.attackDuration, enemy.attackImpact});
            enemy.teleportTarget.reset();
            enemy.nestSpawnPosition.reset();
            enemy.attackMode = 1;
            continue;
        }
        if (enemy.stun > 0 || enemy.freeze > 0 ||
            enemy.hitFlash > 0) {
            monsterStopApproach(enemy);
            enemy.aiPursuing = enemy.aiRunning = enemy.aiEscaping = enemy.aiCircling = false;
            enemy.route.clear();
            cancelTimedAction({enemy.attack, enemy.attackDuration, enemy.attackImpact});
            enemy.teleportTarget.reset();
            enemy.nestSpawnPosition.reset();
            enemy.attackMode = 1;
            enemy.skill2Remaining = enemy.skill2Duration = 0;
            enemy.aiCorpse = {};
            continue;
        }
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
            advanceMonsterAction(enemy, dt, curseAi, nestSpawns);
            continue;
        }
        if (enemy.aiRetaliate && !enemy.approach && !enemy.aiEscaping && !enemy.aiCircling &&
            enemy.hitFlash <= 0 && ai && ai->kind != MonsterAiKind::Zombie &&
            ai->kind != MonsterAiKind::Arach && ai->kind != MonsterAiKind::Vampire) {
            if (ai && ai->kind == MonsterAiKind::QuillRat && !monsterMeleeReach(enemy) &&
                (targetPosition - enemy.pos).length() < monsterDefinition(enemy.kind).sightRange &&
                grid_->missileSegment(enemy.pos, targetPosition, {0x04, 1})) {
                enemy.route.clear();
                enemy.aiEscaping = false;
                beginMonsterAttack(enemy, 2);
                enemy.aiRetaliate = false;
                continue;
            }
            if ((ai->kind == MonsterAiKind::SkeletonBow || ai->kind == MonsterAiKind::SkeletonMage ||
                 ai->kind == MonsterAiKind::CorruptArcher || ai->kind == MonsterAiKind::Bighead) &&
                ai->kind != MonsterAiKind::SkeletonMage &&
                (ai->kind == MonsterAiKind::SkeletonBow || !monsterMeleeReach(enemy)) &&
                grid_->missileSegment(enemy.pos, targetPosition, {0x04, 1})) {
                enemy.aiRetaliate = false;
                enemy.aiWait = 0;
                beginMonsterAttack(enemy, ai->kind == MonsterAiKind::Bighead ? 2 : 1);
                continue;
            }
            if (ai->kind == MonsterAiKind::Fallen && !monsterMeleeReach(enemy))
                monsterStartApproach(enemy, 0, 75, false);
            enemy.aiRetaliate = false;
        }
        if (enemy.aiRetaliate && enemy.approach && ai && ai->kind == MonsterAiKind::Arach) {
            enemy.aiAlerted = true;
            enemy.aiRetaliate = false;
        }
        if (enemy.aiCircling) {
            const Vec before = enemy.pos;
            const int percentage = 75 + (enemy.kind == MonsterKind::Brute
                ? int((bruteWalkMultiplier(enemy) - 1.f) * 100.f + .5f) :
                enemy.kind == MonsterKind::BloodRaven ? 50 : 0);
            enemy.movementVelocityPercent = percentage;
            const auto originalSpeed = monsterMoveSpeed_ ? monsterMoveSpeed_(enemy, percentage) : std::nullopt;
            const float speed = originalSpeed.value_or(monsterDefinition(enemy.kind).speed);
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
                    missileDistance(corpse.pos, enemy.pos) >= 15) continue;
                const auto duration = monsterDeathDuration_(corpse);
                if (duration && corpse.deathAge <= *duration &&
                    fallenStartEscape(enemy, targetPosition, *grid_, movementRule(enemy))) {
                    enemy.aiCorpse = corpse.id;
                    break;
                }
            }
        if (enemy.aiEscaping) {
            const Vec before = enemy.pos;
            const int percentage = 75 + (enemy.kind == MonsterKind::Bighead ? 50 :
                enemy.kind == MonsterKind::SkeletonMage ? 25 :
                enemy.kind == MonsterKind::Fetish ? 50 :
                enemy.kind == MonsterKind::BloodRaven ? 100 :
                enemy.kind == MonsterKind::CorruptArcher ? 100 :
                enemy.kind == MonsterKind::BloodHawk && ai ? ai->params[3] :
                enemy.kind == MonsterKind::Vampire && ai ? ai->retreatVelocityBonus :
                enemy.kind == MonsterKind::Fallen ? 50 : 0);
            enemy.movementVelocityPercent = percentage;
            enemy.aiRunning = archerAi || enemy.kind == MonsterKind::BloodRaven;
            const auto originalSpeed = monsterMoveSpeed_ ? monsterMoveSpeed_(enemy, percentage) : std::nullopt;
            const float speed = originalSpeed.value_or(monsterDefinition(enemy.kind).speed);
            fallenAdvanceEscape(enemy, *grid_, speed, dt, movementRule(enemy));
            leaveSpiderWeb(enemy, (enemy.pos - before).length());
            continue;
        }
        const auto &definition = monsterDefinition(enemy.kind);
        float distance = float(missileDistance(enemy.pos, targetPosition));
        if (fallenAi && !enemy.approach && !enemy.aiCommanded && enemy.aiWait == 0 && enemy.route.empty() &&
            distance < 15.f && enemy.identity.ownerSpawnKey.empty() &&
            monsterAiRandom(enemy) % 100 < unsigned(ai->params[0]) &&
            beginFallenShout(enemy)) {
            enemy.aiCommanded = true;
            for (auto &other : state_.area.enemies)
                if (relation(enemy.id, other.id) == Relation::Allied && other.hp > 0 && other.identity.ownerSpawnKey == enemy.identity.spawnKey &&
                    other.kind == MonsterKind::Fallen && monsterAi_ && monsterAi_(other))
                    other.aiCommanded = true;
            continue;
        }
                if (!enemy.approach && enemy.aiWait > 0) continue;
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
        if (clear && (archerAi || skeletonBowAi || skeletonMageAi || bigheadAi)) {
            const auto target = combatUnit(enemy.combatTarget);
            distance = float(monsterAiDistance(*target.position, target.stats.collisionSize, enemy.pos));
        }
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
        const bool inCombat = monsterMeleeReach(enemy);
        // AITHINK rolls choose an action. Advance the accepted WL/RN action
        // until PathMisc's arrival condition or a real interruption, rather
        // than rerolling approach/run chances at every 25 Hz movement tick.
        if (!enemy.approach && ai && handleMonsterSpecialAi(enemy, *ai, distance, clear)) continue;
        if (skeletonBowAi && !enemy.approach) {
            const auto action = skeletonBowThink(enemy, *ai, distance, clear);
            if (action == SkeletonBowAction::Circle) {
                monsterStartCircle(enemy, targetPosition, 3, *grid_, movementRule(enemy));
                continue;
            }
            if (action == SkeletonBowAction::Approach) {
                const int size = monsterSize_ ? monsterSize_(enemy) : 2;
                monsterStartApproach(enemy, 0, 75, false, monsterRadiusApproachTarget(
                    enemy.pos, size, targetPosition, ai->params[3], ai->params[4]));
            }
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
        if (archerAi && !enemy.approach) {
            if (enemy.aiWait > 0) {
                enemy.route.clear();
                enemy.aiRunning = false;
                continue;
            }
            if (clear && distance < 6.f && corruptArcherRetreats(enemy, *ai) &&
                monsterStartRetreat(enemy, targetPosition, 12, *grid_, movementRule(enemy))) {
                enemy.aiRunning = true;
                continue;
            }
            if (!clear) {
                if (monsterAiRandom(enemy) % 100 < 50u)
                    monsterStartCircle(enemy, targetPosition, 3, *grid_, movementRule(enemy));
                else enemy.aiWait = float(ai->params[2]) / 25.f;
                continue;
            }
            if (corruptArcherApproaches(enemy, *ai, distance))
                monsterStartApproach(enemy, std::max(0, ai->params[7] - 1), 85, false);
            else if (distance > float(ai->params[4]))
                monsterStartApproach(enemy, std::max(0, ai->params[4] - 1), 175, true);
            else {
                enemy.route.clear();
                enemy.aiRunning = false;
                if (corruptArcherShoots(enemy, *ai)) beginMonsterAttack(enemy, 1);
                continue;
            }
        }
        if (quillRatAi && !enemy.approach && !inCombat &&
            distance < float(ai->params[0]) && clear) {
            if (quillRatShoots(enemy, *ai)) {
                beginMonsterAttack(enemy, 2);
                continue;
            }
            if (monsterStartRetreat(enemy, targetPosition, ai->params[3], *grid_, movementRule(enemy))) continue;
            if (distance < 4.f) { beginMonsterAttack(enemy, 2); continue; }
        }
        if (enemy.approach || !inCombat) {
            if (lancerAi && !enemy.approach) {
                const auto action = corruptLancerMovement(enemy, *ai,
                    distance);
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
                    enemy, *ai, distance, state_.population.difficulty);
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
            const auto fallenMove = fallenAi && !enemy.approach ? fallenMovement(enemy, *ai, distance)
                                             : FallenMovement::Approach;
            if (fallenMove == FallenMovement::Idle) {
                enemy.route.clear();
                continue;
            }
            const bool zombieWanders = zombieAi && !enemy.approach && !zombiePursues(
                enemy, *ai, distance,
                zombieForcedPursuit_ && zombieForcedPursuit_(state_.area.region));
            const bool wanders = zombieWanders || fallenMove == FallenMovement::Wander ||
                (quillRatAi && !enemy.approach);
            if (!enemy.approach && !wanders)
                monsterStartApproach(enemy, 0,
                    zombieAi ? 175 : fetishAi ? 125 : bruteAi ? 75 + int((bruteWalkMultiplier(enemy) - 1.f) * 100.f + .5f) :
                    enemy.kind == MonsterKind::BloodHawk && ai && enemy.aiCharged ? 75 + ai->params[4] : 75, zombieAi);
            const Vec movementTarget = enemy.approach && enemy.approach->destination
                ? *enemy.approach->destination : targetPosition;
            Vec destination = movementTarget;
            if (wanders) {
                discardReachedWaypoints(enemy.pos, enemy.route, .25f);
                if (!enemy.route.empty() && !grid_->segment(enemy.pos, enemy.route.front(), {}, movementRule(enemy)))
                    enemy.route.clear();
                if (enemy.route.empty())
                    if (auto target = monsterWanderTarget(enemy, *grid_, quillRatAi ? std::max(3, ai->params[3]) : 3, movementRule(enemy))) enemy.route.push_back(*target);
                if (enemy.route.empty()) continue;
                destination = enemy.route.front();
                monsterStartApproach(enemy, 0, 75, false, destination);
                enemy.rethink = 0;
            } else if (grid_->segment(enemy.pos, movementTarget, {}, movementRule(enemy))) {
                enemy.route.clear();
                enemy.rethink = 0;
            } else {
                discardReachedWaypoints(enemy.pos, enemy.route, .25f);
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
            const auto originalSpeed = monsterMoveSpeed_ ? monsterMoveSpeed_(enemy, 75) : std::nullopt;
            float speed = originalSpeed.value_or(definition.speed);
            if (enemy.approach) {
                enemy.aiRunning = enemy.approach->running;
                enemy.movementVelocityPercent = enemy.approach->velocityPercent;
                const auto runSpeed = monsterMoveSpeed_
                    ? monsterMoveSpeed_(enemy, *enemy.movementVelocityPercent) : std::nullopt;
                speed = runSpeed.value_or(speed);
            }
            const Vec before = enemy.pos;
            if (advanceMovement(enemy.pos, offset.unit(), std::min(speed * dt, offset.length()),
                [&](Vec from, Vec to) { return grid_->segment(from, to, {}, movementRule(enemy)); }).accepted) {
                const float moved = (enemy.pos - before).length();
                if (enemy.webAuraRemaining > 0) leaveSpiderWeb(enemy, moved);
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
            !archerAi && monsterMeleeReach(enemy)) {
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
