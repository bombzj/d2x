#include "gameplay/units/movement.hpp"
#include "gameplay/units/actions.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/monsters/monster_wander.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void Simulation::applyAuraKnockback(EntityId attacker, EntityId defender) {
    const auto source = combatUnit(attacker), target = combatUnit(defender);
    if (!source || !target.alive() || !target.monster) return;
    auto &enemy = *target.records.monster;
    const auto duration = monsterKnockbackDuration_ ? monsterKnockbackDuration_(enemy) : std::nullopt;
    if (!duration) { recoverUnit(defender, attacker, 1, true, 13, true); return; }
    const int offsetX = int(std::floor(enemy.pos.x)) - int(std::floor(source.position->x));
    const int offsetY = int(std::floor(enemy.pos.y)) - int(std::floor(source.position->y));
    const int divisor = std::max(std::abs(offsetX), std::abs(offsetY));
    Vec destination = enemy.pos;
    if (divisor > 0) destination = {std::floor(enemy.pos.x) + float(offsetX * 3 / divisor) + .5f,
        std::floor(enemy.pos.y) + float(offsetY * 3 / divisor) + .5f};
    monsterStopApproach(enemy);
    enemy.route.clear();
    cancelTimedAction({enemy.attack, enemy.attackDuration, enemy.attackImpact});
    enemy.skill2Remaining = enemy.skill2Duration = 0;
    enemy.teleportTarget.reset();
    enemy.nestSpawnPosition.reset();
    enemy.knockbackRemaining = enemy.knockbackDuration = *duration;
    enemy.knockbackDestination = destination;
    enemy.knockbackFacing = *source.position - enemy.pos;
}
bool Simulation::advanceAuraKnockback(Enemy &enemy, float dt) {
    if (enemy.knockbackRemaining <= 0 || !enemy.knockbackDestination) return false;
    const Vec delta = *enemy.knockbackDestination - enemy.pos;
    if (!advanceMovement(enemy.pos, delta.unit(), std::min(delta.length(), 25.f * dt),
        [&](Vec from, Vec to) { return grid_->segment(from, to, {}, movementRule(enemy)); }).accepted)
        enemy.knockbackDestination = enemy.pos;
    enemy.knockbackRemaining = std::max(0.f, enemy.knockbackRemaining - dt);
    if (enemy.knockbackRemaining == 0) {
        enemy.knockbackDestination.reset();
        recoverUnit(enemy.id, {}, 1, false, 160, true);
    }
    return true;
}
} // namespace d2x
