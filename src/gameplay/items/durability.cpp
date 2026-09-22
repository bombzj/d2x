#include "inventory.hpp"
#include <array>
#include <limits>

namespace d2x {
InventoryResult InventoryService::wearEquipment(const PlayerContainers &containers, EntityId weapon,
                                                bool defending, uint64_t &randomState) {
    auto roll = [&](unsigned bound) {
        randomState = uint64_t(uint32_t(randomState)) * 0x6ac690c5ULL + (randomState >> 32);
        return bound ? uint32_t(randomState) % bound : 0u;
    };
    EntityId selected = weapon;
    if (defending) {
        constexpr std::array slots{EquipmentSlot::Head, EquipmentSlot::Torso, EquipmentSlot::RightHand,
                                   EquipmentSlot::LeftHand, EquipmentSlot::Belt, EquipmentSlot::Feet,
                                   EquipmentSlot::Gloves};
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
    } else if (selected != equipped(containers, EquipmentSlot::RightHand) &&
               selected != equipped(containers, EquipmentSlot::LeftHand))
        return {};
    const auto *source = item(selected);
    if (!source)
        return {};
    const auto &definition = *catalog_.find(source->definition);
    if (!definition.maxDurability || !source->durability || definition.maxStack > 1 ||
        (!definition.equipment.isType("armo") && !definition.equipment.isType("weap")))
        return {};
    if (roll(100) >= (definition.equipment.isType("armo") ? 10u : 4u))
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
    --instance.durability;
    ++instance.revision;
    return result;
}
} // namespace d2x