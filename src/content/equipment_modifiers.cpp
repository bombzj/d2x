#include "equipment_modifiers.hpp"
#include "equipment_combat.hpp"
#include <algorithm>
#include <limits>
#include <span>
#include <set>
#include <stdexcept>

namespace d2x {
namespace {
void addProperty(const ClassicData &content, const PropertyRange &property, int value,
                 CharacterModifiers &mods) {
    if (!property.directRoll) return;
    auto definition = std::find_if(content.properties.begin(), content.properties.end(),
                                   [&](const auto &candidate) { return candidate.code == property.code; });
    if (definition == content.properties.end()) return;
    for (const auto &operation : definition->operations) {
        if (operation.function != 1 && operation.function != 2 && operation.function != 3 &&
            operation.function != 8) continue;
        const auto &stat = operation.stat;
        if (std::none_of(content.itemStats.begin(), content.itemStats.end(),
                         [&](const auto &entry) { return entry.name == stat && entry.id.has_value(); }))
            continue;
        int *target = nullptr;
        if (stat == "strength") target = &mods.strength;
        else if (stat == "dexterity") target = &mods.dexterity;
        else if (stat == "vitality") target = &mods.vitality;
        else if (stat == "energy") target = &mods.energy;
        else if (stat == "maxhp") target = &mods.maxLife;
        else if (stat == "maxmana") target = &mods.maxMana;
        else if (stat == "maxstamina") target = &mods.maxStamina;
        else if (stat == "tohit") target = &mods.attackRating;
        else if (stat == "armorclass") target = &mods.defense;
        else if (stat == "fireresist") target = &mods.fireResist;
        else if (stat == "coldresist") target = &mods.coldResist;
        else if (stat == "lightresist") target = &mods.lightningResist;
        else if (stat == "poisonresist") target = &mods.poisonResist;
        else if (stat == "item_lightradius") target = &mods.lightRadius;
        if (target) {
            int64_t sum = int64_t(*target) + value;
            if (sum < std::numeric_limits<int>::min() || sum > std::numeric_limits<int>::max())
                throw std::runtime_error("Equipped property sum exceeds supported range");
            *target = int(sum);
        }
    }
}
void addProperties(const ClassicData &content, std::span<const PropertyRange> properties,
                   std::span<const int32_t> rolls, EntityId item, bool weapon,
                   CharacterModifiers &mods) {
    if (properties.size() != rolls.size())
        throw std::runtime_error("Equipment property roll count mismatch");
    for (size_t index = 0; index < properties.size(); ++index) {
        addProperty(content, properties[index], rolls[index], mods);
        applyEquipmentCombatProperty(content, properties[index], rolls[index], item, weapon, mods.combat);
    }
}
void addItem(const ClassicData &content, const ItemDefinition &definition,
             const ItemInstance &item, bool weapon, CharacterModifiers &mods) {
    // The base item field is an emitted radius; use the strongest equipped source.
    mods.baseItemLightRadius = std::max(mods.baseItemLightRadius,
                                        std::max(0, definition.base.lightRadius.value_or(0)));
    if (item.specialRow >= 0) {
        const auto &records = item.quality == ItemQuality::Unique ? content.uniqueItems : content.setItems;
        auto found = std::find_if(records.begin(), records.end(),
                                  [&](const auto &record) { return int32_t(record.row) == item.specialRow; });
        if (found == records.end()) throw std::runtime_error("Unknown equipped special item row");
        addProperties(content, found->properties, item.propertyRolls, item.id, weapon, mods);
    }
    for (const auto &affix : item.affixes) {
        const auto &records = affix.prefix ? content.magicPrefixes : content.magicSuffixes;
        auto found = std::find_if(records.begin(), records.end(),
                                  [&](const auto &record) { return int32_t(record.row) == affix.row; });
        if (found == records.end()) throw std::runtime_error("Unknown equipped affix row");
        addProperties(content, found->properties, affix.propertyRolls, item.id, weapon, mods);
    }
    if (item.quality == ItemQuality::Superior && item.gradeRow >= 0) {
        auto found = std::find_if(content.superiorGrades.begin(), content.superiorGrades.end(),
                                  [&](const auto &record) { return int32_t(record.row) == item.gradeRow; });
        if (found == content.superiorGrades.end()) throw std::runtime_error("Unknown equipped grade row");
        addProperties(content, found->properties, item.propertyRolls, item.id, weapon, mods);
    }
}
} // namespace
CharacterModifiers resolveEquipmentModifiers(const ClassicData &content,
                                              const InventoryService &inventory,
                                              const PlayerContainers &containers,
                                              const EquipmentActor &baseActor) {
    CharacterModifiers total;
    std::set<EntityId> active;
    for (int pass = 0; pass < int(EquipmentSlot::Count); ++pass) {
        bool changed = false;
        for (int slot = 0; slot < int(EquipmentSlot::Count); ++slot) {
            const auto *item = inventory.item(inventory.equipped(containers, EquipmentSlot(slot)));
            if (!item || active.contains(item->id)) continue;
            const auto *definition = inventory.catalog().find(item->definition);
            if (!definition || (definition->maxDurability && item->durability == 0)) continue;
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
            addItem(content, *definition, *item, definition->equipment.isType("weap"), total);
            active.insert(item->id);
            changed = true;
        }
        if (!changed) break;
    }
    return total;
}
} // namespace d2x
