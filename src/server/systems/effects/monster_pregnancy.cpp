#include "system.hpp"
#include "attribute_projection.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/skills/curse_spec.hpp"
#include <stdexcept>

namespace d2x::server::effects {
DomainResult<> System::monsterPregnancy(EntityId source,EntityId target,int skill,int rank,uint64_t tick) {
    if(!ports_.monsters.canImpregnate(source,target,tick)) return {DomainStatus::Unavailable,{}};
    const auto &caster=*ports_.monsters.find(source),&body=*ports_.monsters.find(target);
    if(!body.rule.stateTarget.acceptsCurseList() || !body.nativeStats) return {DomainStatus::Unavailable,{}};
    UnitEffect unit;if(const auto old=units_.find(target);old!=units_.end()) unit=old->second;unit.area=body.area;
    CombatEffectSpec spec;spec.state=caster.rule.states->at(caster.rule.pregnantState);
    spec.source={CombatEffectSource::Skill,source,skill,rank};spec.stacking=EffectStacking::ReuseState;
    spec.nativeState=true;spec.nativeValues.emplace();spec.nativeRemoval=NativeStateRemoval::Pregnant;
    CurseSpec program;program.state=spec.state;program.nativeValues.emplace();program.rejectZeroPrimary=false;
    MonsterResourceTrace resources(body,tick,&unit.states);
    const auto result=applyCurseState(unit.states,program,spec,{},*body.rule.stats,tick,[&]{resources.sample(unit.states,true);});
    if(!result.accepted) return {DomainStatus::Unavailable,{}};
    unit.nativeStats[spec.state.id]={};resources.sample(unit.states);
    AuraUnitPublication publication;prepareUnitEffects(publication,target,unit,resources.take(),tick);
    publication.facts.emplace_back(StateFact{target,1,body.area,spec.state.id,true});
    if(!validateAuraUnits(publication)) return {DomainStatus::Conflict,{}};
    const auto posted=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},body.area},std::move(publication.facts)});
    if(!posted) return {posted.status,{}};
    publishAuraUnits(publication);return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::finishNativeStateRemovals(uint64_t tick,const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    while(!nativeStateRemovals_.empty()) {
        const auto &operation=nativeStateRemovals_.front();
        const auto result=ports_.monsters.spawnRemovedStateChild(operation.unit,operation.skill,operation.rank,tick,collision);
        if(isDeferred(result.status) || result.status==DomainStatus::Conflict || result.status==DomainStatus::Stale) return StepStatus::Blocked;
        if(!result && result.status!=DomainStatus::InvalidActor && result.status!=DomainStatus::Unavailable)
            throw std::logic_error("Missing native state removal child program");
        nativeStateRemovals_.pop_front();
    }
    return StepStatus::Complete;
}
}
