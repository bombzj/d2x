#include "equipment_modifiers.hpp"
#include "equipment_combat.hpp"
#include "item_properties.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <span>

namespace d2x {
namespace {
void addStats(std::span<const ResolvedItemStat> stats, EntityId item, bool weapon, CharacterModifiers &mods) {
    for (const auto &stat : stats) {
        int *target = nullptr;
        if (stat.effect == "strength") target = &mods.strength;
        else if (stat.effect == "dexterity") target = &mods.dexterity;
        else if (stat.effect == "vitality") target = &mods.vitality;
        else if (stat.effect == "energy") target = &mods.energy;
        else if (stat.effect == "maxhp") target = &mods.maxLife;
        else if (stat.effect == "maxmana") target = &mods.maxMana;
        else if (stat.effect == "maxstamina") target = &mods.maxStamina;
        else if (stat.effect == "skill_staminapercent") target = &mods.staminaPercent;
        else if (stat.effect == "tohit" && !weapon) target = &mods.attackRating;
        else if (stat.effect == "armorclass") target = &mods.defense;
        else if (stat.effect == "fireresist") target = &mods.fireResist;
        else if (stat.effect == "coldresist") target = &mods.coldResist;
        else if (stat.effect == "lightresist") target = &mods.lightningResist;
        else if (stat.effect == "poisonresist") target = &mods.poisonResist;
        else if (stat.effect == "item_lightradius") target = &mods.lightRadius;
        else if (stat.effect == "item_fastermovevelocity") target = &mods.fasterMoveVelocity;
        else if (stat.effect == "velocitypercent") target = &mods.velocityPercent;
        else if (stat.effect == "item_staminadrainpct") target = &mods.staminaDrainPercent;
        else if (stat.effect == "staminarecoverybonus") target = &mods.staminaRecoveryBonus;
        if (target) {
            const int64_t sum = int64_t(*target) + stat.value;
            if (sum < std::numeric_limits<int>::min() || sum > std::numeric_limits<int>::max())
                throw std::runtime_error("Equipped property sum exceeds supported range");
            *target = int(sum);
        }
        applyEquipmentStat(stat, item, weapon, mods.combat);
    }
}
void addItem(const ClassicData &content, const ItemInstance &item, bool weapon,
             int level, CharacterModifiers &mods) {
    addStats(resolveItemStats(content, item, level), item.id, weapon, mods);
}
} // namespace
CharacterModifiers resolveEquipmentModifiers(const ClassicData &content,
                                              const InventoryService &inventory,
                                              const PlayerContainers &containers,
                                              const EquipmentActor &baseActor,
                                              EntityId excludedItem) {
    CharacterModifiers total;
    std::set<EntityId> active;
    std::set<std::string> appliedSetBonuses;
    // Charms are active only in the backpack, never in the cube or stash.
    for (auto id : inventory.contents(containers.backpack)) {
        const auto &item = *inventory.item(id);
        const auto &definition = *inventory.catalog().find(item.definition);
        if (definition.equipment.isType("char") && item.quantity &&
            (!definition.maxDurability || item.durability) &&
            !(item.nativeProperties && (item.nativeFlags & (0x00000100u | 0x00004000u))) &&
            inventory.equipmentRequirements(item.handle(), baseActor) == InventoryError::None)
            addItem(content, item, false, baseActor.level, total);
    }
    for (int pass = 0; pass < int(EquipmentSlot::Count); ++pass) {
        bool changed = false;
        for (int slot = 0; slot < int(EquipmentSlot::Count); ++slot) {
            if (!weaponSlotActive(EquipmentSlot(slot), baseActor.weaponSet)) continue;
            const auto *item = inventory.item(inventory.equipped(containers, EquipmentSlot(slot)));
            if (!item || item->id == excludedItem || active.contains(item->id)) continue;
            const auto *definition = inventory.catalog().find(item->definition);
            if (!definition || !item->identified || !item->quantity || (definition->maxDurability && item->durability == 0)) continue;
            EquipmentActor actor = baseActor;
            auto adjusted = [](int base, int bonus) {
                int64_t value = int64_t(base) + bonus;
                if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
                    throw std::runtime_error("Equipment requirement attribute overflow");
                return int(value);
            };
            actor.strength = adjusted(actor.strength, total.strength);
            actor.dexterity = adjusted(actor.dexterity, total.dexterity);
            if (inventory.equipmentRequirements(item->handle(), actor) != InventoryError::None) continue;
            addItem(content, *item, definition->equipment.isType("weap"), baseActor.level, total);
            if (definition->family == ItemFamily::Armor && definition->base.speed) {
                const int speed = *definition->base.speed;
                if (speed < 0) throw std::runtime_error("Invalid original armor speed penalty");
                const int64_t percent = int64_t(total.velocityPercent) - speed;
                if (percent < std::numeric_limits<int>::min())
                    throw std::runtime_error("Equipped velocity penalty exceeds supported range");
                total.velocityPercent = int(percent);
                if (EquipmentSlot(slot) == EquipmentSlot::Torso)
                    total.torsoSpeed = speed;
            }
            active.insert(item->id);
            changed = true;
        }
        std::map<std::string, std::set<int32_t>, std::less<>> pieces;
        for (auto id : active) {
            const auto &item = *inventory.item(id);
            if (item.quality != ItemQuality::Set) continue;
            for (const auto &record : content.setItems)
                if (int32_t(record.row) == item.specialRow) pieces[record.set].insert(item.specialRow);
        }
        for (auto id : active) {
            const auto &item = *inventory.item(id);
            if (item.quality != ItemQuality::Set) continue;
            for (const auto &record : content.setItems) {
                if (int32_t(record.row) != item.specialRow) continue;
                const int count = int(pieces[record.set].size());
                const int full = int(std::count_if(content.setItems.begin(), content.setItems.end(),
                    [&](const auto &other) { return other.set == record.set; }));
                if (item.nativeProperties && record.setAddFunction == 2) {
                    for (size_t index = 0; index < item.savedSetStats.size(); ++index) {
                        const std::string key = record.set + ":native:" + std::to_string(id.value) + ":" + std::to_string(index);
                        if (count < int(index) + 2 || item.savedSetStats[index].empty() || appliedSetBonuses.contains(key)) continue;
                        auto bonus = item;
                        bonus.savedStats = item.savedSetStats[index];
                        addItem(content, bonus, inventory.catalog().find(item.definition)->equipment.isType("weap"), baseActor.level, total);
                        appliedSetBonuses.insert(key);
                        changed = true;
                    }
                }
                int localIndex = 0, globalIndex = 0;
                for (const auto &bonus : record.setBonuses) {
                    if (item.nativeProperties && bonus.perItem) continue;
                    const std::string key = record.set + (bonus.perItem ? ":item:" + std::to_string(id.value) : ":set") +
                        ":" + std::to_string(bonus.perItem ? localIndex++ : globalIndex++);
                    if (appliedSetBonuses.contains(key) || count < (bonus.pieces ? bonus.pieces : full) ||
                        (bonus.perItem && record.setAddFunction != 2)) continue;
                    const auto &p = bonus.property;
                    if (p.directRoll && p.minimum != p.maximum) continue;
                    addStats(resolvePropertyStats(content, p, p.minimum.value_or(0), baseActor.level),
                        bonus.perItem ? id : EntityId{},
                        bonus.perItem && inventory.catalog().find(item.definition)->equipment.isType("weap"), total);
                    appliedSetBonuses.insert(key);
                    changed = true;
                }
            }
        }
        if (!changed) break;
    }
    return total;
}
} // namespace d2x
