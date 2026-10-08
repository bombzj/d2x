#pragma once
#include "server/player_state.hpp"
#include <algorithm>
namespace d2x::server::inventory {
inline std::map<int,int> itemSkills(const PlayerState &p) {
    std::map<int,int> result; if(!p.rules.character) return result;
    for(const auto &[code,rule]:p.rules.character->itemSkills) { (void)code; result.try_emplace(rule.skill,0); }
    for(const auto &[id,item]:p.persistent.inventory.items) {
        (void)id; const auto *at=std::get_if<ContainerLocation>(&item.location); if(!at || at->container!=p.persistent.containers.backpack) continue;
        const auto rule=p.rules.character->itemSkills.find(item.definition); if(rule==p.rules.character->itemSkills.end()) continue;
        auto &quantity=result[rule->second.skill]; quantity=std::min(255,quantity+int(rule->second.book?item.charges:item.quantity));
    }
    return result;
}
}
