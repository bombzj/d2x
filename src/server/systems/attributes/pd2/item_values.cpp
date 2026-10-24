#include "item_values.hpp"
#include "gameplay/character/persistent_character.hpp"
#include "gameplay/items/equipment_rules.hpp"

namespace d2x::server::attributes {
int pd2EquippedEthereals(const PersistentCharacter &owner,const std::set<EntityId> &active) {
    int count{};
    for(const auto id:active) {
        const auto found=owner.inventory.items.find(id);if(found==owner.inventory.items.end()) continue;
        const auto &item=found->second;const auto *at=std::get_if<ContainerLocation>(&item.location);
        if(!at || !(item.nativeFlags&0x400000u)) continue;
        if(at->container==owner.containers.beltEquipment ||
            (at->container==owner.containers.equipment && at->cell.x>=0 && at->cell.x<int(EquipmentSlot::Count) &&
             weaponSlotActive(EquipmentSlot(at->cell.x),owner.player.weaponSet))) ++count;
    }
    return count;
}
}
