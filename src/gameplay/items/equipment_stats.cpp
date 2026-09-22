#include "equipment_stats.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace d2x {
EquipmentStats deriveEquipmentStats(const InventoryService &inventory, const PlayerContainers &containers,
                                    const EquipmentActor &actor) {
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
    result.defense = actor.dexterity / 4;
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
        bool twoHands = !definition.equipment.isType("miss") && definition.equipment.twoHanded &&
                        (!definition.equipment.oneOrTwoHanded || actor.characterClass != "bar" ||
                         !(item == right ? left : right));
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
        result.weapons[count++] = {item->id, int(low), int(high), definition.equipment.isType("miss")};
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