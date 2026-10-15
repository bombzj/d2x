#include "system.hpp"
#include "server/area_store.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "server/systems/combat/participants.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/event_frame.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/units/resources.hpp"
#include <array>

namespace d2x::server::monsters {
DomainResult<> System::regenerate(EntityId id,uint64_t tick) {
    const auto *actor=find(id);const FrameEventOwner owner{FrameEventUnit::Monster,id};
    if(!actor || !actor->alive() || actor->life<=0) {ports_.frameEvents.cancel(owner,FrameEventType::StatRegeneration);return {DomainStatus::Applied,std::monostate{}};}
    const auto sampled=ports_.effects.unitStatValues(id,tick);
    if(!sampled || !actor->rule.stats || !actor->nativeStats || !actor->nativeStats->sources) return {DomainStatus::Unavailable,{}};
    const auto &rules=*actor->rule.stats;
    const auto aiRate=nativeChecked(actor->aiRegenerationBonus);
    auto rate=nativeAdd(nativeStatValue(*sampled,rules,"hpregen"),aiRate);
    if(ports_.effects.unitLifeState(id,tick))
        rate=nativeSubtract(rate,nativeAdd(baseStatValue(rules,*actor->nativeStats->sources,{rules.names.at("hpregen"),0}),aiRate));
    const auto states=ports_.effects.unitStates(id,tick);
    if(!rate || (rate>=0 && states.contains(actor->rule.preventHealState))) return {DomainStatus::Applied,std::monostate{}};
    const auto deadline=nativeEventDeadline(tick,nativeAdd(nativeNarrow(uint32_t(tick)),1));
    std::vector<FrameEvent> next{{{},owner,FrameEventType::StatRegeneration,deadline.due,deadline.continuous}};
    auto value=Fixed8::fromRaw(nativeChecked(actor->life));
    const bool town=ports_.areas.at(actor->area).definition.town;
    // MonsterMode::ApplyStatRegen permits loss below one HP outside town;
    // it does not apply the player's 256-raw lower clamp.
    if(!(rate<0 && actor->life<256 && town)) {
        const auto raw=nativeAdd(value.raw(),rate);
        if(raw>actor->maximumLife) next.clear();
        advanceLifeRegeneration(value,Fixed8::fromRaw(nativeChecked(actor->maximumLife)),rate);
    }
    if(value<=0) {
        EntityId source;bool foundOwner=false;
        if(const auto *effects=ports_.effects.unitEffects(id)) {
            for(const int state:{actor->rule.hitStates.poison.id,actor->rule.openWoundsState}) {
                if(const auto *effect=effects->findState(state)) {source=effect->spec.source.entity;foundOwner=true;}
                if(foundOwner) break;
            }
        }
        const combat::Participants participants{ports_.players,*this,ports_.social};
        const auto credit=participants.bind(participants.find(source)).credit;
        const auto result=damage(id,credit,actor->life,tick,0,false,0,{},true,0,{},0,false,0,0,true,{},false,{source});
        if(!result) return result;
        recordHirelingKill(id,source);ports_.frameEvents.cancel(owner,FrameEventType::StatRegeneration);
        return result;
    }
    auto events=ports_.frameEvents.prepareEdit(owner,FrameEventType::StatRegeneration,next);
    attributes::MonsterResourceTrace resources(*actor,tick,ports_.effects.unitEffects(id));resources.seedLife(value);
    if(actor->poison) {auto poison=actor->poison;poison->next=tick+1;resources.seedPoison(std::move(poison));}
    auto life=resources.take();
    if(!canCommitLife(life) || !ports_.frameEvents.validate(events)) return {DomainStatus::Conflict,{}};
    if(!commitLife(std::move(life))) return {DomainStatus::Conflict,{}};
    ports_.frameEvents.publish(std::move(events));return {DomainStatus::Applied,std::monostate{}};
}
}
