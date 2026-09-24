#include "gameplay/simulation/simulation.hpp"
#include "gameplay/monsters/bighead_ai.hpp"
#include "gameplay/monsters/skeleton_mage_ai.hpp"
#include "gameplay/monsters/fetish_ai.hpp"
#include "gameplay/monsters/vampire_ai.hpp"
#include "gameplay/monsters/fallen_shaman_ai.hpp"
#include "gameplay/monsters/blood_hawk_ai.hpp"
#include "gameplay/monsters/monster_wander.hpp"
#include <algorithm>

namespace d2x {
bool Simulation::handleMonsterSpecialAi(Enemy &enemy, const MonsterAiProfile &ai,
                                        float distance, bool clear) {
    const auto &player = state_.player;
    if (ai.kind == MonsterAiKind::FoulCrowNest) {
        enemy.route.clear();
        if (distance <= 20.f && enemy.aiWait == 0 &&
            enemy.aiLoop < ai.params[2] && monsterNest_ &&
            monsterNest_(enemy) && monsterAttackTiming_ &&
            monsterAttackTiming_(enemy, 3))
            beginMonsterAttack(enemy, 3);
        return true;
    }
    if (ai.kind == MonsterAiKind::BloodHawk) {
        const bool inCombat = clear &&
            distance < monsterDefinition(enemy.kind).attackRange && player.leapTime <= 0;
        const auto action = bloodHawkThink(enemy, ai, distance, inCombat);
        if (action == BloodHawkAction::Attack) {
            enemy.route.clear();
            beginMonsterAttack(enemy, 1);
            return true;
        }
        if (action == BloodHawkAction::Retreat) {
            if (!monsterStartRetreat(enemy, player.pos, 4, *grid_))
                beginMonsterAttack(enemy, 1);
            return true;
        }
        if (action == BloodHawkAction::Circle) {
            if (monsterStartCircle(enemy, player.pos, 4, *grid_)) return true;
        }
        return false;
    }
    if (ai.kind == MonsterAiKind::FallenShaman) {
        Enemy *corpse = nullptr;
        float closest = float(ai.params[3]);
        for (auto &candidate : state_.area.enemies) {
            if (candidate.hp > 0 || candidate.id == enemy.id ||
                (candidate.kind != MonsterKind::Fallen &&
                 candidate.kind != MonsterKind::FallenShaman) ||
                monsterImplementation(candidate.identity.monster).substitute ||
                (candidate.identity.rank != MonsterRank::Normal &&
                 candidate.identity.rank != MonsterRank::Minion)) continue;
            const float separation = (candidate.pos - enemy.pos).length();
            if (separation > closest) continue;
            const auto duration = monsterDeathDuration_
                ? monsterDeathDuration_(candidate) : std::nullopt;
            if (!duration || candidate.deathAge < *duration) continue;
            closest = separation;
            corpse = &candidate;
        }
        const bool inCombat = clear &&
            distance < monsterDefinition(enemy.kind).attackRange && player.leapTime <= 0;
        const auto decision = fallenShamanThink(enemy, ai, distance, inCombat, corpse != nullptr);
        if (decision.commandMinions)
            for (auto &other : state_.area.enemies)
                if (other.hp > 0 && other.kind == MonsterKind::Fallen &&
                    other.identity.group == enemy.identity.group &&
                    !monsterImplementation(other.identity.monster).substitute)
                    other.aiCommanded = true;
        if (decision.action == FallenShamanAction::Resurrect && corpse) {
            enemy.aiCorpse = corpse->id;
            enemy.route.clear();
            beginMonsterAttack(enemy, 3);
            return true;
        }
        if (decision.action == FallenShamanAction::Fire) {
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
            !monsterStartCircle(enemy, player.pos, 3, *grid_))
            enemy.aiWait = 10.f / 25.f;
        if (!enemy.aiCircling) enemy.route.clear();
        return true;
    }
    if (ai.kind == MonsterAiKind::Vampire) {
        const bool inCombat = clear && distance < monsterDefinition(enemy.kind).attackRange &&
                              player.leapTime <= 0;
        const auto action = vampireThink(enemy, ai, distance, inCombat);
        if (action == VampireAction::Retreat) {
            if (!monsterStartRetreat(enemy, player.pos, 8, *grid_))
                enemy.aiWait = 10.f / 25.f;
            return true;
        }
        if (action == VampireAction::Circle) {
            if (!monsterStartCircle(enemy, player.pos, 4, *grid_))
                enemy.aiWait = 10.f / 25.f;
            return true;
        }
        if (action == VampireAction::Attack || action == VampireAction::CastFirst ||
            action == VampireAction::CastFourth) {
            enemy.route.clear();
            beginMonsterAttack(enemy, action == VampireAction::CastFirst ? 3 :
                                      action == VampireAction::CastFourth ? 6 : 1);
            return true;
        }
        if (action == VampireAction::Idle) {
            enemy.route.clear();
            return true;
        }
    }
    if (ai.kind == MonsterAiKind::Fetish) {
        const bool inCombat = clear && distance < monsterDefinition(enemy.kind).attackRange &&
                              player.leapTime <= 0;
        const int lifePercent = characterStats_.maxLife > 0
            ? std::clamp(int(player.hp * 100.f / float(characterStats_.maxLife)), 0, 100) : 0;
        const auto action = fetishThink(enemy, ai, distance, inCombat, lifePercent);
        if (action == FetishAction::Retreat) {
            if (!monsterStartRetreat(enemy, player.pos, 14, *grid_))
                fetishRetreatFailed(enemy);
            return true;
        }
        if (action == FetishAction::Circle) {
            if (!monsterStartCircle(enemy, player.pos, 4, *grid_))
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
            action = monsterStartRetreat(enemy, player.pos, 5, *grid_)
                ? SkeletonMageAction::Idle : SkeletonMageAction::Fire;
        if (action == SkeletonMageAction::Circle) {
            if (!monsterStartCircle(enemy, player.pos, 4, *grid_))
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
        auto action = bigheadThink(enemy, ai, distance, clear,
                                   monsterDefinition(enemy.kind).attackRange);
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
