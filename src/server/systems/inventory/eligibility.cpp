#include "eligibility.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x::server::inventory {
void synchronizeEquipment(PersistentCharacter &state, const attributes::Totals &totals,
                          const EquipmentRules &rules, std::vector<ItemChange> &changes) {
    for (auto &[id, item] : state.inventory.items) {
        const auto *location = std::get_if<ContainerLocation>(&item.location);
        if (!location || (location->container != state.containers.beltEquipment && location->container != state.containers.equipment)) continue;
        const auto slot = location->container == state.containers.beltEquipment ? EquipmentSlot::Belt : EquipmentSlot(location->cell.x);
        if (!weaponSlotActive(slot, state.player.weaponSet)) continue;
        uint32_t flags = item.nativeFlags;
        flags = totals.activeEquipment.contains(id) ? flags & ~0x4000u : flags | 0x4000u;
        if (rules.at(id, state.player.level).maximumDurability && !item.durability) flags |= 0x100u;
        else flags &= ~0x100u;
        if (flags == item.nativeFlags) continue;
        const auto existing = std::find_if(changes.begin(), changes.end(), [&](const auto &change) { return change.item == id; });
        if (existing == changes.end()) {
            if (item.revision == UINT64_MAX) throw std::runtime_error("Equipment revision exhausted");
            ++item.revision;
            changes.push_back({id, item.revision, ItemChangeKind::PropertiesChanged, item.location, item.location, item.quantity});
        }
        item.nativeFlags = flags;
    }
}
}
