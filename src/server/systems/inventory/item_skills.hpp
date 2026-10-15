#pragma once
#include "server/player_state.hpp"
#include <algorithm>
namespace d2x::server::inventory {
inline std::map<int,int> itemSkills(const PersistentCharacter &state,const CharacterRules &rules) {
    std::map<int,int> result;
    for(const auto &[code,rule]:rules.itemSkills) { (void)code; result.try_emplace(rule.skill,0); }
    for(const auto &[id,item]:state.inventory.items) {
        (void)id; const auto *at=std::get_if<ContainerLocation>(&item.location); if(!at || at->container!=state.containers.backpack) continue;
        const auto rule=rules.itemSkills.find(item.definition); if(rule==rules.itemSkills.end()) continue;
        auto &quantity=result[rule->second.skill]; quantity=std::min(255,quantity+int(rule->second.book?item.charges:item.quantity));
    }
    return result;
}
inline std::map<int,int> itemSkills(const PlayerState &p) {
    return p.rules.character?itemSkills(p.persistent,*p.rules.character):std::map<int,int>{};
}
}
