#pragma once
#include "gameplay/skills/weapon_caster.hpp"

namespace d2x {
struct CombatUnit;
struct CharacterAttributes;
struct PlayerControlInput {
    Vec direction;
    bool forceRun = false, safeZone = false;
};
struct PlayerControlContext {
    WeaponSkillCaster actions;
    const bool &running;
    const float &stamina;
    const CharacterAttributes &attributes;
    const float &chill, &webRemaining;
    const int &webPercent;
};
// World queries and accepted action requests. The local adapter alone knows
// Simulation/PlayerState; this controller selects intents from borrowed abilities.
class IPlayerControlWorld {
  public:
    virtual ~IPlayerControlWorld() = default;
    virtual CombatUnit unit(EntityId id) = 0;
    virtual bool canAttack(EntityId actor, EntityId target) const = 0;
    virtual const WeaponDamage *weapon(bool thrown, bool leftHand) const = 0;
    virtual bool meleeReach(EntityId target, const WeaponDamage &weapon) const = 0;
    virtual std::deque<Vec> path(Vec from, Vec to) const = 0;
    virtual bool segment(Vec from, Vec to) const = 0;
    virtual void beginAttack(Vec aim, EntityId target, const WeaponDamage &weapon, bool thrown, bool leftHand) = 0;
    virtual void beginWeaponSkill(const SkillCastSpec &skill, Vec aim, EntityId target) = 0;
    virtual void advanceCharge(float dt) = 0;
};
// False means block/charge consumed this phase; the caller must also skip its
// normal movement/stamina tail, matching the original fixed-step early exits.
bool advancePlayerControl(PlayerControlContext context, PlayerControlInput input, float dt, IPlayerControlWorld &world);
} // namespace d2x
