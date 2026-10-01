#pragma once
#include "inventory.hpp"
#include "gameplay/combat/stat_modifiers.hpp"

namespace d2x {
struct WeaponDamage {
    EntityId item;
    int minimum = 256, maximum = 512;
    bool ranged = false;
    bool throwable = false;
    bool leftHand = false;
    bool potion = false;
    int rangeAdder = 0, baseSpeed = 0, attackRating = 0, fasterAttack = 0;
    std::string weaponClass = "hth";
    int throwMinimum = 0, throwMaximum = 0;
    int projectileMinimum = 0, projectileMaximum = 0, projectileDamagePercent = 0;
    int meleeBaseMinimum = 256, meleeBaseMaximum = 512, damagePercent = 0;
    int minimumDamagePercent = 0, maximumDamagePercent = 0;
    int baseAttackRating = 0, attackRatingPercent = 0;
    AttackTargetModifiers target = {};
    bool blunt = false;
    std::optional<WeaponProjectileSpec> projectile = {};
    std::vector<std::string> types = {};
    int hitClass = 1;
};
struct EquipmentStats {
    std::string animationClass = "hth";
    std::array<std::string, size_t(EquipmentSlot::Count)> appearanceDefinitions{};
    std::array<WeaponDamage, 2> weapons;
    int weaponCount = 1;
    int defense = 0, blockChance = 0;
    int level = 1;
};
EquipmentStats deriveEquipmentStats(const InventoryService &inventory, const PlayerContainers &containers,
                                    const EquipmentActor &actor, int bonusDefense = 0,
                                    const CombatModifiers &combat = {}, int baseAttackRating = 0);
} // namespace d2x
