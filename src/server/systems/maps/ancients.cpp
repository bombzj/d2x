#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/world/system.hpp"

namespace d2x::server::maps {
DomainResult<> System::operateAncients(const ActorContext &actor,EntityId id) {
    synchronize();
    const auto found=ports_.objects.read().objects.find(id);
    if(!state_.instance || state_.instance->controller!=44 || found==ports_.objects.read().objects.end() ||
       found->second.area!=actor.area) return {DomainStatus::Unavailable,{}};
    if(state_.eventProgress&0x10) return {DomainStatus::Applied,std::monostate{}};
    const int type=found->second.definition;
    if(type>=589 && type<=591) {
        const auto bit=uint32_t(2u<<(type-589));const auto before=state_.eventProgress;
        const auto mode=ports_.objects.setNativeMode(id,(before&bit)?0:1,actor.tick);
        if(!mode) return mode;
        state_.eventProgress=before^bit;return {DomainStatus::Applied,std::monostate{}};
    }
    if(type!=592 || !(state_.eventProgress&0xe)) return {DomainStatus::Applied,std::monostate{}};
    auto &operation=state_.eventOperations[id];
    if(operation.active) return {DomainStatus::Applied,std::monostate{}};
    operation={};operation.stage=state_.eventProgress;operation.active=true;
    const auto advanced=advanceAncientsOperation(id,{actor.tick});
    return isDeferred(advanced.status)?DomainResult<>{DomainStatus::Applied,std::monostate{}}:advanced;
}
DomainResult<> System::advanceAncientsOperation(EntityId id,TickContext tick) {
    auto &operation=state_.eventOperations.at(id);
    if(!operation.phase) {
        const auto mode=ports_.objects.setNativeMode(id,1,tick.tick,true);if(!mode) return mode;
        state_.eventProgress|=0x10;operation.phase=1;
    }
    while(operation.attempt<3) {
        const int index=operation.attempt;
        const auto &binding=state_.bindings[size_t(index+1)];
        const auto source=ports_.objects.read().objects.find(binding.id);
        if(source!=ports_.objects.read().objects.end()) {
            if(operation.stage&(2u<<index)) {
                if(!operation.positionPrepared) {
                    operation.position=PointTarget{source->second.area,ports_.areas.at(source->second.area).generation,source->second.position};
                    const auto removed=ports_.objects.removeNative(binding.id);if(!removed) return removed;
                    operation.positionPrepared=true;
                }
            } else {
                const auto mode=ports_.objects.setNativeMode(binding.id,5,tick.tick);if(!mode) return mode;
            }
        }
        if(operation.positionPrepared && operation.position) {
            const auto &point=*operation.position;
            const auto born=ports_.monsters.admitPd2SceneMonster(point.area,point.position,989+index,
                "map-ancient."+std::to_string(id.value)+"."+std::to_string(index),tick.tick);
            if(isDeferred(born.status) || born.status==DomainStatus::NotImplemented) return {born.status,{}};
            if(born) bind(4+index,*born.value,point.area,point.position,false);
        }
        ++operation.attempt;operation.positionPrepared=false;operation.position.reset();
    }
    operation.active=false;return {DomainStatus::Applied,std::monostate{}};
}
}
