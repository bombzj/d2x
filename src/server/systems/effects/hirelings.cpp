#include "system.hpp"
#include "attribute_projection.hpp"
#include "gameplay/skills/targeting.hpp"
#include "gameplay/skills/aura_resolve.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <optional>
#include <stdexcept>

namespace d2x::server::effects {
bool System::hirelingAuraActive(EntityId id,int skill,int rank) const {
    const auto *unit=ports_.monsters.find(id);
    if(!unit || !unit->rightSkill || unit->rightSkill->skill!=skill) return false;
    const auto assigned=unit->rule.baseSkillRanks.find(skill);
    if(assigned==unit->rule.baseSkillRanks.end() || assigned->second!=rank) return false;
    const auto timers=ports_.frameEvents.events({FrameEventUnit::Monster,id});
    return std::any_of(timers.begin(),timers.end(),[](const auto &event){return event.type==FrameEventType::PeriodicSkills && event.customId==-1;});
}
DomainResult<> System::hirelingAura(EntityId id,const AuraDefinition &a,uint64_t tick) {
    const auto *unit=ports_.monsters.find(id);
    if(!unit || (!unit->hireling && !unit->necroSummon) || !unit->owner) return {DomainStatus::InvalidActor,{}};
    return assignUnitAura(id,a,tick);
}
StepStatus System::advanceUnitAuraCycle(const ActorContext &actor,EntityId id,PaladinCycle &cycle,uint64_t tick) {
    if(cycle.pulseCommitted) return StepStatus::Complete;
    const auto *m=ports_.monsters.find(id);
    if(!m || !m->alive()) return StepStatus::Complete;
    const auto &area=ports_.areas.at(m->area);const auto &a=cycle.aura;
    if(!cycle.pending) {
        cycle.aura=evaluateUnitAura(id,cycle.skill,cycle.rank,tick);
        cycle.duration=uint32_t(skillPeriodicDuration(nativeNarrow(uint32_t(tick)),cycle.aura.periodFrames));
        cycle.pulseCommitted=false;cycle.progress={};cycle.damage.reset();cycle.recipientsPrepared=false;
        cycle.occurrence=++paladinOccurrence_;cycle.targets={id};cycle.target=0;cycle.pending=true;
    }
    while(cycle.target<cycle.targets.size()) {
        const auto target=cycle.targets[cycle.target];
        DomainResult<> result;
        if(target!=id && a.serverDoFunction==82) {
            const auto redeemed=redeemUnitCorpse(id,target,cycle.skill,cycle.rank,tick);
            result=redeemed?DomainResult<>{DomainStatus::Applied,std::monostate{}}:DomainResult<>{redeemed.status,{}};
        } else result=advanceAuraTarget(actor,id,a,target,cycle.duration,(uint64_t(1)<<60)|cycle.occurrence,
            cycle.damage.value_or(0),cycle.progress,target==id);
        if(isDeferred(result.status)) return StepStatus::Blocked;
        if(target==id && !cycle.recipientsPrepared) {
            if(!cycle.damage) {
                const auto after=evaluateUnitAura(id,cycle.skill,cycle.rank,tick);
                cycle.aura.minimumDamage=after.minimumDamage;cycle.aura.maximumDamage=after.maximumDamage;cycle.aura.radius=after.radius;
                if((a.serverDoFunction==66 || a.serverDoFunction==81) && area.definition.town) cycle.damage=0;
                else {const auto rolled=rollAuraDamage(id,a,tick);if(!rolled) return StepStatus::Blocked;cycle.damage=*rolled.value;}
            }
            if(!((a.serverDoFunction==66 || a.serverDoFunction==81) && area.definition.town)) {
                auto recipients=auraCandidateRecipients(id,a,m->area,tick);
                cycle.targets.insert(cycle.targets.end(),recipients.begin(),recipients.end());
            }
            cycle.recipientsPrepared=true;
        }
        ++cycle.target;cycle.progress={};
    }
    cycle.pulseCommitted=true;cycle.targets.clear();
    return StepStatus::Complete;
}
DomainResult<> System::unitAuraEvent(const FrameEvent &event,uint64_t tick) {
    const auto id=event.owner.unit;const auto *unit=ports_.monsters.find(id);
    const auto clear=[&] {ports_.frameEvents.cancel(event.handle);unitAuraCycles_.erase(id);return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    if(!unit || !unit->alive() || !unit->rule.auraPrograms) return clear();
    auto &cycle=unitAuraCycles_[id];
    if(cycle.eventOrder!=event.handle.value) {
        auto stats=unitStatValues(id,tick);if(!stats) return {DomainStatus::Unavailable,{}};
        auto input=skills::formulaInput(*unit,std::move(*stats));
        int skill=event.customId,rank{};
        if(skill==-1) {
            if(!unit->rightSkill) return clear();
            skill=unit->rightSkill->skill;
            const auto selected=input.referenceRanks->effective.find(skill);
            if(selected==input.referenceRanks->effective.end()) return clear();
            rank=selected->second;
        } else {
            const auto effects=units_.find(id);const ActiveCombatEffect *source=nullptr;
            if(effects!=units_.end()) for(const auto &entry:effects->second.states.entries())
                if(entry.spec.source.entity==id && entry.spec.source.definition==skill && effects->second.states.hasState(entry.spec.state.id,tick)) {source=&entry;break;}
            if(!source || !input.referenceRanks->effective.contains(skill)) return clear();
            rank=source->spec.source.level;
        }
        if(!unit->rule.auraPrograms->contains(skill)) return clear();
        cycle=PaladinCycle{};cycle.eventOrder=event.handle.value;cycle.skill=skill;cycle.rank=rank;
        cycle.area=unit->area;cycle.aura=evaluateUnitAura(id,skill,rank,tick);
    }
    const ActorContext actor{unit->owner.value_or(PlayerId{}),id,unit->area,ports_.areas.at(unit->area).generation,0,tick};
    if(advanceUnitAuraCycle(actor,id,cycle,tick)==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
    if(!unit->alive()) return clear();
    const auto after=evaluateUnitAura(id,cycle.skill,cycle.rank,tick);
    FrameEvent replacement{{},event.owner,FrameEventType::PeriodicSkills,skillPeriodicTick(tick,after.periodFrames),
        skillPeriodicFrame(nativeNarrow(uint32_t(tick)),after.periodFrames)==-1,0,event.customId,event.customId==-1?0:cycle.rank};
    auto prepared=ports_.frameEvents.prepareInsertion(replacement);
    ports_.frameEvents.cancel(event.owner,FrameEventType::PeriodicSkills,event.customId);ports_.frameEvents.publish(std::move(prepared));
    unitAuraCycles_.erase(id);return {DomainStatus::Applied,std::monostate{}};
}
}
