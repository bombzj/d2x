#include "gameplay/simulation/simulation.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/caster.hpp"
#include "gameplay/skills/weapon_caster.hpp"
#include "gameplay/units/restoration.hpp"
#include "gameplay/units/impairments.hpp"
#include <algorithm>

namespace d2x {
void Simulation::beginPlayerStep(float dt, const PlayerFrameInput &input) {
    auto &p = state_.player;
    forceRun_ = input.forceRun;
    p.movement.previous = p.movement.pos;
    ++state_.frame;
    if (p.actions.blockAnimation && ++p.actions.blockAnimation->ticks >= p.actions.blockAnimation->timing.durationTicks())
        p.actions.blockAnimation.reset();
    state_.time += dt;
    if (auto removed = p.combatEffects.expire(state_.frame); !removed.empty())
        combatEffectsChanged(removed);
    if (!p.skills.aura || !p.combatEffects.hasState(p.skills.aura->definition.ownerState.id, state_.frame))
        p.skills.auraSuppressesManaRegen = false;
    skills().advanceSkillCasting(skillCaster(p.id), dt, input.direction.length() > .1f);
    skills().advanceWeaponAttack(skillWeaponCaster(p.id));
    p.actions.castTime = std::max(0.f, p.actions.castTime - dt);
    p.actions.hitTime = std::max(0.f, p.actions.hitTime - dt);
    advanceImpairments({&p.resources.chill, nullptr, nullptr, nullptr,
                       {&p.resources.webSlowRemaining, &p.resources.webSlowPercent, &p.resources.webSource}},
                      dt, WebSlowExpiry::ClearMetadata);
    p.movement.moving = false;
    if (p.actions.dead)
        p.actions.deathTime += dt;
}
void Simulation::advancePlayerStep(float dt, const PlayerFrameInput &input) {
    auto &p = state_.player;
    if (!p.actions.dead) {
        restoreResource(p.resources.healing, p.resources.hp, float(p.attributes.maxLife), dt);
        restoreResource(p.resources.manaRestoration, p.resources.mana, float(p.attributes.maxMana), dt);
        updatePlayer(dt, input.direction);
        skills().createBlazeTrail(skillCaster(p.id));
        activateMonsters();
    }
}
void Simulation::finishPlayerStep() {
    auto &p = state_.player;
    if (!p.actions.dead && p.resources.hp <= 0) {
        p.actions.dead = true;
        p.actions.deathCompleted = false;
        p.actions.deathTime = 0; p.actions.deathCorpse = {};
        p.resources.hp = 0;
        p.movement.moving = false;
        combatEffectsChanged(p.combatEffects.onDeath(EffectUnitKind::Player));
        skills().stopChannel(skillCaster(p.id));
        p.skills.pendingCast.reset();
        p.actions.weaponAttack.reset();
        p.actions.charge.reset();
        p.actions.blockAnimation.reset();
        p.actions.approachSkill.reset();
        p.actions.attackPosition.reset();
        p.actions.meleeTime = 0;
        p.actions.castTime = 0;
        p.actions.hitTime = 0;
        p.resources.healing.clear();
        p.resources.manaRestoration.clear();
        p.resources.chill = 0;
        p.resources.poisonRemaining = p.resources.poisonPerSecond = 0;
        p.movement.route.clear();
        p.actions.attackTarget = {};
        p.actions.throwAttack = false;
        p.actions.leftHandAttack = false;
        state_.message = "You have died. Press Esc to return to town.";
        emit(PlayerDied{p.id});
    }
}
void Simulation::finishPlayerDeathAnimation() {
    auto &p = state_.player;
    if (!p.actions.dead || p.actions.deathCompleted) return;
    p.actions.deathCompleted = true;
    // PlrModes.sub_6FC81250 handles every registered pet type at DT -> DEAD,
    // not at the lethal hit. Hireables keep their record for paid resurrection.
    if (p.hireling.active()) {
        finishHirelingDeath();
        emit(UnitDied{p.hireling.id, false, p.hireling.pos, p.hireling.collisionSize});
    }
    for (auto &pet : state_.companions)
        if (pet.allegiance.owner == p.id) finishCompanionDeath(pet);
}
} // namespace d2x
