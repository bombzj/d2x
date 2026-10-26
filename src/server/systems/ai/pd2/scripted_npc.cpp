#include "../system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/maps/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"

namespace d2x::server::ai {
StepStatus System::pd2ScriptedNpc(EntityId id,TickContext tick,Controller &controller) {
    const auto &unit=*ports_.monsters.find(id);const auto &area=ports_.areas.at(unit.area);
    if(const auto result=pd2Lucion(id,tick,controller)) return *result;
    if(const auto result=pd2DiabloClone(id,tick,controller)) return *result;
    if(unit.identity.nativeClass>=989 && unit.identity.nativeClass<=991) return pd2Ancients(id,tick,controller);
    if(unit.identity.nativeClass==921 && area.definition.pd2Map && area.definition.pd2Map->controller==14) {
        if(!controller.phase) {controller.phase=1;controller.loop=0;}
        const auto target=acquireTarget(unit,tick,false,false);
        if(!target) {controller.nextDecision=tick.tick+10;return StepStatus::Complete;}
        if(target->distance>=20) {controller.nextDecision=tick.tick+40;return StepStatus::Complete;}
        // Current core NOPs Game A6E90's failed Act3 quest-coordinate guard.
        // Its out-of-map/uninitialized destination cannot produce a walk in
        // this Act5 scene. Preserve the failed-path phases, without invented
        // quest coordinates, fallback movement or additional random draws.
        controller.nextDecision=tick.tick+1;
        if(controller.phase==1) {controller.phase=2;return StepStatus::Complete;}
        if(controller.loop<3) {++controller.loop;return StepStatus::Complete;}
        const auto killed=perform(controller,[this,id,tick]{return ports_.monsters.scriptDeath(id,tick.tick,{},true);});
        return isDeferred(killed.status)?StepStatus::Blocked:killed?StepStatus::Complete:StepStatus::NotImplemented;
    }
    if(unit.identity.nativeClass!=1168) return StepStatus::NotImplemented;
    const auto values=ports_.effects.unitStatValues(id,tick.tick);if(!values) return StepStatus::Blocked;
    const auto &binding=ports_.maps.read().bindings[1];
    const bool boss=!binding.object && ports_.monsters.find(binding.id);
    const auto name=boss?"mon_cooldown2":"mon_cooldown1";
    const int phase=nativeStatValue(*values,*unit.rule.stats,name);
    const auto status=[](DomainResult<> result) {
        return isDeferred(result.status)?StepStatus::Blocked:result?StepStatus::Complete:StepStatus::NotImplemented;
    };
    if(!boss && phase>5) return status(perform(controller,[this,id] {
        const auto removed=ports_.monsters.remove(id);
        if(removed && ports_.maps.read().bindings[0].id==id) ports_.maps.bind(0,{},{},{},false);
        return removed;
    }));
    if(!boss && !phase) {
        const auto sound=perform(controller,[this,id] {return ports_.monsters.directSound(id,0x1218);});
        if(!sound) return status(sound);
    }
    // 37AA00 returns Levels' original tile origin in subtiles. The NPC
    // follows three authored coordinate requests, not a replacement layout.
    const Vec point{float(area.definition.tileX*5+(boss?(phase>2?57:56):25))-area.definition.origin.x,
        float(area.definition.tileY*5+37)-area.definition.origin.y};
    const auto moved=perform(controller,[this,id,point,region=unit.area,generation=area.generation] {
        const auto result=ports_.monsters.requestMove({id,{region,generation,point},{},0,75,false,1});
        if(isDeferred(result.status)) return result;
        state_.controllers.at(id).pursuing=bool(result);
        return DomainResult<>{DomainStatus::Applied,std::monostate{}};
    });
    if(!moved) return status(moved);
    controller.nextDecision=tick.tick+uint64_t(boss?(phase>2?25:5):50);
    if(!boss || phase<=2) {
        const auto written=setPd2AiStats(controller,tick,{{name,nativeAdd(phase,1)}});
        if(!written) return status(written);
    }
    return StepStatus::Complete;
}
}
