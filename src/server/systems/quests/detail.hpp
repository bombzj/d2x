#pragma once
#include "server/player_state.hpp"
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
// The reusable cube is not subject to a quest difficulty check.
inline const ItemInstance *actTwoCarried(const PlayerState &p,std::string_view code,int difficulty) {
    for(const auto &[id,item]:p.persistent.inventory.items) {
        (void)id;const auto *at=std::get_if<ContainerLocation>(&item.location);
        if(at && (at->container==p.persistent.containers.backpack || at->container==p.persistent.containers.cube || at->container==p.persistent.containers.stash || at->container==p.persistent.containers.cursor || at->container==p.persistent.containers.equipment) &&
           item.definition==code && (code==p.rules.character->actTwo.cube || item.nativeQuestDifficulty>=unsigned(difficulty))) return &item;
    }
    return nullptr;
}
inline const ItemInstance *equippedQuestWeapon(const PlayerState &p,std::string_view code,int difficulty) {
    for(const auto &[id,item]:p.persistent.inventory.items) {
        (void)id;const auto *at=std::get_if<ContainerLocation>(&item.location);
        if(at && at->container==p.persistent.containers.equipment && item.definition==code && item.nativeQuestDifficulty>=unsigned(difficulty) &&
            (at->cell.x==int(weaponHandSlot(false,p.persistent.player.weaponSet)) || at->cell.x==int(weaponHandSlot(true,p.persistent.player.weaponSet))) && (!p.rules.equipment->at(id,p.persistent.player.level).maximumDurability || item.durability)) return &item;
    }
    return nullptr;
}
}
