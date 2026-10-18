#include "system.hpp"
#include "native_birth.hpp"
#include "server/area_store.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::monsters {
bool System::canImpregnate(EntityId source,EntityId target,uint64_t tick) const {
    const auto *caster=find(source),*other=find(target);
    return caster && other && other->alive() && other->alignment()!=UnitAlignment::Good &&
        caster->rule.pregnantState>=0 && other->rule.baseClass!=caster->rule.baseClass &&
        other->rule.baseClass!=caster->rule.painwormBaseClass &&
        !ports_.effects.unitStates(target,tick).contains(caster->rule.pregnantState);
}
DomainResult<> System::spawnRemovedStateChild(EntityId id,int skill,int rank,uint64_t tick,
    const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    (void)rank;
    const auto *body=find(id);if(!body || body->alive()) return {DomainStatus::InvalidActor,{}};
    const auto &area=ports_.areas.at(body->area);
    const auto &program=body->rule.formulas->skills.at(skill);
    if(program.number("srvdofunc")!=133 || program.reference("summode")!="nu") return {DomainStatus::NotImplemented,{}};
    const auto initial=std::find_if(area.definition.nativeMonsterClasses.begin(),area.definition.nativeMonsterClasses.end(),
        [&](const auto &entry){return entry.second.identity.monster==program.reference("summon");});
    if(initial==area.definition.nativeMonsterClasses.end()) return {DomainStatus::NotImplemented,{}};
    const auto selected=area.definition.nativeSummonClasses.find(initial->first);
    const auto found=area.definition.nativeMonsterClasses.find(selected==area.definition.nativeSummonClasses.end()?initial->first:selected->second);
    if(found==area.definition.nativeMonsterClasses.end()) return {DomainStatus::NotImplemented,{}};
    const auto global=body->position+area.definition.origin;
    const auto room=ports_.areas.roomAtPosition(body->area,body->position,int(std::floor(global.x)),int(std::floor(global.y)));
    if(!room) return {DomainStatus::Unavailable,{}};
    auto loan=ports_.areas.roomRandom(*room);if(!loan) return {loan.status,{}};
    const auto &prototype=found->second;auto identity=prototype.identity;
    identity.spawnKey="state-child."+std::to_string(ports_.ids.cursor());identity.ownerSpawnKey.clear();identity.origin=SpawnOrigin::Summoned;
    const auto prepared=prepareRoomSpawn({identity,prototype.implementation,body->area,body->position,true,prototype.rule,{},tick},
        *loan.value,1,{},false,collision);
    if(!prepared) return {prepared.status,{}};
    if(!(*prepared.value)->validate()) return {DomainStatus::Stale,{}};
    (*prepared.value)->publish();return {DomainStatus::Applied,std::monostate{}};
}
}
