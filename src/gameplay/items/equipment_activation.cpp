#include "equipment_activation.hpp"
#include "definitions.hpp"
#include "state.hpp"
#include <array>
#include <stdexcept>
namespace d2x {
EquipmentActivation updateEquipmentActivation(const EquipmentLoadout &loadout,unsigned weaponSet,
    const EquipmentActivationCallbacks &callbacks,EntityId excluded) {
    if(!callbacks.posted || !callbacks.actor || !callbacks.post || !callbacks.withdraw || !callbacks.refreshSet)
        throw std::invalid_argument("Missing native equipment activation callbacks");
    EquipmentActivation result;
    const std::array<EquipmentSlot,10> body{EquipmentSlot::Head,EquipmentSlot::Neck,EquipmentSlot::Torso,
        weaponHandSlot(false,weaponSet),weaponHandSlot(true,weaponSet),EquipmentSlot::RightRing,
        EquipmentSlot::LeftRing,EquipmentSlot::Belt,EquipmentSlot::Feet,EquipmentSlot::Gloves};
    const auto broken=[&](EquipmentItem item) {
        return (item.instance->nativeFlags&0x100u) || (loadout.maximumDurability(*item.instance) && !item.instance->durability);
    };
    const auto qualified=[&](EquipmentItem item) {
        return item.instance->id!=excluded && loadout.requirements(item,callbacks.actor())==InventoryError::None && loadout.quiverReady(item,weaponSet);
    };
    const auto post=[&](EquipmentItem item) {
        result.noEquip.erase(item.instance->id);
        if(callbacks.noEquipChanged) callbacks.noEquipChanged(item.instance->id,false);
        if(callbacks.weaponRegistrationChanged) callbacks.weaponRegistrationChanged(item,true);
        callbacks.post(item);
    };
    for(const auto slot:body) if(const auto item=loadout.equipped[size_t(slot)];item && (item.instance->nativeFlags&0x4000u))
        result.noEquip.insert(item.instance->id);
    // ItemMode's node-page-1 pass requires ITEMS_IsCharmUsable. Body items
    // are enabled by the later body-location pass, not by inventory order.
    for(const auto item:loadout.inventory) if(item && item.definition->equipment.isType("char") &&
        !broken(item) && !(item.instance->nativeFlags&0x4000u) &&
        !callbacks.posted(item.instance->id) && qualified(item)) post(item);
    for(const auto slot:body) {
        const auto item=loadout.equipped[size_t(slot)];if(!item) continue;
        const auto id=item.instance->id;const bool owned=callbacks.posted(id);
        if((!broken(item) || owned) && (!result.noEquip.contains(id) || owned) && !qualified(item)) {
            result.noEquip.insert(id);
            if(callbacks.noEquipChanged) callbacks.noEquipChanged(id,true);
            if(callbacks.weaponRegistrationChanged) callbacks.weaponRegistrationChanged(item,false);
            if(owned) callbacks.withdraw(item);
        }
    }
    bool changed;
    do {
        changed=false;
        for(const auto slot:body) {
            const auto item=loadout.equipped[size_t(slot)];if(!item || broken(item)) continue;
            const auto id=item.instance->id;
            if((result.noEquip.contains(id) || !callbacks.posted(id)) && qualified(item)) {
                post(item);changed=true;
                if(!callbacks.posted(id)) throw std::runtime_error("Native equipment post did not install its eligible source");
            }
        }
    } while(changed);
    for(const auto slot:body) {
        const auto item=loadout.equipped[size_t(slot)];if(!item) continue;
        if(item.instance->quality==ItemQuality::Set && !broken(item) && !result.noEquip.contains(item.instance->id)) callbacks.refreshSet(item);
        if(callbacks.posted(item.instance->id)) result.active.insert(item.instance->id);
    }
    return result;
}
}
