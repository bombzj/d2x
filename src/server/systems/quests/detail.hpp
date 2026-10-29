#pragma once
#include "server/player_state.hpp"
#include "gameplay/items/inventory_order.hpp"
#include "gameplay/items/native_save.hpp"
#include <string_view>
namespace d2x::server::quests::detail {
inline const ItemInstance *carried(const PlayerState &p,std::string_view code,int difficulty) {
    for(const auto &[id,item]:p.persistent.inventory.items) {
        (void)id; const auto *at=std::get_if<ContainerLocation>(&item.location);
        if(at && (at->container==p.persistent.containers.backpack || at->container==p.persistent.containers.cube || at->container==p.persistent.containers.equipment || at->container==p.persistent.containers.cursor) &&
            item.definition==code && item.nativeQuestDifficulty>=unsigned(difficulty)) return &item;
    }
    return nullptr;
}
// ITEMS_FindQuestItem includes stash/cursor and equipped items: original
// ItemMode sets an equipped item's InvPage to NULL (255), not EQUIP (1).
// The inventory branch consults questdiffcheck; the cursor branch does not.
inline const ItemInstance *actTwoCarried(const PlayerState &p,std::string_view code,int difficulty) {
    if(!p.rules.itemSave) return nullptr;
    const auto base=p.rules.itemSave->bases.find(code);if(base==p.rules.itemSave->bases.end()) return nullptr;
    const auto matches=[&](const ItemInstance &item,bool cursor) {
        if(item.definition!=code) return false;
        // The cursor branch checks every quest item; the inventory chain
        // additionally consults questdiffcheck (ITEMS_FindQuestItem).
        return !(cursor?base->second.definition.questTag!=0:base->second.questDifficulty) ||
            item.nativeQuestDifficulty>=unsigned(difficulty);
    };
    for(const auto &[id,item]:p.persistent.inventory.items) {
        (void)id;const auto *at=std::get_if<ContainerLocation>(&item.location);
        if(at && at->container==p.persistent.containers.cursor && matches(item,true)) return &item;
    }
    for(const auto id:inventoryItemOrder(p.persistent.inventory)) {
        const auto &item=p.persistent.inventory.items.at(id);const auto *at=std::get_if<ContainerLocation>(&item.location);
        if(at && (at->container==p.persistent.containers.backpack || at->container==p.persistent.containers.cube ||
            at->container==p.persistent.containers.stash || at->container==p.persistent.containers.equipment) && matches(item,false)) return &item;
    }
    return nullptr;
}
inline const ItemInstance *equippedQuestWeapon(const PlayerState &p,std::string_view code) {
    // INVENTORY_GetLeftHandWeapon follows dwLeftItemGUID, not either hand.
    // Registration already ran the native broken/NOEQUIP callbacks; the getter
    // itself checks only the active body location and the weapon item type.
    const auto body=p.persistent.inventory.containers.find(p.persistent.containers.equipment);
    if(body==p.persistent.inventory.containers.end() || !p.rules.itemSave) return nullptr;
    const auto found=p.persistent.inventory.items.find(body->second.registeredWeapon);
    if(found==p.persistent.inventory.items.end() || found->second.definition!=code) return nullptr;
    const auto &item=found->second;const auto *at=std::get_if<ContainerLocation>(&item.location);
    const auto base=p.rules.itemSave->bases.find(code);
    if(!at || at->container!=body->first || base==p.rules.itemSave->bases.end() ||
        !base->second.definition.equipment.isType("weap")) return nullptr;
    return at->cell.x==int(weaponHandSlot(false,p.persistent.player.weaponSet)) ||
        at->cell.x==int(weaponHandSlot(true,p.persistent.player.weaponSet))?&item:nullptr;
}
}
