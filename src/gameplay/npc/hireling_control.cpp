#include "gameplay/npc/hireling_control.hpp"
#include "gameplay/session/session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void GameSessionImpl::controlHireling(const MonsterRecord &actor, const HirelingCombatStats &stats,
                                    const MonsterAttackTiming *timing, float dt) {
    auto &merc = simulation_->state_.player.hireling;
    const auto &player = simulation_->state_.player;
    auto open = [&](Vec position) {
        if (!map().grid.walkable(position, actor.movementRule())) return false;
        if (missileDistance(position, player.movement.pos) < 2 &&
            (position - player.movement.pos).length() <= (merc.pos - player.movement.pos).length()) return false;
        for (const auto &enemy : state().area.enemies)
            if (enemy.hp > 0 && missileDistance(position, enemy.pos) < 2 &&
                (position - enemy.pos).length() <= (merc.pos - enemy.pos).length()) return false;
        return true;
    };
    const HirelingControlWorld world{
        open,
        [&](Vec from, Vec to) { return map().grid.segment(from, to, {}, actor.movementRule()); },
        [&](Vec from, Vec to) { return map().grid.path(from, to, false, actor.movementRule()); },
        [&](int rate) { return monsterMovementPercent(actor, state().population.difficulty, rate, merc.chill > 0); },
        [&]() -> std::optional<float> {
            if (const auto *idle = monsterContent_.hirelingMotion(merc.classId, "nu")) return idle->frames / idle->duration;
            return std::nullopt;
        },
        [&]() {
            std::optional<HirelingControlTarget> target;
            int closest = 25;
            if (!region().definition.safe && actor.attack1Projectile && timing)
                for (auto candidate : simulation_->combatUnits()) {
                    const int distance = std::max(0, missileDistance(merc.pos, *candidate.position) - 2);
                    if (candidate.alive() && simulation_->canAttack(merc.id, candidate.id) && simulation_->active(*candidate.position) && distance < closest &&
                        simulation_->missilePathClear(actor.attack1Projectile->id, merc.pos, *candidate.position)) {
                        closest = distance; target = HirelingControlTarget{candidate.id, *candidate.position, distance};
                    }
                }
            return target;
        },
        [&](EntityId target, Vec aim) {
            const int baseRate = int(std::lround(timing->frames * 256.f / (timing->duration * 25.f)));
            const int actionFrame = int(std::lround(timing->impact * baseRate * 25.f / 256.f));
            const int cold = merc.chill > 0 ? actor.coldEffect.at(size_t(state().population.difficulty)) : 0;
            const int speed = effectiveAttackSpeed(baseRate, stats.weapon.fasterAttack,
                                                   stats.weapon.baseSpeed, cold + stats.combat.attackRate);
            WeaponAttackState attack;
            attack.weapon = stats.weapon.item; attack.target = target; attack.aim = aim;
            attack.timing = {"a1", timing->frames, speed, actionFrame, 0};
            merc.attackTimer = float(attack.timing.durationTicks()) / 25.f;
            merc.attack = std::move(attack);
        }
    };
    advanceHirelingControl({merc.id, merc.pos, merc.look, merc.route, merc.moving, merc.thinkTimer,
                           merc.animationTime, merc.animationRate, merc.attackBias, merc.combatRandom,
                           merc.level, merc.webSlowRemaining, merc.webSlowPercent},
                          {player.movement.pos, player.movement.moving, player.movement.runningNow},
                          {*actor.walkVelocity, actor.walkAnimationRate.value_or(0),
                           stats.fasterMoveVelocity, stats.velocityPercent}, dt, world);
}
} // namespace d2x
