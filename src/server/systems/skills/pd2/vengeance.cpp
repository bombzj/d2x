#include "vengeance.hpp"
#include "execution.hpp"
#include "../system.hpp"
#include "../native_caster.hpp"
#include "server/systems/actions/release.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/combat/requests.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/effects/native_area.hpp"
#include "server/systems/effects/skill_state.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/effects/queries.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/skills/native_damage.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/pd2/arithmetic.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <stdexcept>

namespace d2x::server::skills {
struct Pd2VengeancePulse {
    EntityId source,center,selected;int rank{1},depth{};
    RegionId area;Vec position;MissileHitDamage damage;
    int chance{},delay{},protection{},state{-1};
    std::vector<EntityId> targets;size_t next{};
    std::shared_ptr<effects::NativeAreaPulse> contact;
    bool prepared{},centerMarked{},damaged{},recipientMarked{},chancePrepared{},propagate{},complete{};
};
struct Pd2VengeanceTimers {std::map<FrameEventHandle,std::shared_ptr<Pd2VengeancePulse>> pulses;};
namespace {
constexpr int vengeance=111,justHit=86;
DomainStatus advanceVengeance(const Ports &ports,Pd2VengeancePulse &pulse,uint64_t tick,Pd2VengeanceTimers &timers) {
    const combat::Participants participants{ports.players,ports.monsters,ports.social};
    const auto caster=nativeCaster(ports.players,ports.monsters,pulse.source);
    const auto center=participants.find(pulse.center);
    if(!caster || !caster->programs() || !center.id()) return DomainStatus::Unavailable;
    const auto &rules=*caster->programs();const auto &row=rules.formulas->skills.at(vengeance);
    const auto actor=caster->context(ports.areas,tick);
    if(!pulse.prepared) {
        pulse.damage={};
        const auto expected=caster->random();auto random=expected;
        auto input=caster->formulaInput(ports.effects,tick);SkillFormulaValues values(*rules.formulas,input);
        if(pulse.depth) {
            const auto level=input.referenceRanks->effective.find(vengeance);
            if(level==input.referenceRanks->effective.end() || level->second<=0) return DomainStatus::Unavailable;
            pulse.rank=level->second;
        }
        const auto formula=[&](std::string_view field) {
            const auto value=values.calculation(FormulaDomain::Skill,vengeance,pulse.rank,field);
            if(!value) throw std::logic_error("Missing PD2 Vengeance formula: "+std::string(field));
            return *value;
        };
        const auto stats=ports.effects.unitStatValues(caster->id(),tick);if(!stats) return DomainStatus::Unavailable;
        const auto stat=[&](std::string_view name){return stats->get({rules.stats->names.at(std::string(name)),0});};
        constexpr std::array<std::string_view,3> lows{"firemindam","coldmindam","lightmindam"},
            highs{"firemaxdam","coldmaxdam","lightmaxdam"},masteries{"passive_fire_mastery","passive_cold_mastery","passive_ltng_mastery"},
            percentages{"calc1","calc2","calc3"};
        constexpr std::array<size_t,3> channels{2,4,3};
        constexpr std::array<std::array<int,2>,3> excludedStates{{{35,16},{43,188},{46,-1}}};
        const auto own=ports.effects.unitEffects(caster->id());
        for(size_t index=0;index<channels.size();++index) {
            auto low=stat(lows[index]),high=stat(highs[index]);
            // Do174 subtracts Holy Fire/Freeze/Shock and both enchants before
            // rolling equipment elements. They are not amplified a second time.
            if(own) for(const auto state:excludedStates[index]) if(const auto effect=own->findState(state))
                if(const auto writes=combatEffectNativeValues(effect->spec)) {
                    const StatSnapshot source{*writes};
                    low=nativeSubtract(low,source.get({rules.stats->names.at(std::string(lows[index])),0}));
                    high=nativeSubtract(high,source.get({rules.stats->names.at(std::string(highs[index])),0}));
                }
            low=nativeShiftLeft(low,8);high=nativeShiftLeft(high,8);
            if(high>0) {
                const auto percent=nativeAdd(formula(percentages[index]),stat(masteries[index]));
                low=nativeAdd(low,nativePercentage(low,percent,100));high=nativeAdd(high,nativePercentage(high,percent,100));
                pulse.damage.channels[channels[index]]=std::max(0,nativeAdd(low,high>low?int32_t(limitedRandom(random,uint32_t(nativeSubtract(high,low)))):0));
            }
        }
        MissileHitDamage base;rollNativeSkillElement(base,values,vengeance,pulse.rank,random);
        for(size_t index=0;index<channels.size();++index)
            pulse.damage.channels[channels[index]]=nativeAdd(pulse.damage.channels[channels[index]],
                nativeAdd(base.channels[0],pd2MultiplyDivide(base.channels[0],stat(masteries[index]),100)));
        pulse.damage.coldLength=std::max(0,nativeSubtract(row.number("elen"),nativeMultiply(pulse.depth,15)));
        pulse.chance=formula("calc4");pulse.delay=row.number("param5");pulse.protection=nativeAdd(row.number("param6"),1);
        pulse.state=row.auraTargetState;
        pulse.area=caster->area();const auto centerArea=center.player?center.player->area:center.monster->area;
        pulse.position=center.position()+ports.areas.at(centerArea).definition.origin-ports.areas.at(pulse.area).definition.origin;
        const auto radius=pulse.depth?std::max(2,nativeSubtract(formula("aurarangecalc"),pulse.depth)):formula("aurarangecalc");
        pulse.targets=ports.effects.nativeAuraCandidateRecipients({caster->id(),pulse.area,radius,0x8583,false,
            pulse.position+ports.areas.at(pulse.area).definition.origin,effects::NativeAuraTargetMode::TargetRange,tick});
        const auto consumed=caster->monster?ports.monsters.consumeRandom(caster->id(),expected,random):ports.transactions.consumeRandom(actor,expected,random);
        if(!consumed) return consumed.status;
        pulse.prepared=true;
    }
    const auto mark=[&](EntityId unit,int state,int duration) {
        effects::NativeSkillStateValues stateValues;stateValues.state=state;
        return ports.effects.skillAuraState(pulse.source,unit,vengeance,pulse.rank,duration,tick,
            effects::NativeSkillStateProcedure::Infection,&stateValues);
    };
    if(!pulse.centerMarked) {
        if(pulse.state>=0) {const auto posted=mark(pulse.center,pulse.state,pulse.delay);if(isDeferred(posted.status)) return posted.status;}
        pulse.centerMarked=true;
    }
    while(pulse.next<pulse.targets.size()) {
        const auto id=pulse.targets[pulse.next];const auto recipient=participants.find(id);
        const auto region=recipient.player?recipient.player->area:recipient.monster?recipient.monster->area:RegionId{};
        if(!recipient.aliveIn(region) && !pulse.contact) {
            pulse.damaged=pulse.recipientMarked=pulse.chancePrepared=pulse.propagate=false;++pulse.next;continue;
        }
        if(!pulse.damaged) {
            if(!pulse.contact) {
                pulse.contact=std::make_shared<effects::NativeAreaPulse>();auto &area=pulse.contact->spec;
                area.source=pulse.source;area.area=pulse.area;area.skill=vengeance;area.rank=pulse.rank;area.targets=std::vector<EntityId>{id};
                area.damage=pulse.damage;area.resultFlags=1;
            }
            const auto hit=ports.effects.nativeArea(pulse.contact->spec,tick,pulse.contact);if(!hit) return hit.status;
            pulse.damaged=true;
        }
        const auto current=participants.find(id);
        if(current.aliveIn(region) && (pulse.recipientMarked || !ports.effects.unitStates(id,tick).contains(justHit))) {
            if(!pulse.recipientMarked) {
                const auto posted=mark(id,justHit,pulse.protection);if(isDeferred(posted.status)) return posted.status;
                pulse.recipientMarked=true;
            }
            if(!pulse.chancePrepared) {
                const auto owner=nativeCaster(ports.players,ports.monsters,pulse.source);if(!owner) return DomainStatus::Stale;
                const auto expected=owner->random();auto random=expected;
                const auto action=ports.actions.casts.find(pulse.source);
                const auto selected=action==ports.actions.casts.end()?pulse.selected:action->second.target;
                pulse.propagate=id!=selected && pd2UnitEventRandom(random)%100<uint32_t(pulse.chance);
                const auto consumed=owner->monster?ports.monsters.consumeRandom(owner->id(),expected,random):
                    ports.transactions.consumeRandom(owner->context(ports.areas,tick),expected,random);
                if(!consumed) return consumed.status;
                pulse.chancePrepared=true;
            }
            if(pulse.propagate) {
                auto next=std::make_shared<Pd2VengeancePulse>();next->source=pulse.source;next->center=id;next->selected=pulse.selected;
                next->rank=pulse.rank;next->depth=nativeAdd(pulse.depth,1);
                auto timer=ports.frameEvents.prepareInsertion({{}, {current.player?FrameEventUnit::Player:FrameEventUnit::Monster,id},
                    FrameEventType::MonsterModifier,tick+uint64_t(std::max(1,pulse.delay)),false,pd2VengeanceTimerHandler});
                timers.pulses.emplace(timer.handle(),std::move(next));ports.frameEvents.publish(std::move(timer));
            }
        }
        pulse.contact.reset();pulse.damaged=pulse.recipientMarked=pulse.chancePrepared=pulse.propagate=false;++pulse.next;
    }
    const auto cleared=ports.effects.setNativeSkillState(pulse.center,107,false,tick);if(isDeferred(cleared.status)) return cleared.status;
    pulse.complete=true;return DomainStatus::Applied;
}
}
DomainStatus pd2VengeanceAfterHit(const Ports &ports,actions::SkillRelease &pending,const ActorContext &actor,
    std::shared_ptr<Pd2VengeanceTimers> &timers) {
    if(!pending.nativeContact || !pending.nativeContact->accuracyHit) return DomainStatus::Applied;
    if(!timers) timers=std::make_shared<Pd2VengeanceTimers>();
    if(!pending.pd2Execution) pending.pd2Execution=std::make_shared<Pd2CastExecution>();
    auto &entry=pending.pd2Execution->vengeance;
    if(!entry) {entry=std::make_shared<Pd2VengeancePulse>();entry->source=actor.actor;entry->center=entry->selected=pending.unit;entry->rank=pending.skill.rank;}
    if(entry->complete) return DomainStatus::Applied;
    pending.nativeCallbackStarted=true;const auto result=advanceVengeance(ports,*entry,actor.tick,*timers);
    if(result==DomainStatus::Applied) pending.nativeCallbackStarted=false;
    return result;
}
DomainResult<> pd2VengeanceEvent(const Ports &ports,const FrameEvent &event,uint64_t tick,std::shared_ptr<Pd2VengeanceTimers> &timers) {
    if(!timers) return {DomainStatus::Applied,std::monostate{}};
    const auto found=timers->pulses.find(event.handle);if(found==timers->pulses.end()) return {DomainStatus::Applied,std::monostate{}};
    const auto result=advanceVengeance(ports,*found->second,tick,*timers);
    if(isDeferred(result)) return {result,{}};
    timers->pulses.erase(event.handle);ports.frameEvents.cancel(event.handle);return {DomainStatus::Applied,std::monostate{}};
}
}
