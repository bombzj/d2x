#include "equipment_stats.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace d2x {
EquipmentStats deriveEquipmentStats(const InventoryService &inventory, const PlayerContainers &containers,
                                    const EquipmentActor &actor, int bonusDefense,
                                    const CombatModifiers &combat) {
    EquipmentStats result;
    result.level = std::max(1, actor.level);
    int count = 0;
    auto usable = [&](EquipmentSlot slot) -> const ItemInstance * {
        const auto *item = inventory.item(inventory.equipped(containers, slot));
        if (!item)
            return nullptr;
        const auto &definition = *inventory.catalog().find(item->definition);
        if ((definition.maxDurability && item->durability == 0) || !item->quantity ||
            inventory.equipmentRequirements(item->handle(), actor) != InventoryError::None)
            return nullptr;
        return item;
    };
    auto right = usable(weaponHandSlot(false, actor.weaponSet));
    auto left = usable(weaponHandSlot(true, actor.weaponSet));
    result.defense = actor.dexterity / 4 + bonusDefense;
    for (int index = 0; index < int(EquipmentSlot::Count); ++index) {
        if (!weaponSlotActive(EquipmentSlot(index), actor.weaponSet)) continue;
        auto item = usable(EquipmentSlot(index));
        if (!item)
            continue;
        const auto &definition = *inventory.catalog().find(item->definition);
        if (definition.family == ItemFamily::Armor) {
            int64_t armor = item->defense;
            if (auto found = combat.armorPercent.find(item->id); found != combat.armorPercent.end()) {
                if (!definition.base.maxDefense)
                    throw std::runtime_error("Unverified enhanced armor defense: " + definition.code);
                armor = (int64_t(*definition.base.maxDefense) + 1) *
                        std::max<int64_t>(0, 100 + found->second) / 100;
            }
            if (armor > std::numeric_limits<int>::max() - int64_t(result.defense))
                throw std::runtime_error("Equipment defense exceeds supported range");
            result.defense += int(armor);
        }
        if (definition.equipment.isType("shld")) {
            auto block = definition.base.block;
            if (!block)
                throw std::runtime_error("Unverified shield block: " + definition.code);
            result.blockChance = std::clamp(int((int64_t(*block) + actor.blockFactor + combat.blockBonus) *
                                               (actor.dexterity - 15) / (2 * std::max(1, actor.level))), 0, 75);
        }
    }
    result.defense = int(std::clamp<int64_t>(int64_t(result.defense) *
        std::max(0, 100 + combat.defensePercent) / 100, 0, std::numeric_limits<int>::max()));
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
                        int64_t(definition.base.dexterityBonus.value_or(0)) * actor.dexterity / 100 +
                        combat.damagePercent;
        WeaponModifiers own;
        if (auto found = combat.weapons.find(item->id); found != combat.weapons.end()) own = found->second;
        int64_t baseLow = *minimum;
        int64_t baseHigh = *maximum;
        if (item->quality == ItemQuality::Inferior) {
            baseLow = std::max<int64_t>(1, baseLow * 75 / 100);
            baseHigh = std::max<int64_t>(2, baseHigh * 75 / 100);
        }
        baseLow += baseLow * own.enhancedMinimum / 100;
        baseHigh += baseHigh * own.enhancedMaximum / 100;
        baseLow += own.minimum + own.normalDamage;
        baseHigh += own.maximum + own.normalDamage;
        int64_t low = std::max<int64_t>(1, baseLow + combat.normalDamage + combat.minimumDamage) * 256;
        int64_t high = std::max<int64_t>(low / 256 + 1, baseHigh + combat.normalDamage + combat.maximumDamage) * 256;
        low += low * std::max<int64_t>(bonus + combat.minimumDamagePercent, -90) / 100;
        high += high * std::max<int64_t>(bonus + combat.maximumDamagePercent, -90) / 100;
        high = std::max(high, low + 256);
        if (low < 0 || high > std::numeric_limits<int>::max())
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
            const int64_t scaleLow = std::max<int64_t>(10, 100 + bonus + combat.minimumDamagePercent);
            const int64_t scaleHigh = std::max<int64_t>(10, 100 + bonus + combat.maximumDamagePercent);
            const auto rawLow = item->quality == ItemQuality::Inferior ? std::max(2, *tmin * 75 / 100) : *tmin;
            const auto rawHigh = item->quality == ItemQuality::Inferior ? std::max(1, *tmax * 75 / 100) : *tmax;
            const auto localLow = int64_t(rawLow) * (100 + own.enhancedMinimum) / 100 + own.minimum + own.normalDamage;
            const auto localHigh = int64_t(rawHigh) * (100 + own.enhancedMaximum) / 100 + own.maximum + own.normalDamage;
            const int64_t throwLow = std::max<int64_t>(1, localLow + combat.normalDamage +
                                      combat.minimumDamage) * 256 * scaleLow / 100;
            const int64_t throwHigh = std::max<int64_t>(throwLow + 256,
                std::max<int64_t>(2, localHigh + combat.normalDamage + combat.maximumDamage) *
                256 * scaleHigh / 100);
            if (throwLow < 0 || throwHigh > std::numeric_limits<int>::max())
                throw std::runtime_error("Equipment throw damage exceeds supported range");
            weapon.throwMinimum = int(throwLow);
            weapon.throwMaximum = int(throwHigh);
        }
    }
    if (count)
        result.weaponCount = count;
    else {
        const auto scale = std::max<int64_t>(10, 100 + actor.strength + combat.damagePercent);
        result.weapons[0].minimum = int(std::max<int64_t>(1, 1 + combat.normalDamage + combat.minimumDamage) *
                                        256 * scale / 100);
        result.weapons[0].maximum = int(std::max<int64_t>(2, 2 + combat.normalDamage + combat.maximumDamage) *
                                        256 * scale / 100);
    }
    return result;
}
} // namespace d2x
