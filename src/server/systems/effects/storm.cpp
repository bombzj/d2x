#include "system.hpp"
#include "native_area.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include "gameplay/skills/aura_resolve.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <utility>

namespace d2x::server::effects {
FrameEventQueue::PreparedEdit System::prepareStormTimers(const ActorContext &actor,const PlayerState &player,
    const CombatEffectSet &states,const attributes::Totals &totals,const CharacterRecord &record,const RightAuraTimerChange &right) const {
    const FrameEventOwner owner{FrameEventUnit::Player,actor.actor};
    const auto existing=ports_.frameEvents.events(owner);
    std::vector<FrameEvent> events;
    // Right-skill periodic execution owns custom ID -1 independently of the
    // state-driven positive-skill callbacks.
    for(auto event:existing) if(!right.replace && record.hp>0 && event.type==FrameEventType::PeriodicSkills && event.customId==-1) {
        event.handle={event.payload};events.push_back(event);
    }
    if(right.replace && right.event && record.hp>0) events.push_back(*right.event);
    if(record.hp>0 && player.rules.skills && player.rules.skills->formulas) {
        auto input=skills::formulaInput(player,record,totals);SkillFormulaValues values(*player.rules.skills->formulas,input);
        const auto previous=state_.players.find(actor.actor);
        for(const auto &effect:states.entries()) {
            const auto skill=effect.spec.source.definition;
            const auto definition=player.rules.skills->definitions.find(skill);
            if(effect.spec.source.kind!=CombatEffectSource::Skill || definition==player.rules.skills->definitions.end() ||
               (definition->second.spec.effect!=SkillBehavior::ThunderStorm &&
                !(definition->second.spec.assassin && definition->second.spec.assassin->program==AssassinProgram::BladeShield)) ||
               !states.hasState(effect.spec.state.id,actor.tick)) continue;
            const auto old=std::find_if(existing.begin(),existing.end(),[&](const auto &event){return event.type==FrameEventType::PeriodicSkills && event.customId==skill;});
            const auto *oldEffect=previous==state_.players.end()?nullptr:previous->second.states.find(effect.handle);
            if(old!=existing.end() && oldEffect && oldEffect->revision==effect.revision && oldEffect->spec.source==effect.spec.source) {
                auto event=*old;event.handle={event.payload};events.push_back(event);continue;
            }
            const auto delay=values.calculation(FormulaDomain::Skill,skill,effect.spec.source.level,"perdelay");
            if(!delay) throw std::runtime_error("Unavailable native periodic skill delay");
            const auto due=skillPeriodicTick(actor.tick,*delay);
            const auto continuous=skillPeriodicFrame(nativeNarrow(uint32_t(actor.tick)),*delay)==-1;
            events.push_back({{ports_.frameEvents.reserveIdentity()},{},FrameEventType::PeriodicSkills,due,continuous,0,skill,effect.spec.source.level});
        }
    }
    std::sort(events.begin(),events.end(),[](const auto &left,const auto &right){return left.handle.value>right.handle.value;});
    return ports_.frameEvents.prepareEdit(owner,FrameEventType::PeriodicSkills,events);
}
DomainResult<> System::stormEvent(const FrameEvent &event,uint64_t tick) {
    const auto *player=ports_.players.findActor(event.owner.unit);
    if(bladeShieldPulses_.contains(event.handle)) return bladeShieldEvent(event,tick);
    if(player) if(const auto definition=player->rules.skills->definitions.find(event.customId);definition!=player->rules.skills->definitions.end() &&
        definition->second.spec.assassin && definition->second.spec.assassin->program==AssassinProgram::BladeShield) return bladeShieldEvent(event,tick);
    const auto retained=stormPulses_.find(event.handle);
    if((!player || !player->entered || player->persistent.player.hp<=0) && (retained==stormPulses_.end() || !retained->second.launched)) {
        ports_.frameEvents.cancel(event.handle);stormPulses_.erase(event.handle);return {DomainStatus::Applied,std::monostate{}};
    }
    auto &pulse=stormPulses_[event.handle];
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    if(!pulse.prepared) {
        const auto recovery=state_.players.find(player->actor);
        const ActiveCombatEffect *effect=nullptr;
        if(recovery!=state_.players.end()) for(const auto &entry:recovery->second.states.entries())
            if(entry.spec.source.kind==CombatEffectSource::Skill && entry.spec.source.definition==event.customId &&
               recovery->second.states.hasState(entry.spec.state.id,tick)) {effect=&entry;break;}
        auto input=skills::formulaInput(*player);const auto &ranks=input.referenceRanks->effective;
        const auto highest=ranks.find(event.customId);
        if(!effect || highest==ranks.end()) {
            ports_.frameEvents.cancel(event.handle);stormPulses_.erase(event.handle);return {DomainStatus::Applied,std::monostate{}};
        }
        pulse.skill=skills::evaluate(*player,event.customId,effect->spec.source.level);pulse.area=player->area;
        const auto last=stormLast_[{event.owner.unit,event.customId}];EntityId next,fallback;
        for(const auto candidate:nativeAuraCandidateRecipients({player->actor,player->area,pulse.skill.stormRadius,0xA783,false,{},NativeAuraTargetMode::OwnerRange,tick})) {
            if(candidate.value>uint32_t(last.value) && (!next || candidate<next)) next=candidate;
            if(candidate.value<=uint32_t(last.value) && (!fallback || candidate<fallback)) fallback=candidate;
        }
        pulse.target=next?next:fallback;
        if(pulse.target) {
            const auto target=participants.find(pulse.target);pulse.position=target.position();
            pulse.area=target.player?target.player->area:target.monster->area;
        }
        pulse.prepared=true;
    }
    if(pulse.target && !pulse.launched) {
        const auto *area=player?ports_.areas.find(player->area):nullptr;
        const auto target=participants.find(pulse.target);
        const auto *destination=ports_.areas.find(pulse.area);
        if(player && area && destination && !area->definition.town && !destination->definition.town && (target.player || target.monster)) {
            missiles::NativeSpawn request{player->actor,pulse.area,destination->generation,tick,pulse.position,pulse.position,
                pulse.skill.missileId,pulse.skill.sourceId,pulse.skill.rank};request.transient=true;
            const auto result=ports_.missiles.spawnNative(request);
            if(isDeferred(result.status) || result.status==DomainStatus::Stale || result.status==DomainStatus::Conflict) return {result.status,{}};
            if(!result) return {result.status,{}};
            pulse.projectile=*result.value;
            const auto contact=ports_.missiles.nativeAreaContact(pulse.projectile,true);if(!contact) return {DomainStatus::Unavailable,{}};
            pulse.damage=std::make_shared<NativeAreaPulse>();auto &spec=pulse.damage->spec;
            spec.source=player->actor;spec.area=pulse.area;spec.center=pulse.position;spec.skill=pulse.skill.sourceId;spec.rank=pulse.skill.rank;
            spec.targets=std::vector<EntityId>{pulse.target};spec.missileContact=std::make_shared<combat::SpellImpact>(*contact);
            spec.rollMissileDamage=true;spec.resultFlags=1;
            pulse.notices.emplace_back(SkillPulseFact{player->actor,pulse.target,0,uint8_t(target.player?0:1),pulse.area,pulse.skill.sourceId,pulse.skill.rank,pulse.position});
        }
        pulse.launched=true;
    }
    if(pulse.projectile && !pulse.damaged) {
        const auto result=nativeArea(pulse.damage->spec,tick,pulse.damage);
        if(isDeferred(result.status) || result.status==DomainStatus::Conflict || result.status==DomainStatus::Stale) return {DomainStatus::Capacity,{}};
        ports_.missiles.removeTransient(pulse.projectile);
        pulse.damaged=true;
    }
    if(!pulse.notified) {
        if(!pulse.notices.empty()) {
            const auto result=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},pulse.area},pulse.notices});
            if(!result) return {result.status,{}};
        }
        stormLast_[{event.owner.unit,event.customId}]=pulse.target;
        pulse.notified=true;
    }
    if(!pulse.rescheduled) {
        if(player && player->entered && player->persistent.player.hp>0) {
            auto input=skills::formulaInput(*player);SkillFormulaValues values(*player->rules.skills->formulas,input);
            const auto period=values.calculation(FormulaDomain::Skill,pulse.skill.sourceId,pulse.skill.rank,"perdelay");
            if(!period) return {DomainStatus::Unavailable,{}};
            FrameEvent replacement{{},event.owner,FrameEventType::PeriodicSkills,skillPeriodicTick(tick,*period),
                skillPeriodicFrame(nativeNarrow(uint32_t(tick)),*period)==-1,0,event.customId,pulse.skill.rank,event.payload};
            pulse.next=ports_.frameEvents.prepareInsertion(replacement);
        }
        pulse.rescheduled=true;
    }
    ports_.frameEvents.cancel(event.owner,FrameEventType::PeriodicSkills,event.customId);
    if(pulse.next.handle()) ports_.frameEvents.publish(std::move(pulse.next));stormPulses_.erase(event.handle);
    return {DomainStatus::Applied,std::monostate{}};
}
}
