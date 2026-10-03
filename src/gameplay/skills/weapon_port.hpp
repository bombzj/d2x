#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include <deque>

namespace d2x {
struct CombatUnit;
struct WeaponDamage;
struct WeaponSkillSpec;
struct SkillCastSpec;
struct AttackElements;
class ISkillWeaponWorld {
  public:
    virtual ~ISkillWeaponWorld() = default;
    virtual bool meleeReach(EntityId actor, EntityId target, const WeaponDamage &weapon) const = 0;
    virtual bool beginWeaponAttack(EntityId actor, Vec aim, EntityId target, const WeaponDamage &weapon,
                                   bool thrown, bool leftHand, const WeaponSkillSpec *skill) = 0;
    virtual void requestWeaponAttack(EntityId actor, EntityId target, Vec aim) = 0;
    virtual std::deque<Vec> approach(EntityId actor, Vec target) const = 0;
    virtual bool movementSegment(EntityId actor, Vec from, Vec to) const = 0;
    virtual void fireWeaponProjectile(EntityId actor, Vec aim, const WeaponDamage &weapon,
                                     bool thrown, const SkillCastSpec *skill) = 0;
    virtual void weaponMelee(EntityId actor, EntityId target, const WeaponDamage &weapon,
                             const SkillCastSpec *skill) = 0;
    virtual AttackElements rollWeaponElements(EntityId actor, EntityId weapon) = 0;
    virtual void weaponHit(EntityId actor, EntityId target, float amount, const AttackElements &elements) = 0;
    virtual bool hasEquipmentWear() const = 0;
    virtual void wearWeapon(EntityId item) = 0;
    virtual bool canStun(EntityId target) const = 0;
    virtual void stun(EntityId target, int frames) = 0;
    virtual void convert(EntityId actor, const CombatUnit &target, const SkillCastSpec &skill, int level) = 0;
};
} // namespace d2x
