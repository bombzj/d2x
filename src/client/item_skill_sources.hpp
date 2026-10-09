#pragma once
#include "contracts/online.hpp"
#include "gameplay/items/skill_sources.hpp"
#include "gameplay/items/equipment_set.hpp"
#include "resources/data_table.hpp"
#include <map>
#include <set>
namespace d2x {
// Decoded original item values only. This is eligibility for input/display;
// charge consumption and final equipment qualification belong to the server.
inline std::vector<ChargedSkill> nativeChargedSkills(const OnlineView &view,const OnlineInventoryView &inventory,const DataTable &sets) {
    std::vector<ChargedSkill> result;
    std::map<std::string,std::set<int32_t>,std::less<>> equippedSets;
    for(const auto &[id,wire]:view.world.items) {
        if(wire.ownerType!=0 || wire.owner!=view.load.playerUnitId || wire.mode!=1 || wire.body<1 || wire.body>10 || (wire.flags&0x4100u)) continue;
        const auto found=inventory.items.find(id);
        if(found!=inventory.items.end() && found->second.decoded && found->second.revision==wire.revision && found->second.identified && found->second.quality==5 && found->second.fileIndex<sets.rows().size()) {
            const auto set=sets.value(found->second.fileIndex,"set");
            if(!set.empty()) equippedSets[std::string(set)].insert(found->second.fileIndex);
        }
    }
    for(const auto &[id,wire]:view.world.items) {
        if(wire.ownerType!=0 || wire.owner!=view.load.playerUnitId || wire.mode!=1 || wire.body<1 || wire.body>10 || (wire.flags&0x4100u)) continue;
        const auto found=inventory.items.find(id);
        if(found==inventory.items.end() || !found->second.decoded || found->second.revision!=wire.revision) continue;
        std::map<uint32_t,int64_t> layers;bool valid=true;
        const auto add=[&](const auto &list){for(const auto &stat:list) if(stat.id==204 && !layers.emplace(stat.parameter,stat.value).second) valid=false;};
        add(found->second.stats);add(found->second.runewordStats);
        const auto &item=found->second;
        if(item.identified && item.quality==5 && item.fileIndex<sets.rows().size()) {
            const auto set=sets.value(item.fileIndex,"set");
            const auto equipped=equippedSets.find(set);
            if(equipped!=equippedSets.end()) {
                std::vector<int32_t> members;
                for(size_t row=0;row<sets.rows().size();++row) if(sets.value(row,"set")==set && !sets.value(row,"item").empty()) members.push_back(int32_t(row));
                const std::vector<int32_t> worn(equipped->second.begin(),equipped->second.end());
                const auto active=activeSetItemLayers(sets.number(item.fileIndex,"add func").value_or(0),item.fileIndex,members,worn);
                for(size_t i=0;i<active.size();++i) if(active[i]) add(item.setStats[i]);
            }
        }
        if(!valid) continue;
        for(const auto &[layer,value]:layers) if(layer<=UINT16_MAX && value>0 && value<=65535 && (layer&63) && (value&255)<=((value>>8)&255))
            result.push_back({{EntityId{id},wire.revision},int(layer>>6),int(layer&63),int(value&255),int((value>>8)&255),int(layer)});
    }
    return result;
}
}
