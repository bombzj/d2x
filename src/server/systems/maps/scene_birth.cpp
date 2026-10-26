#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/objects/system.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include <algorithm>

namespace d2x::server::maps {
DomainResult<> System::initializeSceneRoom(RegionId region,TickContext tick) {
    const auto *area=ports_.areas.find(region);
    if(!state_.instance || !area || area->definition.pd2Map!=state_.instance) return {DomainStatus::Applied,std::monostate{}};
    const int controller=state_.instance->controller;
    if(!state_.sceneBirths.contains(region)) {
        State::SceneBirth birth;
        if(controller==44 && int(region)==168 && state_.eventProgress==0) {
            state_.eventProgress=1;
            birth.units={{592,true,{0x4e50,0x36fd},0,0},{589,true,{0x4e45,0x36fd},0,1},
                {590,true,{0x4e50,0x36f3},0,2},{591,true,{0x4e5a,0x36fd},0,3}};
        } else if(controller==43 && int(region)==161 && state_.phase==0) {
            state_.phase=1;
            birth.units={{585,true,{0x4862,0x363d}},{587,true,{0x476b,0x35d3},1},
                {934,false,{0x476c,0x35bf},1,0}};
        } else if(controller==43 && int(region)==162 && state_.phase==2) {
            state_.phase=3;
            birth.units={{585,true,{0x488a,0x3a25}},{587,true,{0x47d7,0x3961},1},
                {933,false,{0x47d3,0x394b},1,0}};
        } else if(controller==43 && int(region)==163 && state_.phase==4) {
            state_.phase=5;
            constexpr std::array<Vec,3> doors{{{0x4fb1,0x1590},{0x4ff0,0x151c},{0x5096,0x1549}}};
            birth.units={{585,true,doors[pd2CrtRandom(ports_.random)%3]},{587,true,{0x4e98,0x1419},1},
                {936,false,{0x4e97,0x1403},1,1},{935,false,{0x4e9d,0x1403},1,0}};
            birth.units[0].fallback={{0x4fae,0x158d},{0x4fed,0x1519},{0x5093,0x1546}};
        } else if(controller==45 && int(region)==188) {
            const auto marker=std::find_if(area->definition.nativePresetMonsters.begin(),area->definition.nativePresetMonsters.end(),
                [](const auto &entry){return entry.pd2SceneMarker==738;});
            if(marker==area->definition.nativePresetMonsters.end() || !ports_.areas.loaded(region,marker->position))
                return {DomainStatus::Applied,std::monostate{}};
            const auto point=marker->position+area->definition.origin;
            birth.units={{1112,false,point,1,0},{1113,false,point,1,1}};
        } else return {DomainStatus::Applied,std::monostate{}};
        state_.sceneBirths.emplace(region,std::move(birth));
    }
    auto &birth=state_.sceneBirths.at(region);
    while(birth.next<birth.units.size()) {
        auto &source=birth.units[birth.next];
        const Vec global=source.attempt?source.fallback.at(source.attempt-1):source.global;
        const auto point=global-area->definition.origin;
        if(area->definition.activation.room(point)) {
            ports_.world.activate(region,point);
            if(!ports_.areas.loaded(region,point)) return {DomainStatus::PendingContent,{}};
            const auto created=source.object?ports_.objects.admit({source.definition,region,point,{},{},source.mode,tick.tick}):
                ports_.monsters.admitPd2SceneMonster(region,point,source.definition,
                    "map-scene."+std::to_string(int(region))+"."+std::to_string(birth.next),tick.tick,source.binding);
            if(isDeferred(created.status) || created.status==DomainStatus::NotImplemented) return {created.status,{}};
            if(created) source.id=*created.value;
        }
        if(!source.id && source.attempt<source.fallback.size()) {++source.attempt;continue;}
        if(source.id && source.binding>=0) bind(source.binding,source.id,region,point,source.object);
        ++birth.next;
    }
    if(controller==43 && !birth.linked && birth.units.size()>=2) {
        if(birth.units[0].id && birth.units[1].id) {
            const auto linked=ports_.objects.linkNativePortals(birth.units[0].id,birth.units[1].id,false,tick.tick);
            if(isDeferred(linked.status)) return linked;
        }
        birth.linked=true;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
}
