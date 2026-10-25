#include "item_values.hpp"
#include "gameplay/character/persistent_character.hpp"
#include "gameplay/items/equipment_rules.hpp"
#include "gameplay/stats/stat_list.hpp"
#include <algorithm>

namespace d2x::server::attributes {
int pd2EquippedMaterialEthereals(const ItemInstance &material,const StatList &root) {
    // SkillNec Do57 equips this manufacturing item on the golem. Its retained
    // GUID and active item source stand for that inventory's single body item.
    if(!(material.nativeFlags&0x400000u)) return 0;
    return int(std::any_of(root.contributions.begin(),root.contributions.end(),[&](const auto &part) {
        return part && part->ownerKind==StatOwnerKind::Item && part->source.owner==material.id;
    }));
}
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
