#include "system.hpp"
#include "gameplay/skills/mana_payment.hpp"
#include "attribute_projection.hpp"
#include "gameplay/skills/targeting.hpp"
#include "gameplay/combat/geometry.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/social/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "core/random.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/event_frame.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/skills/aura_resolve.hpp"
#include "gameplay/effects/queries.hpp"
#include <algorithm>
#include <string>
#include <string_view>
#include <span>
#include <set>
#include <stdexcept>
#include <optional>
#include <cmath>
#include <exception>
#include <limits>
namespace d2x::server::effects {
void System::stageAuraActors(AuraUnitPublication &plan,std::map<EntityId,monsters::Actor> actors,size_t allocatedIds,std::span<const EntityId> retired) {
    if(plan.actors) throw std::invalid_argument("Duplicate staged aura actors");
    plan.actors=std::make_shared<monsters::ActorPublication>(ports_.monsters.prepareActorPublication(std::move(actors),allocatedIds,{}, {},retired));
}
void System::stageSummonEvents(AuraUnitPublication &plan,EntityId id,int skill,int rank,const SkillFormulaCatalog &catalog,uint64_t) const {
    const auto definition=catalog.skills.find(skill);
    if(definition==catalog.skills.end() || definition->second.unitEvents.front().event<0) return;
    plan.skillEvents.push_back({id,definition->second.suppressionState,{CombatEffectSource::Skill,id,skill,rank},definition->second.unitEvents});
}
void System::prepareStagedSkillEvents(AuraUnitPublication &plan,uint64_t tick) const {
    for(const auto &registration:plan.skillEvents) {
        const auto id=registration.unit;UnitEffect unit;
        if(const auto prepared=plan.units.find(id);prepared!=plan.units.end()) unit=prepared->second;
        else if(const auto live=units_.find(id);live!=units_.end()) unit=live->second;
        const auto *actor=ports_.monsters.find(id);
        if(plan.actors) if(const auto staged=plan.actors->actors.find(id);staged!=plan.actors->actors.end()) actor=&staged->second;
        if(!actor) throw std::invalid_argument("Missing native summon event recipient");
        unit.area=actor->area;unit.states.registerUnitEvents(registration.state,registration.source,registration.bindings);
        if(plan.units.contains(id)) plan.units.at(id)=std::move(unit);
        else {
            prepareUnitEffects(plan,id,unit,{},tick);
            if(plan.life.contains(id)) {
                plan.velocity.insert_or_assign(id,unit.states.modifiers(tick).velocityPercent);
                if(std::find(plan.lifeOrder.begin(),plan.lifeOrder.end(),id)==plan.lifeOrder.end()) plan.lifeOrder.push_back(id);
            } else plan.eventsOnly.insert(id);
        }
    }
    plan.skillEvents.clear();
    prepareUnitTimers(plan);
}
void System::stageAuraSource(AuraUnitPublication &plan,EntityId id,AuraUnitPublication::SourcePreparation source) {
    if(!source || !plan.actors || !plan.actors->actors.contains(id) || !plan.sources.emplace(id,std::move(source)).second)
        throw std::invalid_argument("Invalid staged unit source preparation");
}
void System::prepareStagedItemAuras(AuraUnitPublication &plan,uint64_t tick,const AuraRecipientPreparation &recipient) {
    if(!plan.actors) return;
    for(auto &[id,candidate]:plan.actors->actors) {
        const auto source=plan.sources.find(id);
        if(source==plan.sources.end()) {
            prepareUnitItemAuras(plan,candidate,tick,recipient);
            // Inherited summon aura slots are selected at birth, before AI.
            if(candidate.druidPet && !ports_.monsters.find(id) && candidate.rule.auraPrograms)
                for(const auto skill:candidate.nativeSkillOrder)
                    if(candidate.rule.auraPrograms->contains(skill))
                        prepareUnitAuraSelection(plan,id,skill,tick,recipient);
            continue;
        }
        candidate.preparedItemAuras.clear();
        CombatEffectSet states;
        if(const auto prepared=plan.units.find(id);prepared!=plan.units.end()) states=prepared->second.states;
        else if(const auto live=units_.find(id);live!=units_.end()) states=live->second.states;
        source->second(candidate,states,[&](monsters::Actor &actor,MonsterResourceTrace &resources,const SkillAuraChange &change) {
            struct ActiveSourceScope {
                std::map<EntityId,MonsterResourceTrace *> &sources;EntityId actor;MonsterResourceTrace *previous{};
                ~ActiveSourceScope() {if(previous) sources.at(actor)=previous;else sources.erase(actor);}
            } scope{plan.activeSources,actor.id};
            if(const auto previous=plan.activeSources.find(actor.id);previous!=plan.activeSources.end()) scope.previous=previous->second;
            plan.activeSources[actor.id]=&resources;
            const auto sourceId=actor.id;
            if(const auto prepared=plan.units.find(sourceId);prepared!=plan.units.end()) {
                resources.inheritPassiveCache(prepared->second.states.passiveRefreshEpoch());resources.inheritStateCallbacks(prepared->second.states);
            } else if(const auto live=units_.find(sourceId);live!=units_.end()) {
                resources.inheritPassiveCache(live->second.states.passiveRefreshEpoch());resources.inheritStateCallbacks(live->second.states);
            } else resources.inheritStateCallbacks(CombatEffectSet{});
            resources.readStateSources([&,sourceId] {
                std::vector<ModifierStatSource> states;
                if(const auto prepared=plan.units.find(sourceId);prepared!=plan.units.end())
                    states=combatEffectStatSources(prepared->second.states.entries(),tick,true);
                else if(const auto live=units_.find(sourceId);live!=units_.end())
                    states=combatEffectStatSources(live->second.states.entries(),tick,true);
                return states;
            });
            auto life=resources.read();
            if(const auto previous=plan.life.find(actor.id);previous!=plan.life.end()) retainPreparedUnitControl(life,previous->second);
            plan.life.insert_or_assign(actor.id,std::move(life));
            prepareUnitItemAura(plan,actor,change,tick,recipient);
            actor.nativeStats=resources.read().intrinsic;actor.life=resources.read().life.raw();actor.maximumLife=resources.read().maximum.raw();
            if(resources.read().poison) actor.poison=*resources.read().poison;
            if(resources.read().shatter) actor.shatterActive=*resources.read().shatter;
            if(const auto life=plan.life.find(actor.id);life!=plan.life.end() && life->second.curseControl) actor.curseControl=*life->second.curseControl;
        });
        plan.sources.erase(source);
        if(const auto life=plan.life.find(id);life!=plan.life.end()) {
            life->second.expectedRevision=candidate.revision;life->second.life=Fixed8::fromRaw(nativeChecked(candidate.life));
            life->second.maximum=Fixed8::fromRaw(nativeChecked(candidate.maximumLife));life->second.intrinsic=candidate.nativeStats;
            life->second.itemAuraTimers=candidate.itemAuraTimers;life->second.changed=true;
        }
    }
    prepareStagedSkillEvents(plan,tick);
}
bool System::validateAuraUnits(const AuraUnitPublication &plan) const {
    if(plan.actors) for(const auto &[id,revision]:plan.actors->removals) {
        (void)revision;
        if(plan.units.contains(id) || plan.life.contains(id) || plan.velocity.contains(id)) return false;
    }
    if(plan.timers.size()!=plan.units.size()) return false;
    for(const auto &[id,timers]:plan.timers) if(!plan.units.contains(id) || !ports_.frameEvents.validate(timers)) return false;
    for(const auto &[id,timers]:plan.resetTimers) if(!plan.life.contains(id) || !plan.life.at(id).aiResetTimers || !ports_.frameEvents.validate(timers)) return false;
    for(const auto &[id,timers]:plan.skillTimers) if(timers.owner!=FrameEventOwner{FrameEventUnit::Monster,id} || !ports_.frameEvents.validate(timers)) return false;
    for(const auto &[id,timers]:plan.nativeSkillTimers) if(timers.owner!=FrameEventOwner{FrameEventUnit::Monster,id} || !ports_.frameEvents.validate(timers)) return false;
    for(const auto &[id,timers]:plan.regenerationTimers) if(!plan.life.contains(id) || !ports_.frameEvents.validate(timers)) return false;
    for(const auto &[id,life]:plan.life) if(life.periodicEvents &&
        (life.periodicEvents->edit.owner!=FrameEventOwner{FrameEventUnit::Monster,id} || !validatePeriodicEvents(*life.periodicEvents))) return false;
    size_t registrations=0;
    for(const auto &[id,life]:plan.life) if(life.specialTargetNode.value_or(false)) {
        const auto *unit=ports_.monsters.find(id);
        if(unit && !unit->owner && !unit->specialTargetOrder) ++registrations;
    }
    if(!ports_.monsters.canRegisterSpecialTargets(registrations)) return false;
    if(!plan.activeSources.empty() || !plan.sources.empty() || !plan.skillEvents.empty()) return false;
    if(plan.actors && !ports_.monsters.validateActors(*plan.actors)) return false;
    for(const auto &[id,unit]:plan.units) {
        const auto guard=plan.recipients.find(id);const auto *recipientArea=ports_.areas.find(unit.area);
        if(guard==plan.recipients.end() || !recipientArea || recipientArea->generation!=guard->second.areaGeneration) return false;
        const auto *actor=ports_.monsters.find(id);const auto life=plan.life.find(id);
        const monsters::Actor *staged=nullptr;
        if(plan.actors) {
            const auto candidate=plan.actors->actors.find(id);
            if(candidate!=plan.actors->actors.end()) staged=&candidate->second;
        }
        if(plan.eventsOnly.contains(id) && life==plan.life.end()) {
            if(!actor && !staged) return false;
            if((staged?staged->area:actor->area)!=unit.area) return false;
        } else if(staged) {
            if(staged->area!=unit.area || life==plan.life.end() || life->second.expectedRevision!=staged->revision ||
               life->second.maximum.raw()<0 || life->second.life.raw()<0 || life->second.life>life->second.maximum || !life->second.intrinsic) return false;
            const uint64_t increments=uint64_t(life->second.changed)+uint64_t(plan.velocity.at(id)!=staged->effectVelocity);
            if(staged->revision>UINT64_MAX-increments) return false;
            if((life->second.curseControl || life->second.reselectAi) && staged->aiFunctionRevision==UINT64_MAX) return false;
            if((life->second.tauntPostActions || life->second.restoreNeutral || life->second.aiThinkAt) && staged->aiThinkRevision==UINT64_MAX) return false;
            const bool alignmentChanged=staged->rule.stats && life->second.intrinsic->values.get({staged->rule.stats->names.at("alignment"),0})!=int(staged->alignment());
            if((alignmentChanged || life->second.curseControl || life->second.attraction || life->second.detachedMinion) && staged->allegianceRevision==UINT64_MAX) return false;
        } else if(actor) {
            if(actor->area!=unit.area || life==plan.life.end() || !ports_.monsters.canCommitLife(life->second)) return false;
            const uint64_t increments=uint64_t(life->second.changed)+uint64_t(plan.velocity.at(id)!=actor->effectVelocity);
            if(actor->revision>UINT64_MAX-increments) return false;
        } else return false;
        const auto current=units_.find(id);
        if(plan.expectedAbsent.contains(id)) {if(current!=units_.end()) return false;}
        else if(current==units_.end() || !current->second.states.sharesSnapshot(plan.expectedEffects.at(id))) return false;
    }
    return true;
}
void System::prepareUnitEffects(AuraUnitPublication &plan,EntityId id,const UnitEffect &unit,std::optional<monsters::LifeChange> life,uint64_t tick) const {
    plan.tick=tick;
    if((life && life->actor!=id) || plan.units.contains(id)) throw std::invalid_argument("Duplicate prepared aura unit");
    const monsters::Actor *actor=ports_.monsters.find(id);
    if(plan.actors) if(const auto staged=plan.actors->actors.find(id);staged!=plan.actors->actors.end()) actor=&staged->second;
    if(life && !actor) throw std::invalid_argument("Missing prepared control unit");
    if(const auto current=units_.find(id);current!=units_.end()) {
        if(actor && !actor->alive()) for(const auto &effect:current->second.states.entries())
            if(effect.spec.nativeRemoval==NativeStateRemoval::Pregnant && !unit.states.find(effect.handle))
                plan.removals.push_back({id,effect.spec.source.definition,effect.spec.source.level});
        plan.expectedEffects.emplace(id,current->second.states);
        if(life) prepareUnitControl(*actor,current->second.states,unit.states,*life,tick);
    } else {
        plan.expectedAbsent.insert(id);
        if(life) prepareUnitControl(*actor,CombatEffectSet{},unit.states,*life,tick);
    }
    plan.units.emplace(id,unit);
    if(life) {plan.lifeOrder.push_back(id);plan.life.emplace(id,std::move(*life));plan.velocity.emplace(id,unit.states.modifiers(tick).velocityPercent);}
    prepareUnitTimers(plan);
}
void System::publishAuraUnits(AuraUnitPublication &plan) noexcept {
    if(plan.timers.size()!=plan.units.size()) std::terminate();
    for(auto &[id,timers]:plan.timers) {(void)id;ports_.frameEvents.publish(std::move(timers));}
    for(auto &[id,timers]:plan.resetTimers) {(void)id;ports_.frameEvents.publish(std::move(timers));}
    for(auto &[id,timers]:plan.skillTimers) {(void)id;ports_.frameEvents.publish(std::move(timers));}
    for(auto &[id,timers]:plan.nativeSkillTimers) {(void)id;ports_.frameEvents.publish(std::move(timers));}
    for(auto &[id,timers]:plan.regenerationTimers) {(void)id;ports_.frameEvents.publish(std::move(timers));}
    const auto publishLife=[&](EntityId id,monsters::LifeChange &life) {
        auto *staged=plan.actors && plan.actors->actors.contains(id)?&plan.actors->actors.at(id):nullptr;
        if(staged) {
            if(life.changed) {
                const auto alignment=staged->alignment();
                staged->life=life.life.raw();staged->maximumLife=life.maximum.raw();staged->nativeStats=std::move(life.intrinsic);++staged->revision;
                if(life.cureColdPoison) {staged->poison.reset();staged->chilledUntil=staged->frozenUntil=0;staged->shatterActive=false;}
                if(life.poison) staged->poison=*life.poison;
                if(life.wound) staged->wound=*life.wound;
                if(life.chilledUntil) staged->chilledUntil=*life.chilledUntil;
                if(life.frozenUntil) staged->frozenUntil=*life.frozenUntil;
                if(life.stunnedUntil) staged->stunnedUntil=*life.stunnedUntil;
                if(life.restoreNeutral) {
                    staged->route.clear();staged->moving=staged->running=false;staged->busyUntil=staged->reactionUntil=0;staged->neutralAt.reset();
                }
                if(life.shatter) staged->shatterActive=*life.shatter;
                if(life.itemAuraTimers) staged->itemAuraTimers=std::move(*life.itemAuraTimers);
                if(life.periodicEvents) publishPeriodicEvents(*life.periodicEvents);
                if(life.curseControl) {
                    if(life.reselectAi || (staged->curseControl.ai!=life.curseControl->ai && staged->curseControl.ai!=CurseAi::Confuse && life.curseControl->ai!=CurseAi::Confuse && staged->curseControl.ai!=CurseAi::Attract && life.curseControl->ai!=CurseAi::Attract)) {
                        ++staged->aiFunctionRevision;staged->aiThinkAt=0;
                    }
                    staged->curseControl=*life.curseControl;
                }
                if(life.tauntPostActions) staged->mainOwnerCleared=true;
                if(life.restoreNeutral || life.tauntPostActions || life.aiThinkAt) {++staged->aiThinkRevision;staged->aiThinkAt=life.aiThinkAt.value_or(0);}
                if(life.tauntTarget) staged->movementTarget=staged->tauntTarget=*life.tauntTarget;
                if(life.detachedMinion) {
                    staged->detachedMinion=*life.detachedMinion;
                    if(*life.detachedMinion) staged->nativeMinionOwner=EntityId{};
                }
                if(life.nativeMinions) staged->nativeMinions=std::move(*life.nativeMinions);
                if(life.specialTargetNode && !*life.specialTargetNode) staged->specialTargetOrder=0;
                if(life.stopMovement) {staged->route.clear();staged->moving=false;staged->running=false;}
                if(life.hitDelay) staged->nextHitTick=std::max(staged->nextHitTick,*life.hitDelay);
                if(life.attraction) {staged->attractionTarget=life.attraction->target;staged->attractionUntil=life.attraction->until;}
                if(life.aiResetTimers) {staged->aiResetTimers=std::move(*life.aiResetTimers);staged->nativeAiResetTracked=true;}
                if(alignment!=staged->alignment() || life.curseControl || life.attraction || life.detachedMinion) ++staged->allegianceRevision;
            }
            if(staged->effectVelocity!=plan.velocity.at(id)) {staged->effectVelocity=plan.velocity.at(id);++staged->revision;}
        } else if(!ports_.monsters.commitLife(std::move(life))) std::terminate();
    };
    for(const auto id:plan.lifeOrder) publishLife(id,plan.life.at(id));
    for(auto &[id,life]:plan.life) if(std::find(plan.lifeOrder.begin(),plan.lifeOrder.end(),id)==plan.lifeOrder.end()) publishLife(id,life);
    if(plan.actors) {
        for(const auto &[id,candidate]:plan.actors->actors) plan.velocity.erase(id);
        ports_.monsters.publishActors(*plan.actors);
    }
    while(!plan.units.empty()) {
        auto node=plan.units.extract(plan.units.begin());const auto current=units_.find(node.key());
        if(const auto *actor=ports_.monsters.find(node.key());actor && !actor->alive()) {
            const FrameEventOwner owner{FrameEventUnit::Monster,node.key()};
            ports_.frameEvents.cancel(owner,FrameEventType::PeriodicSkills);
            ports_.frameEvents.cancel(owner,FrameEventType::PeriodicStats);
            ports_.frameEvents.cancel(owner,FrameEventType::StatRegeneration);
        }
        if(current==units_.end()) units_.insert(std::move(node));else current->second=std::move(node.mapped());
    }
    for(const auto &[id,velocity]:plan.velocity) ports_.monsters.velocityModifier(id,velocity);
    nativeStateRemovals_.splice(nativeStateRemovals_.end(),plan.removals);
}
} // namespace d2x::server::effects
