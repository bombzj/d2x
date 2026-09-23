#include "identification.hpp"
#include <cstdint>
#include <stdexcept>

namespace d2x {
IdentificationPlan planCainIdentification(const InventoryState &inventory,
                                          const PlayerContainers &containers) {
    IdentificationPlan plan;
    for (const auto &[id, item] : inventory.items) {
        if (item.identified)
            continue;
        auto location = std::get_if<ContainerLocation>(&item.location);
        if (!location || (location->container != containers.backpack &&
                          location->container != containers.equipment &&
                          location->container != containers.beltEquipment))
            continue;
        plan.items.push_back(id);
    }
    if (plan.items.size() > UINT32_MAX / cainIdentifyCost)
        throw std::runtime_error("Too many items for Cain identification");
    plan.cost = unsigned(plan.items.size()) * cainIdentifyCost;
    return plan;
}
void applyCainIdentification(InventoryState &inventory, const IdentificationPlan &plan) {
    for (auto id : plan.items) {
        auto &item = inventory.items.at(id);
        item.identified = true;
        ++item.revision;
    }
}
} // namespace d2x
