#include "core/random.hpp"
#include "inventory.hpp"
#include <algorithm>
#include <array>
#include <limits>

namespace d2x {
int InventoryService::propertyValue(const ItemInstance &item, std::string_view stat) const {
    int value = 0;
    if (itemProperties_)
        for (const auto &property : itemProperties_(item))
            if (property.effect == stat) value += property.value;
    return value;
}
unsigned InventoryService::maximumDurability(const ItemInstance &item) const {
    if (item.nativeProperties) return item.nativeMaxDurability;
    auto base = catalog_.find(item.definition)->maxDurability;
    if (!base) return 0;
    if (item.quality == ItemQuality::Inferior) base = std::max(1u, base / 3);
    return unsigned(std::clamp<int64_t>(int64_t(base) *
        (100 + propertyValue(item, "item_maxdurability_percent")) / 100 +
        propertyValue(item, "maxdurability"), 1, 255));
}
unsigned InventoryService::maximumStack(const ItemInstance &item) const {
    const auto base = catalog_.find(item.definition)->maxStack;
    if (base <= 1 || catalog_.find(item.definition)->equipment.isType("gold")) return base;
    return unsigned(std::clamp<int64_t>(int64_t(base) + propertyValue(item, "item_extra_stack"), 1, 511));
}
bool InventoryService::retainsEmptyStack(const ItemInstance &item) const {
    // D2Game PlrModes::sub_6FC80B90, called after Skills decrements quantity.
    // The item_throwable stat is a separate engine exemption from destruction.
    if (propertyValue(item, "item_throwable")) return true;
    const auto &definition = *catalog_.find(item.definition);
    if (!definition.equipment.isType("weap") ||
        (!definition.equipment.throwable && definition.maxStack <= 1)) return false;
    return item.quality == ItemQuality::Magic || item.quality == ItemQuality::Rare || item.quality == ItemQuality::Crafted ||
           item.quality == ItemQuality::Set || item.quality == ItemQuality::Unique;
}
InventoryResult InventoryService::replenish(float dt) {
    InventoryResult result;
    if (dt <= 0) return result;
    std::erase_if(replenishTimers_, [&](const auto &entry) { return !item(entry.first.first); });
    for (auto &[id, instance] : state_.items) {
        const auto &definition = *catalog_.find(instance.definition);
        for (bool quantity : {false, true}) {
            auto key = std::pair{id, quantity};
            const int rate = propertyValue(instance, quantity ? "item_replenish_quantity" : "item_replenish_durability");
            auto &value = quantity ? instance.quantity : instance.durability;
            const unsigned maximum = quantity ? maximumStack(instance) : maximumDurability(instance);
            // D2MOO ItemMode: a broken item cannot self-repair; empty throwing
            // weapons can recover through quantity regeneration.
            if (rate <= 0 || (quantity ? definition.maxStack <= 1 : !value) || value >= maximum) {
                replenishTimers_.erase(key);
                continue;
            }
            auto &timer = replenishTimers_[key];
            timer.elapsed += dt;
            for (;;) {
                const int frames = timer.repeated ? std::max(125, 2500 / rate + 1) : 2500 / rate + 1;
                const float interval = float(frames) / 25.f;
                if (timer.elapsed < interval || value >= maximum ||
                    instance.revision == std::numeric_limits<uint64_t>::max()) break;
                timer.elapsed -= interval;
                timer.repeated = true;
                ++value;
                ++instance.revision;
                result.changes.push_back({id, instance.revision,
                    quantity ? ItemChangeKind::QuantityChanged : ItemChangeKind::DurabilityChanged,
                    instance.location, instance.location, instance.quantity});
            }
        }
    }
    return result;
}
InventoryResult InventoryService::wearEquipment(const PlayerContainers &containers, EntityId weapon,
                                                bool defending, uint64_t &randomState,
                                                unsigned weaponSet, int chanceOverride, int amount) {
    auto roll = [&](unsigned bound) {
        rollRandom(randomState);
        return bound ? uint32_t(randomState) % bound : 0u;
    };
    EntityId selected = weapon;
    if (defending) {
        const std::array slots{EquipmentSlot::Head, EquipmentSlot::Torso,
                               weaponHandSlot(false, weaponSet), weaponHandSlot(true, weaponSet),
                               EquipmentSlot::Belt, EquipmentSlot::Feet, EquipmentSlot::Gloves};
        constexpr std::array<unsigned, 7> weights{3, 5, 4, 4, 2, 2, 2};
        std::array<EntityId, 7> candidates{};
        unsigned total = 0;
        for (size_t index = 0; index < slots.size(); ++index) {
            auto id = equipped(containers, slots[index]);
            const auto *instance = item(id);
            if (instance && catalog_.find(instance->definition)->equipment.isType("armo")) {
                candidates[index] = id;
                total += weights[index];
            }
        }
        if (!total)
            return {};
        size_t index = roll(unsigned(slots.size()));
        unsigned weight = roll(total);
        for (size_t visited = 0; visited < slots.size(); ++visited, index = (index + 1) % slots.size()) {
            if (!candidates[index])
                continue;
            if (weight < weights[index]) {
                selected = candidates[index];
                break;
            }
            weight -= weights[index];
        }
    } else if (selected != equipped(containers, weaponHandSlot(false, weaponSet)) &&
               selected != equipped(containers, weaponHandSlot(true, weaponSet)))
        return {};
    const auto *source = item(selected);
    if (!source)
        return {};
    const auto &definition = *catalog_.find(source->definition);
    if (chanceOverride >= 0 && definition.maxStack > 1 && definition.maxDurability && source->quantity &&
        !propertyValue(*source, "item_indesctructible")) {
        if (roll(100) < unsigned(std::clamp(chanceOverride, 0, 100))) return consumeEquipped(selected, containers);
        return {};
    }
    if (!definition.maxDurability || !source->durability ||
        propertyValue(*source, "item_indesctructible") || definition.maxStack > 1 ||
        (!definition.equipment.isType("armo") && !definition.equipment.isType("weap")))
        return {};
    if (roll(100) >= (chanceOverride >= 0 ? unsigned(std::clamp(chanceOverride, 0, 100)) : definition.equipment.isType("armo") ? 10u : 4u))
        return {};
    InventoryResult result;
    result.item = source->id;
    if (source->revision == std::numeric_limits<uint64_t>::max()) {
        result.error = InventoryError::RevisionExhausted;
        return result;
    }
    result.changes.push_back({source->id, source->revision + 1, ItemChangeKind::DurabilityChanged,
                              source->location, source->location, source->quantity});
    auto &instance = state_.items.at(source->id);
    instance.durability -= std::min(instance.durability, unsigned(std::max(0, amount)));
    ++instance.revision;
    return result;
}
} // namespace d2x
