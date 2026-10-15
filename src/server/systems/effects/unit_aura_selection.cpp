#include "system.hpp"
#include <memory>
#include "attribute_projection.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/attributes/monster_sources.hpp"
#include "gameplay/skills/aura_resolve.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace d2x::server::effects {
DomainResult<> System::assignUnitSkills(EntityId id,std::span<const std::pair<int,int>> skills,uint64_t tick) {
    const auto *live=ports_.monsters.find(id);
    if(!live || !live->alive() || !live->rule.stats || !live->rule.formulas) return {DomainStatus::InvalidActor,{}};
    if(live->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    auto plan=std::make_shared<AuraUnitPublication>();auto candidate=*live;++candidate.revision;
    stageAuraActors(*plan,{{id,std::move(candidate)}},0);
    const std::vector<std::pair<int,int>> assignments(skills.begin(),skills.end());
    stageAuraSource(*plan,id,[assignments,tick](monsters::Actor &unit,const CombatEffectSet &states,const AuraUnitPublication::SourceMutation &mutation) {
        for(const auto &[skill,rank]:assignments) attributes::assignMonsterSkill(unit,states,skill,rank,tick,mutation);
    });
    auto prepared=ports_.transactions.prepareUnitAuras(plan,[&](const AuraRecipientPreparation &recipient){prepareStagedItemAuras(*plan,tick,recipient);});
    if(!prepared) return {prepared.status,{}};
    const ActorContext actor{live->owner.value_or(PlayerId{}),id,live->area,ports_.areas.at(live->area).generation,0,tick};
    return ports_.transactions.commitUnitAuras(std::move(*prepared.value),std::move(plan),actor);
}
DomainResult<> System::assignUnitAura(EntityId id,const AuraDefinition &definition,uint64_t tick,std::optional<int> assignedRank) {
    const auto *live=ports_.monsters.find(id);
    if(!live || !live->alive() || !live->rule.stats || !live->rule.formulas || !live->rule.auraPrograms ||
       !live->rule.auraPrograms->contains(definition.skill)) return {DomainStatus::Unavailable,{}};
    if(live->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    const ActorContext actor{live->owner.value_or(PlayerId{}),id,live->area,ports_.areas.at(live->area).generation,0,tick};
    auto plan=std::make_shared<AuraUnitPublication>();
    auto candidate=*live;++candidate.revision;
    stageAuraActors(*plan,{{id,std::move(candidate)}},0);
    auto prepared=ports_.transactions.prepareUnitAuras(plan,[&](const AuraRecipientPreparation &recipient) {
        auto &source=plan->actors->actors.at(id);const auto &rules=*source.rule.stats;const auto &catalog=*source.rule.formulas;
        UnitEffect state;
        if(const auto previous=units_.find(id);previous!=units_.end()) state=previous->second;
        else state.area=source.area;
        if(assignedRank) {
            attributes::assignMonsterSkill(source,state.states,definition.skill,*assignedRank,tick,
                [&](monsters::Actor &unit,MonsterResourceTrace &resources,const SkillAuraChange &change) {
                    struct Scope {AuraUnitPublication &plan;EntityId id;~Scope(){plan.activeSources.erase(id);}} scope{*plan,id};
                    plan->activeSources[id]=&resources;
                    prepareUnitItemAura(*plan,unit,change,tick,recipient);
                });
            if(const auto current=plan->units.find(id);current!=plan->units.end()) state=current->second;
        }
        MonsterResourceTrace resources(source,tick,&state.states);
        resources.sample(state.states,true);
        const auto observe=[&]{resources.sample(state.states,true);};
        const auto noMana=source.rule.noManaRegenState;
        if(noMana>=0 && state.states.hasState(noMana,tick)) {
            state.states.setNativeState(noMana,false);observe();plan->facts.emplace_back(StateFact{id,1,source.area,noMana,false});
        }
        if(source.rightSkill && source.rule.auraPrograms->contains(source.rightSkill->skill)) {
            const auto oldState=source.rule.auraPrograms->at(source.rightSkill->skill).ownerState.id;
            if(oldState>=0) {
                const bool active=state.states.hasState(oldState,tick);
                const auto removed=state.states.removeState(oldState,observe);
                state.states.setNativeState(oldState,false);observe();state.nativeStats.erase(oldState);
                if(active || !removed.empty()) plan->facts.emplace_back(StateFact{id,1,source.area,oldState,false});
            }
        }
        source.rightSkill=NativeSkillSlot{definition.skill,{}};
        auto life=resources.take();source.nativeStats=life.intrinsic;source.life=life.life.raw();source.maximumLife=life.maximum.raw();
        if(plan->units.contains(id)) {
            plan->units.at(id)=state;plan->life.insert_or_assign(id,std::move(life));plan->velocity[id]=state.states.modifiers(tick).velocityPercent;
        } else prepareUnitEffects(*plan,id,state,std::move(life),tick);
        auto snapshot=skills::formulaInput(source,source.nativeStats->values);
        const auto highest=snapshot.referenceRanks->effective.find(definition.skill);
        const int rank=highest==snapshot.referenceRanks->effective.end()?0:highest->second;
        std::vector<FrameEvent> timers;
        for(auto event:ports_.frameEvents.events({FrameEventUnit::Monster,id}))
            if(event.type==FrameEventType::PeriodicSkills && event.customId!=-1) {event.handle={event.payload};timers.push_back(event);}
        const auto &program=source.rule.auraPrograms->at(definition.skill);const int stateId=program.ownerState.id;
        if(rank>0 && stateId>=0) {
            SkillFormulaValues values(catalog,snapshot);
            const auto period=values.calculation(FormulaDomain::Skill,definition.skill,rank,"perdelay");
            if(!period) throw std::runtime_error("Unavailable native unit right aura period");
            const bool immediate=catalog.skills.at(definition.skill).number("immediate")!=0;
            const auto frame=std::make_shared<SkillFormulaSnapshot>(captureSkillFormulaInput(snapshot));
            SkillAuraChange activation{{},definition.skill,rank,stateId,*period,immediate,frame};
            if(immediate) {
                const auto aura=evaluateNativeAura(program,rank,catalog,snapshot);
                prepareImmediateAuraUnit(*plan,actor,aura,activation,id,true,&source);
                for(const auto target:auraCandidateRecipients(id,aura,source.area,actor.tick,&source)) recipient(actor,aura,activation,target);
            } else {
                auto &owner=plan->units.at(id);
                CombatEffectSpec empty;empty.state=program.ownerState;empty.source={CombatEffectSource::Skill,id,definition.skill,rank};
                empty.nativeState=true;empty.nativeValues=std::map<StatKey,int32_t>{{{rules.names.at("modifierlist_skill"),0},definition.skill},
                    {{rules.names.at("modifierlist_level"),0},rank}};
                MonsterResourceTrace trace(source,tick,&owner.states);
                const auto installed=owner.states.apply(std::move(empty),tick,[&]{trace.sample(owner.states,true);});
                if(!installed.accepted) throw std::runtime_error("Native unit right aura list admission failed");
                trace.sample(owner.states);plan->life.insert_or_assign(id,trace.take());
                owner.nativeStats[stateId]={{{rules.names.at("modifierlist_skill"),0},definition.skill},{{rules.names.at("modifierlist_level"),0},rank}};
                plan->facts.emplace_back(StateFact{id,1,source.area,stateId,true,owner.nativeStats.at(stateId)});
            }
            auto after=source;
            if(const auto result=plan->life.find(id);result!=plan->life.end()) {
                after.nativeStats=result->second.intrinsic;after.life=result->second.life.raw();after.maximumLife=result->second.maximum.raw();
            }
            auto input=skills::formulaInput(after,after.nativeStats->values);SkillFormulaValues completed(catalog,input);
            const auto nextPeriod=completed.calculation(FormulaDomain::Skill,definition.skill,rank,"perdelay");
            if(!nextPeriod) throw std::runtime_error("Unavailable completed native unit right aura period");
            timers.push_back({{ports_.frameEvents.reserveIdentity()},{FrameEventUnit::Monster,id},FrameEventType::PeriodicSkills,
                skillPeriodicTick(tick,*nextPeriod),skillPeriodicFrame(nativeNarrow(uint32_t(tick)),*nextPeriod)==-1,0,-1,0});
        }
        std::sort(timers.begin(),timers.end(),[](const auto &left,const auto &right){return left.handle.value>right.handle.value;});
        plan->skillTimers.insert_or_assign(id,ports_.frameEvents.prepareEdit({FrameEventUnit::Monster,id},FrameEventType::PeriodicSkills,timers));
    });
    if(!prepared) return {prepared.status,{}};
    return ports_.transactions.commitUnitAuras(std::move(*prepared.value),std::move(plan),actor);
}
}
