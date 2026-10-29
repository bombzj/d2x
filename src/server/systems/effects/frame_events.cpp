#include "system.hpp"
#include "server/systems/actions/state.hpp"
#include "attribute_projection.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/skills/aura_resolve.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/effects/queries.hpp"
#include "gameplay/rules/event_frame.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace d2x::server::effects {
void System::scheduleModeEnd(EntityId id,uint64_t tick) {
    const auto *states=unitEffects(id);if(!states || !states->hasEndedModeEffects({})) return;
    const FrameEventOwner owner{ports_.players.findActor(id)?FrameEventUnit::Player:FrameEventUnit::Monster,id};
    auto timer=ports_.frameEvents.prepareInsertion({{},owner,FrameEventType::EndAnimation,tick+1,false,modeEndTimerHandler});
    ports_.frameEvents.cancelHandler(owner,FrameEventType::EndAnimation,modeEndTimerHandler);
    ports_.frameEvents.publish(std::move(timer));
}
DomainResult<> System::modeEndEvent(EntityId id,uint64_t tick) {
    std::optional<uint64_t> current;
    if(const auto cast=ports_.actions.casts.find(id);cast!=ports_.actions.casts.end() && !cast->second.interrupted && cast->second.until>tick)
        current=cast->second.revision;
    else if(const auto *unit=ports_.monsters.find(id);unit && unit->alive() && unit->nativeActionUntil>tick && unit->nativeActionInterruption==unit->interruption)
        current=unit->nativeActionRevision;
    return endMode(id,tick,current);
}
std::shared_ptr<FrameEventPublication> System::prepareItemAuraEvents(FrameEventOwner owner,std::span<const ItemAuraTimer> timers) const {
    const auto existing=ports_.frameEvents.events(owner);
    if(timers.empty() && std::none_of(existing.begin(),existing.end(),[](const auto &event){return event.type==FrameEventType::PeriodicStats;})) return {};
    std::vector<FrameEvent> events;events.reserve(timers.size());
    for(auto it=timers.rbegin();it!=timers.rend();++it) {
        if(!it->order || it->source.value>UINT32_MAX) throw std::invalid_argument("Invalid original item aura timer identity");
        events.push_back({{it->order},{},FrameEventType::PeriodicStats,it->next,it->continuous,0,
            nativeNarrow(uint32_t(it->source.value)),it->skill});
    }
    auto publication=std::make_shared<FrameEventPublication>();
    publication->edit=ports_.frameEvents.prepareEdit(owner,FrameEventType::PeriodicStats,events);
    return publication;
}
bool System::validatePeriodicEvents(const FrameEventPublication &plan) const {return ports_.frameEvents.validate(plan.edit);}
void System::publishPeriodicEvents(FrameEventPublication &plan) noexcept {ports_.frameEvents.publish(std::move(plan.edit));}
void System::cancelPeriodicSkills(FrameEventOwner owner) noexcept {
    ports_.frameEvents.cancel(owner,FrameEventType::PeriodicSkills);
    // FrameEventQueue retains an executing callback even when cancelled.
    // Its private pulse must also survive until that callback finishes.
}
void System::prepareUnitTimers(AuraUnitPublication &plan) const {
    plan.timers.clear();plan.resetTimers.clear();plan.regenerationTimers.clear();plan.nativeSkillTimers.clear();
    for(const auto &[id,unit]:plan.units) {
        const auto *area=ports_.areas.find(unit.area);
        if(!area) throw std::invalid_argument("Missing effect recipient area");
        if(!plan.recipients.contains(id)) {
            AuraUnitPublication::RecipientGuard guard{area->generation};
            const bool staged=plan.actors && plan.actors->actors.contains(id);
            if(!ports_.monsters.find(id) && !staged) throw std::invalid_argument("Missing effect recipient identity");
            plan.recipients.emplace(id,guard);
        }
        plan.timers.emplace(id,ports_.frameEvents.prepareEdit({FrameEventUnit::Monster,id},FrameEventType::RemoveState,unit.states.expirationEvents()));
        const auto *body=ports_.monsters.find(id);
        if(plan.actors) if(const auto found=plan.actors->actors.find(id);found!=plan.actors->actors.end()) body=&found->second;
        if(body && body->rule.formulas && body->nativeStats) {
            auto sampled=*body;
            if(const auto changed=plan.life.find(id);changed!=plan.life.end()) {
                sampled.nativeStats=changed->second.intrinsic;sampled.life=changed->second.life.raw();sampled.maximumLife=changed->second.maximum.raw();
            }
            const FrameEventOwner owner{FrameEventUnit::Monster,id};const auto existing=ports_.frameEvents.events(owner);
            std::vector<FrameEvent> timers;
            if(sampled.alive()) for(const auto &effect:unit.states.entries()) {
                if(effect.spec.source.kind!=CombatEffectSource::Skill || !unit.states.hasState(effect.spec.state.id,plan.tick)) continue;
                const auto record=sampled.rule.formulas->skills.find(effect.spec.source.definition);
                if(record==sampled.rule.formulas->skills.end() || record->second.number("srvdofunc")!=54) continue;
                const auto old=std::find_if(existing.begin(),existing.end(),[&](const auto &event) {
                    return event.type==FrameEventType::PeriodicSkills && event.handler==nativeUnitSkillTimerHandler && event.customId==record->first;
                });
                const auto previous=units_.find(id);const auto *oldEffect=previous==units_.end()?nullptr:previous->second.states.find(effect.handle);
                if(old!=existing.end() && oldEffect && oldEffect->revision==effect.revision && oldEffect->spec.source==effect.spec.source) {
                    auto event=*old;event.handle={event.payload};timers.push_back(event);continue;
                }
                auto input=skills::formulaInput(sampled,sampled.nativeStats->values);SkillFormulaValues values(*sampled.rule.formulas,input);
                const auto period=values.calculation(FormulaDomain::Skill,record->first,effect.spec.source.level,"perdelay");
                if(!period) throw std::logic_error("Missing native unit periodic skill period");
                timers.push_back({{ports_.frameEvents.reserveIdentity()},owner,FrameEventType::PeriodicSkills,skillPeriodicTick(plan.tick,*period),
                    skillPeriodicFrame(nativeNarrow(uint32_t(plan.tick)),*period)==-1,nativeUnitSkillTimerHandler,record->first,effect.spec.source.level});
            }
            plan.nativeSkillTimers.emplace(id,ports_.frameEvents.prepareEdit(owner,FrameEventType::PeriodicSkills,timers,nativeUnitSkillTimerHandler));
        }
    }
    for(auto &[id,life]:plan.life) if(life.aiResetTimers) {
        std::vector<FrameEvent> events;events.reserve(life.aiResetTimers->size());
        // AiResetTimer keeps creation order; native owner lists are newest
        // first, so the generic edit receives the reversed local list.
        for(auto it=life.aiResetTimers->rbegin();it!=life.aiResetTimers->rend();++it) {
            if(!it->identity) it->identity=ports_.frameEvents.reserveIdentity();
            events.push_back({{it->identity},{},FrameEventType::AiReset,it->due,it->continuous});
        }
        plan.resetTimers.emplace(id,ports_.frameEvents.prepareEdit({FrameEventUnit::Monster,id},FrameEventType::AiReset,events));
    }
    for(auto &[id,life]:plan.life) if(life.itemAuraTimers)
        life.periodicEvents=prepareItemAuraEvents({FrameEventUnit::Monster,id},*life.itemAuraTimers);
    for(const auto &[id,life]:plan.life) {
        const auto *actor=ports_.monsters.find(id);
        if(plan.actors) if(const auto candidate=plan.actors->actors.find(id);candidate!=plan.actors->actors.end()) actor=&candidate->second;
        if(!actor || actor->deathMode || life.life<=0 || !life.intrinsic || !life.intrinsic->sources || !actor->rule.stats) continue;
        const auto &rule=*actor->rule.stats;
        auto rate=nativeAdd(nativeStatValue(life.intrinsic->values,rule,"hpregen"),nativeChecked(actor->aiRegenerationBonus));
        const auto unit=plan.units.find(id);
        if(unit!=plan.units.end() && hasCombatLifeState(unit->second.states.entries(),plan.tick))
            rate=nativeSubtract(rate,nativeAdd(baseStatValue(rule,*life.intrinsic->sources,{rule.names.at("hpregen"),0}),nativeChecked(actor->aiRegenerationBonus)));
        const FrameEventOwner owner{FrameEventUnit::Monster,id};
        if(plan.resetRegeneration.contains(id) || (rate && (rate<0 || life.life<life.maximum) && !ports_.frameEvents.hasEvent(owner,FrameEventType::StatRegeneration))) {
            const auto deadline=nativeAdd(nativeNarrow(uint32_t(plan.tick)),1);
            const auto due=nativeEventDeadline(plan.tick,deadline);
            const std::vector<FrameEvent> events{FrameEvent{{},owner,FrameEventType::StatRegeneration,due.due,due.continuous}};
            plan.regenerationTimers.emplace(id,ports_.frameEvents.prepareEdit(owner,FrameEventType::StatRegeneration,events));
        }
    }
}
DomainResult<> System::frameEvent(const FrameEvent &event,uint64_t tick) {
    if(event.type==FrameEventType::PeriodicSkills && event.owner.type==FrameEventUnit::Monster && event.handler==nativeUnitSkillTimerHandler)
        return bladeShieldEvent(event,tick);
    if(event.type==FrameEventType::StatRegeneration && event.owner.type==FrameEventUnit::Player) return regeneratePlayer(event.owner.unit,tick);
    if(event.type==FrameEventType::PeriodicStats && !event.handler) return itemAuraEvent(event,tick);
    if(event.type==FrameEventType::PeriodicSkills && !event.handler && event.customId==-1)
        return event.owner.type==FrameEventUnit::Monster?unitAuraEvent(event,tick):rightAuraEvent(event,tick);
    if(event.type==FrameEventType::PeriodicSkills && !event.handler && event.customId>0)
        return event.owner.type==FrameEventUnit::Monster?unitAuraEvent(event,tick):stormEvent(event,tick);
    if(event.type!=FrameEventType::RemoveState || event.handler) return {DomainStatus::NotImplemented,{}};
    if(event.owner.type==FrameEventUnit::Player) {
        const auto *player=ports_.players.findActor(event.owner.unit);
        if(!player || !player->entered) {ports_.frameEvents.cancelOwner(event.owner);return {DomainStatus::InvalidActor,{}};}
        const auto current=state_.players.find(event.owner.unit);
        if(current==state_.players.end()) return {DomainStatus::Applied,std::monostate{}};
        auto states=current->second.states;
        PlayerResourceTrace resources(*player,tick);
        const auto removed=states.expireEvent({event.payload},tick,[&]{resources.sample(states,true);},
            player->persistent.player.hp<=0?std::optional{EffectUnitKind::Player}:std::nullopt);
        const auto &area=ports_.areas.at(player->area);
        transactions::CharacterEdit edit{{player->player,player->actor,player->area,area.generation,0,tick},
            player->inventoryRevision,player->characterRevision,resources.record()};
        restoreStaminaOnEffectRemoval(edit.player.stamina,resources.maximum().stamina,removed);
        edit.resourceBase=resources.maximum();edit.petLimitChanges=resources.petLimits();edit.transient=projectEffectAttributes(states,tick);
        for(const auto &entry:removed) if(entry.effect.spec.state.id>=0 && !states.hasState(entry.effect.spec.state.id,tick) &&
            (!entry.effect.spec.nativeMissileHitGate || !entry.effect.spec.state.noSend))
            edit.publicFacts.emplace_back(StateFact{player->actor,0,player->area,entry.effect.spec.state.id,false});
        auto prepared=ports_.transactions.prepare(std::move(edit));
        return prepared?ports_.transactions.commit(std::move(*prepared.value)):DomainResult<>{prepared.status,{}};
    }
    if(event.owner.type!=FrameEventUnit::Monster) return {DomainStatus::NotImplemented,{}};
    const auto current=units_.find(event.owner.unit);
    if(current==units_.end()) {ports_.frameEvents.cancel(event.owner,FrameEventType::RemoveState);return {DomainStatus::Applied,std::monostate{}};}
    const auto *monster=ports_.monsters.find(event.owner.unit);
    if(!monster) {ports_.frameEvents.cancelOwner(event.owner);units_.erase(current);return {DomainStatus::Applied,std::monostate{}};}
    auto unit=current->second;if(monster) unit.area=monster->area;
    std::optional<MonsterResourceTrace> resources;
    if(monster && monster->nativeStats && monster->nativeStats->sources && monster->rule.stats) resources.emplace(*monster,tick,&unit.states);
    const auto removed=unit.states.expireEvent({event.payload},tick,[&]{if(resources) resources->sample(unit.states,true);},
        monster && !monster->alive()?std::optional{monster->rule.boss?EffectUnitKind::Boss:EffectUnitKind::Monster}:std::nullopt);
    if(resources) {
        if(monster->alive() && std::any_of(removed.begin(),removed.end(),[](const auto &entry){return entry.effect.spec.nativeRemoval==NativeStateRemoval::HolyFreeze;})) resources->seedShatter(false);
        resources->sample(unit.states);
    }
    std::optional<monsters::LifeChange> life;
    if(resources) life=resources->take();
    else if(monster) life=monsters::LifeChange{monster->id,monster->revision,Fixed8::fromRaw(monster->life),Fixed8::fromRaw(monster->maximumLife),monster->nativeStats,false};
    AuraUnitPublication plan;
    for(const auto &entry:removed) if(entry.effect.spec.state.id>=0 && !unit.states.hasState(entry.effect.spec.state.id,tick)) {
        unit.nativeStats.erase(entry.effect.spec.state.id);
        if(!entry.effect.spec.nativeMissileHitGate || !entry.effect.spec.state.noSend)
            plan.facts.emplace_back(StateFact{event.owner.unit,1,unit.area,entry.effect.spec.state.id,false});
    }
    prepareUnitEffects(plan,event.owner.unit,unit,std::move(life),tick);
    if(!validateAuraUnits(plan)) return {DomainStatus::Conflict,{}};
    if(!plan.facts.empty()) {
        const auto result=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},unit.area},std::move(plan.facts)});
        if(!result) return {result.status,{}};
    }
    publishAuraUnits(plan);return {DomainStatus::Applied,std::monostate{}};
}
}
