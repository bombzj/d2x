#include "equipment_stats.hpp"
#include "definitions.hpp"
#include "state.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace d2x {
EquipmentStats deriveEquipmentStats(const EquipmentLoadout &loadout,
                                    const EquipmentActor &actor, int bonusDefense,
                                    const CombatModifiers &combat, int baseAttackRating) {
    EquipmentStats result;
    result.level = std::max(1, actor.level);
    int count = 0;
    auto usable = [&](EquipmentSlot slot) { return loadout.usable(slot, actor); };
    auto right = usable(weaponHandSlot(false, actor.weaponSet));
    auto left = usable(weaponHandSlot(true, actor.weaponSet));
    result.defense = actor.dexterity / 4 + bonusDefense;
    for (int index = 0; index < int(EquipmentSlot::Count); ++index) {
        if (!weaponSlotActive(EquipmentSlot(index), actor.weaponSet)) continue;
        auto item = usable(EquipmentSlot(index));
        if (!item)
            continue;
        const auto &definition = *loadout.find(item->id).definition;
        result.appearanceDefinitions[size_t(index)] = definition.code;
        if (definition.family == ItemFamily::Armor) {
            int64_t armor = item->defense;
            if (auto found = combat.armorPercent.find(item->id); found != combat.armorPercent.end()) {
                if (!definition.base.maxDefense)
                    throw std::runtime_error("Unverified enhanced armor defense: " + definition.code);
                armor = int64_t(item->defense) *
                        std::max<int64_t>(0, 100 + found->second) / 100;
            }
            if (armor > std::numeric_limits<int>::max() - int64_t(result.defense))
                throw std::runtime_error("Equipment defense exceeds supported range");
            result.defense += int(armor);
        }
        if (definition.equipment.isType("shld")) {
            result.shield = item->id;
            result.smiteMinimum = definition.base.minDamage.value_or(0);
            result.smiteMaximum = definition.base.maxDamage.value_or(0);
            auto block = definition.base.block;
            if (!block)
                throw std::runtime_error("Unverified shield block: " + definition.code);
            result.blockChance = std::clamp(int((int64_t(*block) + actor.blockFactor + combat.blockBonus) *
                                               (actor.dexterity - 15) / (2 * std::max(1, actor.level))), 0, 75);
        }
    }
    result.defense = int(std::clamp<int64_t>(int64_t(result.defense) *
        std::max(0, 100 + combat.defensePercent + (result.shield ? combat.shieldDefensePercent : 0)) / 100, 0, std::numeric_limits<int>::max()));
    for (auto item : {right, left}) {
        if (!item)
            continue;
        const auto &definition = *loadout.find(item->id).definition;
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
        if (item->nativeFlags & 0x400000u) { baseLow = baseLow * 3 / 2; baseHigh = baseHigh * 3 / 2; }
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
        const auto rawMinimum = low, rawMaximum = high;
        low += low * (std::max<int64_t>(bonus, -90) + combat.minimumDamagePercent) / 100;
        high += high * (std::max<int64_t>(bonus, -90) + combat.maximumDamagePercent) / 100;
        low = std::max<int64_t>(0, low);
        high = std::max(high, low);
        if (low < 0 || high > std::numeric_limits<int>::max())
            throw std::runtime_error("Equipment damage exceeds supported range");
        auto &weapon = result.weapons[count++];
        weapon = {item->id, int(low), int(high), definition.equipment.isType("miss")};
        weapon.meleeBaseMinimum = int(rawMinimum);
        weapon.meleeBaseMaximum = int(rawMaximum);
        weapon.damagePercent = int(bonus);
        weapon.minimumDamagePercent = combat.minimumDamagePercent;
        weapon.maximumDamagePercent = combat.maximumDamagePercent;
        weapon.baseAttackRating = baseAttackRating + own.attackRating;
        weapon.attackRatingPercent = combat.attackRatingPercent + own.attackRatingPercent;
        weapon.target = combat.target;
        mergeAttackTargetModifiers(weapon.target, own.target);
        weapon.blunt = definition.equipment.isType("blun");
        weapon.leftHand = item == left;
        weapon.throwable = definition.equipment.throwable;
        weapon.potion = definition.equipment.isType("tpot");
        weapon.rangeAdder = definition.base.rangeAdder;
        weapon.baseSpeed = definition.base.speed.value_or(0);
        weapon.weaponClass = definition.base.weaponClass;
        weapon.hitClass = definition.base.hitClass;
        weapon.types = definition.equipment.types;
        weapon.attackRating = int(std::clamp<int64_t>((int64_t(baseAttackRating) + own.attackRating) *
            std::max<int64_t>(0, 100LL + combat.attackRatingPercent + own.attackRatingPercent) / 100,
            0, std::numeric_limits<int>::max()));
        weapon.fasterAttack = combat.fasterAttack + own.fasterAttack;
        result.animationClass = twoHands ? definition.equipment.twoHandWeaponClass : weapon.weaponClass;
        if (weapon.ranged || weapon.throwable) {
            if (!definition.base.projectile)
                throw std::runtime_error("Missing original weapon projectile: " + definition.code);
            weapon.projectile = definition.base.projectile;
            weapon.projectileDamagePercent = int(std::max<int64_t>(-90, bonus +
                std::max(combat.minimumDamagePercent, combat.maximumDamagePercent)));
        }
        if (weapon.ranged) {
            // MISSILE_CalculateDamageData transfers min/max weapon stats, not item_normaldamage.
            weapon.projectileMinimum = int(std::max<int64_t>(0, baseLow - own.normalDamage + combat.minimumDamage) * 256);
            weapon.projectileMaximum = int(std::max<int64_t>(0, baseHigh - own.normalDamage + combat.maximumDamage) * 256);
            weapon.minimum = int(int64_t(weapon.projectileMinimum) * (100 + weapon.projectileDamagePercent) / 100);
            weapon.maximum = int(int64_t(weapon.projectileMaximum) * (100 + weapon.projectileDamagePercent) / 100);
        }
        if (weapon.throwable && !weapon.potion) {
            auto tmin = definition.base.throwMin;
            auto tmax = definition.base.throwMax;
            if (!tmin || !tmax || *tmin < 0 || *tmax < *tmin)
                throw std::runtime_error("Unverified original throw damage: " + definition.code);
            const auto rawLow = item->quality == ItemQuality::Inferior ? std::max(2, *tmin * 75 / 100) : *tmin;
            const auto rawHigh = item->quality == ItemQuality::Inferior ? std::max(1, *tmax * 75 / 100) : *tmax;
            const auto localLow = int64_t(rawLow) * (100 + own.enhancedMinimum) / 100 + own.minimum;
            const auto localHigh = int64_t(rawHigh) * (100 + own.enhancedMaximum) / 100 + own.maximum;
            const auto rawMinimum = std::max<int64_t>(0, localLow + combat.minimumDamage) * 256;
            const auto rawMaximum = std::max<int64_t>(rawMinimum, (localHigh + combat.maximumDamage) * 256);
            const int64_t throwLow = rawMinimum * (100 + weapon.projectileDamagePercent) / 100;
            const int64_t throwHigh = rawMaximum * (100 + weapon.projectileDamagePercent) / 100;
            if (throwLow < 0 || throwHigh > std::numeric_limits<int>::max())
                throw std::runtime_error("Equipment throw damage exceeds supported range");
            weapon.throwMinimum = int(throwLow);
            weapon.throwMaximum = int(throwHigh);
            weapon.projectileMinimum = int(rawMinimum);
            weapon.projectileMaximum = int(rawMaximum);
        }
    }
    if (count)
        result.weaponCount = count;
    else {
        const auto bonus = std::max<int64_t>(-90, actor.strength + combat.damagePercent);
        const int64_t low = std::max<int64_t>(1, std::max(1, combat.minimumDamage) + combat.normalDamage) * 256;
        const int64_t high = std::max<int64_t>(low + 256,
            int64_t(std::max(2, combat.maximumDamage) + combat.normalDamage) * 256);
        auto &fists = result.weapons[0];
        fists.meleeBaseMinimum = int(low);
        fists.meleeBaseMaximum = int(high);
        fists.damagePercent = actor.strength + combat.damagePercent;
        fists.minimumDamagePercent = combat.minimumDamagePercent;
        fists.maximumDamagePercent = combat.maximumDamagePercent;
        fists.baseAttackRating = baseAttackRating;
        fists.attackRatingPercent = combat.attackRatingPercent;
        fists.target = combat.target;
        fists.minimum = int(std::clamp<int64_t>(low + low * (bonus + combat.minimumDamagePercent) / 100,
                                              0, std::numeric_limits<int>::max()));
        fists.maximum = int(std::clamp<int64_t>(high + high * (bonus + combat.maximumDamagePercent) / 100,
                                              fists.minimum, std::numeric_limits<int>::max()));
        fists.attackRating = int(std::clamp<int64_t>(int64_t(baseAttackRating) *
            std::max<int64_t>(0, 100LL + combat.attackRatingPercent) / 100, 0, std::numeric_limits<int>::max()));
        fists.fasterAttack = combat.fasterAttack;
    }
    if (count == 2) {
        const auto &primary = result.weapons[0].weaponClass;
        const auto &secondary = result.weapons[1].weaponClass;
        result.animationClass = primary == "ht1" && secondary == "ht1" ? "ht2" :
            primary == "1ht" ? (secondary == "1ht" ? "1jt" : "1st") :
                               (secondary == "1ht" ? "1js" : "1ss");
        const int speed = (result.weapons[0].baseSpeed + result.weapons[1].baseSpeed) / 2;
        for (auto &weapon : result.weapons) weapon.baseSpeed = speed;
    }
    return result;
}
} // namespace d2x
