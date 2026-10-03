#include "gameplay/player/control.hpp"
#include "gameplay/units/actions.hpp"
#include "gameplay/units/resources.hpp"
#include "gameplay/skills/weapon_caster.hpp"
#include "gameplay/skills/caster.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/simulation/simulation.hpp"

namespace d2x {
class SimulationPlayerControl final : public IPlayerControlWorld {
    Simulation &simulation_;
  public:
    explicit SimulationPlayerControl(Simulation &simulation) : simulation_(simulation) {}
    CombatUnit unit(EntityId id) override { return simulation_.combatUnit(id); }
    bool canAttack(EntityId actor, EntityId target) const override { return simulation_.canAttack(actor, target); }
    const WeaponDamage *weapon(bool thrown, bool leftHand) const override { return simulation_.attackWeapon(thrown, leftHand); }
    bool meleeReach(EntityId target, const WeaponDamage &weapon) const override { return simulation_.meleeReach(target, weapon); }
    std::deque<Vec> path(Vec from, Vec to) const override { return simulation_.grid_->path(from, to, false, playerMovement); }
    bool segment(Vec from, Vec to) const override { return simulation_.grid_->segment(from, to, {}, playerMovement); }
    void beginAttack(Vec aim, EntityId target, const WeaponDamage &weapon, bool thrown, bool leftHand) override {
        simulation_.beginWeaponAttack(aim, target, weapon, thrown, leftHand);
    }
    void beginWeaponSkill(const SkillCastSpec &skill, Vec aim, EntityId target) override {
        simulation_.skills().beginWeaponSkill(simulation_.skillWeaponCaster(simulation_.state_.player.id), skill, aim, target);
    }
    void advanceCharge(float dt) override {
        simulation_.skills().advanceCharge(simulation_.skillWeaponCaster(simulation_.state_.player.id), dt);
    }
};

void Simulation::stopWalking() {
    stopActorMovement(skillWeaponCaster(state_.player.id));
}
void Simulation::moveTo(Vec target) {
    skills().stopChannel(skillCaster(state_.player.id));
    auto &p = state_.player;
    if (p.actions.dead)
        return;
    p.actions.charge.reset();
    p.actions.approachSkill.reset();
    p.actions.attackTarget = {};
    p.actions.attackPosition.reset();
    p.actions.throwAttack = false;
    p.actions.leftHandAttack = false;
    p.movement.route = grid_->path(p.movement.pos, target, true, playerMovement);
    state_.message = p.movement.route.empty() ? "That path is blocked" : "";
}
void Simulation::updatePlayer(float dt, Vec keyboard) {
    auto &p = state_.player;
    advanceResourceRecovery(p.resources.hp, p.resources.mana,
        {p.attributes.maxLife, p.attributes.maxMana, p.attributes.manaRegen,
         p.attributes.combat.replenishLife, p.skills.auraSuppressesManaRegen}, dt);
    SimulationPlayerControl world(*this);
    if (!advancePlayerControl({skillWeaponCaster(p.id), p.movement.running, p.resources.stamina, p.attributes,
                               p.resources.chill, p.resources.webSlowRemaining, p.resources.webSlowPercent},
                              {keyboard, forceRun_, safeZone_}, dt, world)) return;
    const bool idle = !p.movement.moving && p.actions.castTime <= 0 && p.actions.meleeTime <= 0 && p.actions.hitTime <= 0 &&
                      p.skills.channelSkill() < 0;
    advanceStamina(p.resources.stamina,
        {p.attributes.maxStamina, p.attributes.staminaDrain, p.attributes.staminaRecoveryBonus},
        {p.movement.moving, p.movement.runningNow, idle, safeZone_}, dt);
}
} // namespace d2x
