#pragma once
#include "equipment_loadout.hpp"
#include <functional>
#include <set>
namespace d2x {
struct EquipmentActivationCallbacks {
    std::function<bool(EntityId)> posted;
    std::function<EquipmentActor()> actor;
    std::function<void(EquipmentItem)> post,withdraw,refreshSet;
    std::function<void(EntityId,bool)> noEquipChanged{};
    std::function<void(EquipmentItem,bool)> weaponRegistrationChanged{}; // Insert / removal before source propagation.
};
struct EquipmentActivation {
    std::set<EntityId> active,noEquip;
};
// ItemMode::UpdateInventoryItems ordering. Callbacks prepare candidate sources;
// this function never writes inventory, character resources or shared lists.
EquipmentActivation updateEquipmentActivation(const EquipmentLoadout &,unsigned weaponSet,
    const EquipmentActivationCallbacks &,EntityId excluded={});
}
