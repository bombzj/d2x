#include "gameplay/items/equipment_contributions.hpp"
#include "gameplay/items/equipment_loadout.hpp"
#include "gameplay/items/definitions.hpp"
#include "gameplay/items/state.hpp"
#include "gameplay/items/equipment_combat.hpp"
#include <algorithm>
#include <limits>
#include <map>
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
void addItem(const EquipmentContributionSource &source, const ItemInstance &item, bool weapon,
             int level, CharacterModifiers &mods) {
    addStats(source.itemStats(item, level), item.id, weapon, mods);
}
} // namespace
CharacterModifiers deriveEquipmentModifiers(const EquipmentLoadout &loadout,
    const EquipmentActor &baseActor, const EquipmentContributionSource &source, EntityId excludedItem) {
    CharacterModifiers total;
    std::set<EntityId> active;
    std::set<std::string> appliedSetBonuses;
    // Charms are active only in the backpack, never in the cube or stash.
    for (auto entry : loadout.backpack) {
        if (!entry) continue;
        const auto &item = *entry.instance;
        const auto &definition = *entry.definition;
        if (definition.equipment.isType("char") && item.quantity &&
            (!definition.maxDurability || item.durability) &&
            !(item.nativeProperties && (item.nativeFlags & (0x00000100u | 0x00004000u))) &&
            loadout.requirements(entry, baseActor) == InventoryError::None)
            addItem(source, item, false, baseActor.level, total);
    }
    for (int pass = 0; pass < int(EquipmentSlot::Count); ++pass) {
        bool changed = false;
        for (int slot = 0; slot < int(EquipmentSlot::Count); ++slot) {
            if (!weaponSlotActive(EquipmentSlot(slot), baseActor.weaponSet)) continue;
            const auto entry = loadout.equipped[size_t(slot)];
            const auto *item = entry.instance;
            if (!entry || item->id == excludedItem || active.contains(item->id)) continue;
            const auto *definition = entry.definition;
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
            if (loadout.requirements(entry, actor) != InventoryError::None) continue;
            addItem(source, *item, definition->equipment.isType("weap"), baseActor.level, total);
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
            const auto &item = *loadout.find(id).instance;
            if (item.quality != ItemQuality::Set) continue;
            for (const auto &record : source.sets)
                if (record.row == item.specialRow) pieces[record.set].insert(item.specialRow);
        }
        for (auto id : active) {
            const auto &item = *loadout.find(id).instance;
            if (item.quality != ItemQuality::Set) continue;
            for (const auto &record : source.sets) {
                if (record.row != item.specialRow) continue;
                const int count = int(pieces[record.set].size());
                const int full = record.fullPieces;
                if (item.nativeProperties && record.addFunction == 2) {
                    for (size_t index = 0; index < item.savedSetStats.size(); ++index) {
                        const std::string key = record.set + ":native:" + std::to_string(id.value) + ":" + std::to_string(index);
                        if (count < int(index) + 2 || item.savedSetStats[index].empty() || appliedSetBonuses.contains(key)) continue;
                        auto bonus = item;
                        bonus.savedStats = item.savedSetStats[index];
                        addItem(source, bonus, loadout.find(item.id).definition->equipment.isType("weap"), baseActor.level, total);
                        appliedSetBonuses.insert(key);
                        changed = true;
                    }
                }
                int localIndex = 0, globalIndex = 0;
                for (const auto &bonus : record.bonuses) {
                    if (item.nativeProperties && bonus.perItem) continue;
                    const std::string key = record.set + (bonus.perItem ? ":item:" + std::to_string(id.value) : ":set") +
                        ":" + std::to_string(bonus.perItem ? localIndex++ : globalIndex++);
                    if (appliedSetBonuses.contains(key) || count < (bonus.pieces ? bonus.pieces : full) ||
                        (bonus.perItem && record.addFunction != 2)) continue;
                    if (!bonus.fixedValue) continue;
                    addStats(source.setStats(record.instruction, bonus.instruction, baseActor.level),
                        bonus.perItem ? id : EntityId{},
                        bonus.perItem && loadout.find(item.id).definition->equipment.isType("weap"), total);
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
