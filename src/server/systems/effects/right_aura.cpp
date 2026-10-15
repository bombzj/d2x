#include "system.hpp"
#include "attribute_projection.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/skills/aura_resolve.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace d2x::server::effects {
RightAuraTimerChange System::prepareRightAuraSelection(const ActorContext &actor,const PlayerState &player,const CharacterRecord &record,
    CombatEffectSet &states,const attributes::AuraSourceObserver &observe,const attributes::AuraSourceReader &read,
    PlayerPoisonCandidate &poison,std::vector<DomainFact> &facts,const AuraRecipientPreparation &recipient,bool force) {
    RightAuraTimerChange change;
    if(!player.rules.skills || !player.rules.skills->formulas || !player.rules.character || !player.rules.character->stats) return change;
    const auto &rules=*player.rules.character->stats;const auto &catalog=*player.rules.skills->formulas;
    const auto oldSlot=player.persistent.player.weaponSet*2+1,newSlot=record.weaponSet*2+1;
    const auto oldSkill=player.persistent.player.selectedSkills.at(oldSlot),skill=record.selectedSkills.at(newSlot);
    auto snapshot=read();
    const auto &ranks=snapshot.referenceRanks?snapshot.referenceRanks->effective:snapshot.effectiveRanks;
    const auto rank=ranks.find(skill);const int level=rank==ranks.end()?0:rank->second;
    const bool aura=snapshot.currentResources.at({rules.names.at("hitpoints"),0})>0 && level>0 && player.rules.skills->auras.contains(skill);
    const FrameEventOwner owner{FrameEventUnit::Player,actor.actor};
    const auto timers=ports_.frameEvents.events(owner);
    const bool registered=std::any_of(timers.begin(),timers.end(),[](const auto &event){return event.type==FrameEventType::PeriodicSkills && event.customId==-1;});
    const bool changed=oldSkill!=skill || player.persistent.player.selectedSkillOwners.at(oldSlot)!=record.selectedSkillOwners.at(newSlot) || oldSlot!=newSlot;
    if(!force && !changed && (aura==registered)) return change;
    change.replace=true;
    const auto publish=[&] {
        auto projection=projectEffectAttributes(states,actor.tick);
        observe({std::move(*projection.sources),std::move(projection.states),std::move(projection.effects),{}});
    };
    const auto noMana=player.rules.character->noManaRegenState;
    if(noMana>=0 && states.hasState(noMana,actor.tick)) {
        states.setNativeState(noMana,false);publish();
        facts.emplace_back(StateFact{actor.actor,0,actor.area,noMana,false});
    }
    if(const auto old=player.rules.skills->auras.find(oldSkill);old!=player.rules.skills->auras.end()) {
        const auto state=old->second.spec.ownerState.id;
        if(state>=0) {
            const bool active=states.hasState(state,actor.tick);
            const auto removed=states.removeState(state,publish);
            states.setNativeState(state,false);publish();
            if(active || !removed.empty()) facts.emplace_back(StateFact{actor.actor,0,actor.area,state,false});
        }
    }
    if(!aura) return change;
    const auto &rule=player.rules.skills->auras.at(skill);const auto state=rule.spec.ownerState.id;
    if(state<0) return change;
    snapshot=read();auto input=snapshot.input(rules);SkillFormulaValues values(catalog,input);
    const auto period=values.calculation(FormulaDomain::Skill,skill,level,"perdelay");
    if(!period) throw std::runtime_error("Unavailable right aura period");
    SkillAuraChange activation{{},skill,level,state,*period,rule.immediate,std::make_shared<SkillFormulaSnapshot>(snapshot)};
    if(rule.immediate) {
        const auto auraValue=evaluateNativeAura(rule.spec,level,catalog,input);
        // Current MPQ immediate paladin auras use the complete BasicAura
        // program. Candidate recipients share this transaction's graph.
        applyImmediateAuraOwner(states,actor,auraValue,activation,rules,noMana,catalog.skills.at(skill).passiveState,observe,read,facts,poison);
        for(const auto id:auraCandidateRecipients(actor.actor,auraValue,actor.area,actor.tick)) recipient(actor,auraValue,activation,id);
    } else {
        CombatEffectSpec empty;empty.state=rule.spec.ownerState;empty.source={CombatEffectSource::Skill,actor.actor,skill,level};
        empty.nativeState=true;empty.nativeValues=std::map<StatKey,int32_t>{{{rules.names.at("modifierlist_skill"),0},skill},
            {{rules.names.at("modifierlist_level"),0},level}};
        // AssignSkill posts an indefinite empty list. The first Do callback
        // installs the actual writes/removal callback and expiry separately.
        const auto applied=states.apply(std::move(empty),actor.tick,publish);
        if(!applied.accepted) throw std::runtime_error("Right aura list admission failed");
        facts.emplace_back(StateFact{actor.actor,0,actor.area,state,true,
            std::vector<StatValue>{{{rules.names.at("modifierlist_skill"),0},skill},{{rules.names.at("modifierlist_level"),0},level}}});
    }
    // Rescheduling observes the completed immediate Do's sources/resources.
    snapshot=read();auto afterInput=snapshot.input(rules);SkillFormulaValues after(catalog,afterInput);
    const auto nextPeriod=after.calculation(FormulaDomain::Skill,skill,level,"perdelay");
    if(!nextPeriod) throw std::runtime_error("Unavailable completed right aura period");
    change.event=FrameEvent{{ports_.frameEvents.reserveIdentity()},owner,FrameEventType::PeriodicSkills,
        skillPeriodicTick(actor.tick,*nextPeriod),skillPeriodicFrame(nativeNarrow(uint32_t(actor.tick)),*nextPeriod)==-1,0,-1,0};
    return change;
}
DomainResult<> System::rightAuraEvent(const FrameEvent &event,uint64_t tick) {
    const auto id=event.owner.unit;const auto *player=ports_.players.findActor(id);
    auto clear=[&] {ports_.frameEvents.cancel(event.handle);paladinCycles_.erase(id);return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    if(!player || !player->entered) return clear();
    auto &cycle=paladinCycles_[id];
    if(cycle.eventOrder!=event.handle.value) {cycle=PaladinCycle{};cycle.eventOrder=event.handle.value;}
    if(cycle.skill<0) {
        const auto skill=player->persistent.player.selectedSkills.at(player->persistent.player.weaponSet*2+1);
        const auto selected=skills::selectedSource(*player,skill,true);
        if(player->persistent.player.hp<=0 || !selected || !player->rules.skills->auras.contains(skill)) return clear();
        cycle.skill=skill;cycle.rank=selected->rank;cycle.area=player->area;cycle.random=player->combatRandom;
    }
    const ActorContext actor{player->player,id,player->area,ports_.areas.at(player->area).generation,0,tick};
    if(advanceAuraCycle(actor,*player,player->rules.skills->auras.at(cycle.skill).spec,cycle,tick)==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
    // Native periodic right-skill callback uses the current active skill level at
    // entry, then schedules after its synchronous program completes.
    if(player->persistent.player.hp>0) {
        auto input=skills::formulaInput(*player);SkillFormulaValues values(*player->rules.skills->formulas,input);
        const auto period=values.calculation(FormulaDomain::Skill,cycle.skill,cycle.rank,"perdelay");
        if(!period) return {DomainStatus::Unavailable,{}};
        FrameEvent replacement{{},event.owner,FrameEventType::PeriodicSkills,skillPeriodicTick(tick,*period),
            skillPeriodicFrame(nativeNarrow(uint32_t(tick)),*period)==-1,0,-1,0,event.payload};
        auto prepared=ports_.frameEvents.prepareInsertion(replacement);
        ports_.frameEvents.cancel(event.owner,FrameEventType::PeriodicSkills,-1);
        ports_.frameEvents.publish(std::move(prepared));
    } else ports_.frameEvents.cancel(event.handle);
    paladinCycles_.erase(id);return {DomainStatus::Applied,std::monostate{}};
}
}
