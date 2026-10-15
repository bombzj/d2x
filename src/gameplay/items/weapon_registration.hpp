#pragma once
#include "core/id.hpp"
#include <array>
#include <optional>
#include <cstddef>
#include <span>
#include <vector>
namespace d2x {
struct WeaponRegistrationHand {
    EntityId item;
    bool weapon{},missilePotion{},usable{};
};
using WeaponRegistrationHands=std::array<WeaponRegistrationHand,2>;
// Native dwLeftItemGUID denotes the registered weapon, irrespective of side.
std::optional<size_t> registeredWeaponHand(EntityId,const WeaponRegistrationHands &);
EntityId weaponRegistrationOnInsert(EntityId,const WeaponRegistrationHands &,size_t hand);
EntityId weaponRegistrationOnRemoval(EntityId,const WeaponRegistrationHands &,size_t hand);
struct InventoryState;
struct ItemChange;
class ItemCatalog;
// Replays body moves into the next candidate. Returned insertions have already
// run the native registration callback before delayed attribute source posting.
std::vector<EntityId> prepareWeaponRegistration(const InventoryState &,InventoryState &,
    const ItemCatalog &,EntityId equipment,unsigned previousSet,unsigned nextSet,std::span<const ItemChange>);
}
