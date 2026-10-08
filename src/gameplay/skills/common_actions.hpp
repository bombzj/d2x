#pragma once
#include "gameplay/combat/weapon_values.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/combat/geometry.hpp"
#include <algorithm>

namespace d2x {
// SrvDo001/003/005 select equipment, not a skill-specific item-code list.
// Migrated from master's selectAttackWeapon; no world or resource access.
inline const WeaponDamage *commonAttackWeapon(const EquipmentStats &equipment, bool thrown, bool leftHand) {
    for (int index = 0; index < equipment.weaponCount; ++index) {
        const auto &weapon = equipment.weapons[size_t(index)];
        if (leftHand && (!weapon.item || !weapon.leftHand)) continue;
        if (thrown && !weapon.throwable) return nullptr;
        if (!thrown && weapon.potion) return nullptr;
        return &weapon;
    }
    return nullptr;
}
// MISSILES_CreateMissileFromParams flag 0x400: lifetime changes, velocity does not.
inline int groundThrowFrames(Vec origin, Vec target, int velocityUnits) {
    if (velocityUnits <= 0) return 0;
    return std::max(1, int(int64_t(std::max(1, missileDistance(origin, target))) * 4096 / velocityUnits));
}
inline SkillCastSpec commonProjectileSkill(SkillCastSpec skill, const WeaponProjectileSpec &projectile) {
    skill.missileId = projectile.id;
    skill.missileVelocity = projectile.speed;
    skill.missileLifetime = projectile.lifetime;
    skill.missileImpact = projectile.impact;
    return skill;
}
}
