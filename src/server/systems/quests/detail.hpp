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
}
