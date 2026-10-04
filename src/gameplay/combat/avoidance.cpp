// D2MOO SUnitDmg::ApplyDodge / result animation (MIT).
#include "avoidance.hpp"
#include "gameplay/simulation/unit_records.hpp"
#include "gameplay/player/state.hpp"
#include "gameplay/monsters/state.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x {
int avoidCombatHit(RuntimeCombatUnit target, bool ranged,
                   const std::function<std::optional<WeaponAttackTiming>()> &animation) {
    if (!target.alive() || !target.random) return -1;
    const bool moving = target.player ? target.records.player->movement.moving :
        target.monster && !target.records.monster->route.empty() && target.records.monster->attack <= 0;
    const auto &combat = target.stats.attributes.combat;
    const int chance = moving ? combat.evade : ranged ? combat.avoid : combat.dodge;
    if (chance <= 0 || limitedRandom(*target.random, 100) >= unsigned(std::clamp(chance, 0, 100))) return -1;
    const int id = moving ? 29 : ranged ? 18 : 13;
    // Evade preserves movement; summoned monsters do not use player S1.
    if (!target.player || moving) return id;
    const auto timing = animation();
    if (!timing) return id;
    auto &player = *target.records.player;
    player.skills.pendingCast.reset(); player.skills.channel.reset(); player.skills.lightningSequence = false;
    player.actions.weaponAttack.reset(); player.actions.castTime = player.actions.meleeTime = 0;
    player.actions.hitTime = 0; player.actions.charge.reset(); player.actions.approachSkill.reset();
    player.movement.route.clear(); player.actions.attackTarget = {}; player.actions.attackPosition.reset();
    WeaponAttackState dodge; dodge.aim = player.movement.pos; dodge.timing = *timing;
    player.actions.blockAnimation = std::move(dodge);
    return id;
}
} // namespace d2x
