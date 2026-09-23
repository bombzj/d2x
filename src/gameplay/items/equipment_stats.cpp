#include "equipment_stats.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace d2x {
EquipmentStats deriveEquipmentStats(const InventoryService &inventory, const PlayerContainers &containers,
                                    const EquipmentActor &actor, int bonusDefense) {
    EquipmentStats result;
    result.level = std::max(1, actor.level);
    int count = 0;
    auto usable = [&](EquipmentSlot slot) -> const ItemInstance * {
        const auto *item = inventory.item(inventory.equipped(containers, slot));
        if (!item)
            return nullptr;
        const auto &definition = *inventory.catalog().find(item->definition);
        if ((definition.maxDurability && item->durability == 0) ||
            inventory.equipmentRequirements(item->handle(), actor) != InventoryError::None)
            return nullptr;
        return item;
    };
    auto right = usable(EquipmentSlot::RightHand);
    auto left = usable(EquipmentSlot::LeftHand);
    result.defense = actor.dexterity / 4 + bonusDefense;
    for (int index = 0; index < int(EquipmentSlot::Count); ++index) {
        auto item = usable(EquipmentSlot(index));
        if (!item)
            continue;
        const auto &definition = *inventory.catalog().find(item->definition);
        if (definition.family == ItemFamily::Armor)
            result.defense += item->defense;
        if (definition.equipment.isType("shld")) {
            auto block = definition.base.block;
            if (!block)
                throw std::runtime_error("Unverified shield block: " + definition.code);
            result.blockChance = std::clamp(int((int64_t(*block) + actor.blockFactor) *
                                               (actor.dexterity - 15) / (2 * std::max(1, actor.level))), 0, 75);
        }
    }
    for (auto item : {right, left}) {
        if (!item)
            continue;
        const auto &definition = *inventory.catalog().find(item->definition);
        if (!definition.equipment.isType("weap"))
            continue;
        bool twoHands = definition.equipment.isType("miss") || (definition.equipment.twoHanded &&
                        (!definition.equipment.oneOrTwoHanded || actor.characterClass != "bar" ||
                         !(item == right ? left : right)));
        auto minimum = twoHands ? definition.base.twoHandMin : definition.base.minDamage;
        auto maximum = twoHands ? definition.base.twoHandMax : definition.base.maxDamage;
        if (!minimum || !maximum || *minimum < 0 || *maximum < *minimum)
            throw std::runtime_error("Unverified weapon damage: " + definition.code);
        int64_t bonus = int64_t(definition.base.strengthBonus.value_or(0)) * actor.strength / 100 +
                        int64_t(definition.base.dexterityBonus.value_or(0)) * actor.dexterity / 100;
        int64_t low = std::max(1, *minimum) * int64_t(256);
        int64_t high = std::max(*maximum, std::max(1, *minimum) + 1) * int64_t(256);
        low += low * std::max<int64_t>(bonus, -90) / 100;
        high += high * std::max<int64_t>(bonus, -90) / 100;
        if (low < 0 || high < low || high > std::numeric_limits<int>::max())
            throw std::runtime_error("Equipment damage exceeds supported range");
        auto &weapon = result.weapons[count++];
        weapon = {item->id, int(low), int(high), definition.equipment.isType("miss")};
        weapon.leftHand = item == left;
        weapon.throwable = definition.equipment.isType("thro");
        if (weapon.ranged || weapon.throwable) {
            if (!definition.base.projectile)
                throw std::runtime_error("Missing original weapon projectile: " + definition.code);
            weapon.missileId = definition.base.projectile->id;
            weapon.missileSpeed = definition.base.projectile->speed;
            weapon.missileLifetime = definition.base.projectile->lifetime;
        }
        if (weapon.throwable) {
            auto tmin = definition.base.throwMin;
            auto tmax = definition.base.throwMax;
            if (!tmin || !tmax || *tmin < 0 || *tmax < *tmin)
                throw std::runtime_error("Unverified original throw damage: " + definition.code);
            const int64_t scale = std::max<int64_t>(10, 100 + bonus);
            const int64_t throwLow = int64_t(*tmin) * 256 * scale / 100;
            const int64_t throwHigh = int64_t(*tmax) * 256 * scale / 100;
            if (throwLow < 0 || throwHigh < throwLow || throwHigh > std::numeric_limits<int>::max())
                throw std::runtime_error("Equipment throw damage exceeds supported range");
            weapon.throwMinimum = int(throwLow);
            weapon.throwMaximum = int(throwHigh);
        }
    }
    if (count)
        result.weaponCount = count;
    else {
        result.weapons[0].minimum = int(256 + int64_t(256) * actor.strength / 100);
        result.weapons[0].maximum = int(512 + int64_t(512) * actor.strength / 100);
    }
    return result;
}
} // namespace d2x
