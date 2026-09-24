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
#include "gameplay/monsters/bighead_ai.hpp"
#include "gameplay/monsters/skeleton_mage_ai.hpp"
#include "gameplay/monsters/monster_wander.hpp"
#include <algorithm>

namespace d2x {
void Simulation::updateMonsters(float dt) {
    auto &player = state_.player;
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
        if (enemy.hp < enemy.maxHp && monsterDamageRegen_)
            if (auto rate = monsterDamageRegen_(enemy, state_.area.region)) {
                const auto perFrame = int(enemy.maxHp * 256.f * *rate / 4096.f);
                enemy.hp = std::min(enemy.maxHp, enemy.hp + perFrame / 256.f * dt * 25.f);
            }
        enemy.chill = std::max(0.f, enemy.chill - dt);
        enemy.stun = std::max(0.f, enemy.stun - dt);
        enemy.rethink = std::max(0.f, enemy.rethink - dt);
        enemy.aiWait = std::max(0.f, enemy.aiWait - dt);
        if (player.dead || player.hp <= 0) {
            enemy.route.clear();
            enemy.aiPursuing = false;
            enemy.aiEscaping = false;
            enemy.aiCommanded = false;
            enemy.aiCircling = false;
            enemy.aiRunning = false;
            enemy.aiRetaliate = false;
            enemy.aiCharged = false;
            enemy.aiAdvanceRemaining = 0;
            enemy.skill2Remaining = enemy.skill2Duration = 0;
            enemy.attack = enemy.attackDuration = 0;
            enemy.attackImpact = -1;
            enemy.attackMode = 1;
            continue;
        }
        if (enemy.stun > 0) {
            enemy.attack = enemy.attackDuration = 0;
            enemy.attackImpact = -1;
            enemy.attackMode = 1;
            enemy.skill2Remaining = enemy.skill2Duration = 0;
            continue;
        }
        if (enemy.kind == MonsterKind::QuillRat && enemy.hitFlash > 0) continue;
        if (enemy.skill2Remaining > 0) {
            enemy.skill2Remaining = std::max(0.f, enemy.skill2Remaining - dt);
            if (enemy.skill2Remaining == 0) enemy.skill2Duration = 0;
            continue;
        }
        if (enemy.attack > 0) {
            enemy.attack = std::max(0.f, enemy.attack - dt);
            if (enemy.attackImpact >= 0) {
                enemy.attackImpact -= dt;
                if (enemy.attackImpact <= 0) {
                    enemy.attackImpact = -1;
                    if (monsterProjectile_ && monsterProjectile_(enemy, enemy.attackMode))
                        launchMonsterProjectile(enemy);
                    else
                        resolveMonsterAttack(enemy);
                }
            }
            if (enemy.attack == 0) {
                enemy.attackDuration = 0;
                enemy.attackImpact = -1;
                enemy.attackMode = 1;
            }
            continue;
        }
        if (enemy.aiRetaliate && enemy.hitFlash <= 0) {
            enemy.aiRetaliate = false;
            const auto ai = monsterAi_ ? monsterAi_(enemy) : std::nullopt;
            if (ai && ai->kind == MonsterAiKind::QuillRat &&
                (player.pos - enemy.pos).length() < monsterDefinition(enemy.kind).sightRange &&
                grid_->segment(enemy.pos, player.pos)) {
                enemy.route.clear();
                enemy.aiEscaping = false;
                beginMonsterAttack(enemy, 2);
                continue;
            }
        }
        if (enemy.aiCircling) {
            const auto originalSpeed = monsterWalkSpeed_ ? monsterWalkSpeed_(enemy) : std::nullopt;
            const float speed = originalSpeed.value_or(monsterDefinition(enemy.kind).speed) *
                                (enemy.kind == MonsterKind::Brute ? bruteWalkMultiplier(enemy) : 1.f) *
                                (enemy.chill > 0 ? .42f : 1.f);
            monsterAdvanceCircle(enemy, *grid_, speed, dt);
            continue;
        }
        const auto ai = monsterAi_ ? monsterAi_(enemy) : std::nullopt;
        const bool fallenAi = ai && ai->kind == MonsterAiKind::Fallen;
        const bool rogueAi = ai && ai->kind == MonsterAiKind::CorruptRogue;
        const bool lancerAi = ai && ai->kind == MonsterAiKind::CorruptLancer;
        const bool archerAi = ai && ai->kind == MonsterAiKind::CorruptArcher;
        if (fallenAi && !enemy.aiEscaping && monsterDeathDuration_)
            for (const auto &corpse : state_.area.enemies) {
                if (corpse.hp > 0 || corpse.id == enemy.id ||
                    (corpse.pos - enemy.pos).length() >= 15.f) continue;
                const auto duration = monsterDeathDuration_(corpse);
                if (duration && corpse.deathAge <= *duration &&
                    fallenStartEscape(enemy, player.pos, *grid_)) break;
            }
        if (enemy.aiEscaping) {
            const auto originalSpeed = monsterWalkSpeed_ ? monsterWalkSpeed_(enemy) : std::nullopt;
            const float speed = originalSpeed.value_or(monsterDefinition(enemy.kind).speed) *
                                (enemy.kind == MonsterKind::Bighead ? .5f :
                                 enemy.kind == MonsterKind::SkeletonMage ? .25f :
                                 enemy.kind == MonsterKind::QuillRat ? 1.f : 1.5f) *
                                (enemy.chill > 0 ? .42f : 1.f);
            fallenAdvanceEscape(enemy, *grid_, speed, dt);
            continue;
        }
        const auto &definition = monsterDefinition(enemy.kind);
        auto delta = player.pos - enemy.pos;
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
                if (other.hp > 0 && other.identity.group == enemy.identity.group &&
                    other.kind == MonsterKind::Fallen && monsterAi_ && monsterAi_(other))
                    other.aiCommanded = true;
            continue;
        }
        if (distance >= definition.sightRange) {
            enemy.route.clear();
            enemy.rethink = 0;
            enemy.aiPursuing = false;
            enemy.aiRunning = false;
            enemy.aiAdvanceRemaining = 0;
            continue;
        }
        const bool skeletonAi = ai && ai->kind == MonsterAiKind::Skeleton;
        const bool bruteAi = ai && ai->kind == MonsterAiKind::Brute;
        const bool zombieAi = ai && ai->kind == MonsterAiKind::Zombie;
        const bool goatmanAi = ai && ai->kind == MonsterAiKind::Goatman;
        const bool quillRatAi = ai && ai->kind == MonsterAiKind::QuillRat;
        const bool wraithAi = ai && ai->kind == MonsterAiKind::Wraith;
        const bool skeletonBowAi = ai && ai->kind == MonsterAiKind::SkeletonBow;
        const bool bigheadAi = ai && ai->kind == MonsterAiKind::Bighead;
        const bool skeletonMageAi = ai && ai->kind == MonsterAiKind::SkeletonMage;
        bool clear = grid_->segment(enemy.pos, player.pos);
        if (skeletonMageAi) {
            auto action = skeletonMageThink(enemy, *ai, distance, clear);
            if (action == SkeletonMageAction::Retreat)
                action = monsterStartRetreat(enemy, player.pos, 5, *grid_)
                    ? SkeletonMageAction::Idle : SkeletonMageAction::Fire;
            if (action == SkeletonMageAction::Circle) {
                if (!monsterStartCircle(enemy, player.pos, 4, *grid_))
                    enemy.aiWait = float(ai->params[7]) / 25.f;
                action = SkeletonMageAction::Idle;
            }
            if (action == SkeletonMageAction::Fire) {
                enemy.route.clear();
                beginMonsterAttack(enemy, 1);
                continue;
            }
            if (action == SkeletonMageAction::Idle) {
                if (!enemy.aiEscaping && !enemy.aiCircling) enemy.route.clear();
                continue;
            }
        }
        if (bigheadAi) {
            auto action = bigheadThink(enemy, *ai, distance, clear, definition.attackRange);
            if (action == BigheadAction::Retreat)
                action = monsterStartRetreat(enemy, player.pos, 5, *grid_)
                    ? BigheadAction::Idle : BigheadAction::Fire;
            if (action == BigheadAction::Circle) {
                if (!monsterStartCircle(enemy, player.pos, 4, *grid_))
                    enemy.aiWait = 10.f / 25.f;
                action = BigheadAction::Idle;
            }
            if (action == BigheadAction::Melee || action == BigheadAction::Fire) {
                enemy.route.clear();
                beginMonsterAttack(enemy, action == BigheadAction::Fire ? 2 : 1);
                continue;
            }
            if (action == BigheadAction::Idle) {
                if (!enemy.aiEscaping && !enemy.aiCircling) enemy.route.clear();
                continue;
            }
        }
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
                monsterStartRetreat(enemy, player.pos, 12, *grid_)) {
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
            if (monsterStartRetreat(enemy, player.pos, ai->params[3], *grid_)) continue;
            if (distance < 4.f) {
                beginMonsterAttack(enemy, 2);
                continue;
            }
        }
        if (distance >= definition.attackRange || !clear) {
            if (archerAi) enemy.aiRunning = distance > float(ai->params[4]);
            if (lancerAi) {
                const auto action = corruptLancerMovement(enemy, *ai, distance);
                if (action == CorruptLancerMovement::Idle) {
                    enemy.route.clear();
                    enemy.aiRunning = false;
                    continue;
                }
                enemy.aiRunning = action == CorruptLancerMovement::Run;
            }
            if (rogueAi && enemy.aiAdvanceRemaining <= 0) {
                const auto action = corruptRogueMovement(
                    enemy, *ai, distance, state_.population.difficulty);
                if (action == CorruptRogueMovement::Idle) {
                    enemy.route.clear();
                    continue;
                }
                enemy.aiRunning = action == CorruptRogueMovement::Run;
                enemy.aiAdvanceRemaining = enemy.aiRunning ? 3.f : 1.f;
            }
            if (skeletonAi && !skeletonApproaches(enemy, *ai)) {
                enemy.route.clear();
                continue;
            }
            if (goatmanAi && !goatmanApproaches(enemy, *ai)) {
                enemy.route.clear();
                continue;
            }
            if (wraithAi && !wraithApproaches(enemy, *ai)) {
                enemy.route.clear();
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
            Vec destination = player.pos;
            if (wanders) {
                while (!enemy.route.empty() && (enemy.route.front() - enemy.pos).length() < .25f)
                    enemy.route.pop_front();
                if (!enemy.route.empty() && !grid_->segment(enemy.pos, enemy.route.front()))
                    enemy.route.clear();
                if (enemy.route.empty())
                    if (auto target = monsterWanderTarget(enemy, *grid_, 3)) enemy.route.push_back(*target);
                if (enemy.route.empty()) continue;
                destination = enemy.route.front();
                enemy.rethink = 0;
            } else if (clear) {
                enemy.route.clear();
                enemy.rethink = 0;
            } else {
                while (!enemy.route.empty() && (enemy.route.front() - enemy.pos).length() < .25f)
                    enemy.route.pop_front();
                if (!enemy.route.empty() && !grid_->segment(enemy.pos, enemy.route.front())) {
                    enemy.route.clear();
                    enemy.rethink = 0;
                }
                if (enemy.rethink <= 0) {
                    enemy.route = grid_->path(enemy.pos, player.pos);
                    enemy.rethink = .7f;
                }
                if (enemy.route.empty()) {
                    if (fallenAi) enemy.aiCommanded = false;
                    if (rogueAi) {
                        enemy.aiRunning = false;
                        enemy.aiAdvanceRemaining = 0;
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
            if (rogueAi && enemy.aiRunning) {
                const auto runSpeed = monsterRunSpeed_ ? monsterRunSpeed_(enemy) : std::nullopt;
                speed = runSpeed.value_or(originalSpeed.value_or(definition.speed)) *
                        (1.f + float(ai->params[3]) / 100.f) * (enemy.chill > 0 ? .42f : 1.f);
            }
            if ((lancerAi || archerAi) && enemy.aiRunning) {
                const auto runSpeed = monsterRunSpeed_ ? monsterRunSpeed_(enemy) : std::nullopt;
                speed = runSpeed.value_or(originalSpeed.value_or(definition.speed)) *
                        (enemy.chill > 0 ? .42f : 1.f);
            }
            if (bruteAi) speed *= bruteWalkMultiplier(enemy);
            if (zombieAi && !zombieWanders) speed *= 4.f / 3.f;
            auto next = enemy.pos + offset.unit() * std::min(speed * dt, offset.length());
            if (grid_->segment(enemy.pos, next)) {
                const float moved = (next - enemy.pos).length();
                enemy.pos = next;
                if (rogueAi || skeletonBowAi || skeletonMageAi) {
                    enemy.aiAdvanceRemaining = std::max(0.f, enemy.aiAdvanceRemaining - moved);
                    if (rogueAi && enemy.aiAdvanceRemaining == 0) enemy.aiRunning = false;
                }
            } else {
                enemy.route.clear();
                enemy.rethink = 0;
                if (fallenAi) enemy.aiCommanded = false;
                if (rogueAi) {
                    enemy.aiRunning = false;
                    enemy.aiAdvanceRemaining = 0;
                }
                if (lancerAi) enemy.aiRunning = false;
                if (archerAi) enemy.aiRunning = false;
            }
        } else {
            enemy.route.clear();
            enemy.rethink = 0;
            if (rogueAi) {
                enemy.aiRunning = false;
                enemy.aiAdvanceRemaining = 0;
            }
            if (lancerAi) enemy.aiRunning = false;
            if (archerAi) enemy.aiRunning = false;
        }
        if (!skeletonBowAi && !skeletonMageAi && !bigheadAi &&
            (player.pos - enemy.pos).length() < definition.attackRange &&
            player.leapTime <= 0 && grid_->segment(enemy.pos, player.pos)) {
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
                    if (!monsterStartCircle(enemy, player.pos, 4, *grid_))
                        enemy.aiWait = 15.f / 25.f;
                    continue;
                }
            }
            if (rogueAi && !corruptRogueAttacks(enemy, *ai)) continue;
            if (goatmanAi && !goatmanAttacks(enemy, *ai)) continue;
            if (wraithAi && !wraithAttacks(enemy, *ai)) continue;
            if (lancerAi && !corruptLancerAttacks(enemy, *ai)) continue;
            beginMonsterAttack(enemy);
        }
    }
}
} // namespace d2x
