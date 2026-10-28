#include "item_triggers.hpp"
#include "item_trigger_runtime.hpp"
#include "server/runtime/events.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/skills/stat_callbacks.hpp"
#include "core/random.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "gameplay/skills/pd2_program.hpp"
#include "gameplay/skills/behavior.hpp"
#include "pd2/item_events.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace d2x::server::reactions {
ItemEventPlan::ItemEventPlan() = default;
ItemEventPlan::~ItemEventPlan() = default;
ItemEventPlan::ItemEventPlan(ItemEventPlan &&) noexcept = default;
ItemEventPlan &ItemEventPlan::operator=(ItemEventPlan &&) noexcept = default;
ItemTriggers::ItemTriggers(ItemTriggerPorts ports) : ports_(ports) {}
ItemTriggers::~ItemTriggers() = default;
size_t ItemTriggers::pending() const { return pending_.size(); }
bool ItemTriggers::involves(EntityId id) const {
    const auto matches=[&](const auto &job) {return job.actor.actor==id || job.target==id || (job.effectActor && job.effectActor->actor==id);};
    if(std::any_of(pending_.begin(),pending_.end(),matches)) return true;
    for(const auto &[handle,plan]:scheduled_) {
        (void)handle;
        if(std::any_of(plan.jobs.begin(),plan.jobs.end(),matches)) return true;
    }
    return false;
}
void ItemTriggers::append(ItemEventPlan &target,ItemEventPlan source) const noexcept {target.jobs.splice(target.jobs.end(),source.jobs);}
void ItemTriggers::commit(ItemEventPlan plan, bool terminal) noexcept {
    if (!terminal) std::erase_if(plan.jobs, [](const auto &job) {
        return job.event == ItemSkillEvent::Death || job.event == ItemSkillEvent::Kill;
    });
    pending_.splice(pending_.end(), plan.jobs);
}
DomainResult<ItemEventPlan> ItemTriggers::prepare(const ActorContext &actor,std::initializer_list<ItemSkillEvent> events,EntityId target,Vec position,const StatSnapshot *weaponStats) const {
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || caster->area()!=actor.area || !caster->programs() ||
        (caster->player && (!caster->player->entered || caster->player->player!=actor.player))) return {DomainStatus::InvalidActor,{}};
    const auto *player=caster->player;const auto &programs=*caster->programs();
    if(!programs.stats) throw std::runtime_error("Missing native item event stat rules");
    const auto &stats=*programs.stats;
    const auto sampled=caster->monster?ports_.effects.unitStatValues(actor.actor,actor.tick):std::optional<StatSnapshot>{};
    if(caster->monster && !sampled) return {DomainStatus::Unavailable,{}};
    const auto &values=weaponStats?*weaponStats:player?player->totals.nativeStats:*sampled;
    const auto *registrations=player?&player->totals.itemEvents:&caster->monster->itemStatEvents;
    std::vector<ItemStatEvent> weaponRegistrations;
    if(programs.pd2 && weaponStats && std::find(events.begin(),events.end(),ItemSkillEvent::MeleeSplash)!=events.end()) {
        // Weapon damage-related stats are deliberately absent from the unit's
        // ordinary registration list. Fill/Drain samples this selected hand.
        weaponRegistrations=*registrations;
        const auto splash=stats.names.at("item_splashonhit");
        for(const auto &[key,value]:weaponStats->values) if(key.id==splash)
            refreshItemStatEvents(weaponRegistrations,stats,key,value);
        registrations=&weaponRegistrations;
    }
    ItemEventPlan plan;
    // Trigger phases follow the caller's order. Within one phase use the
    // native newest-first subscription list, never stat/layer map ordering.
    for(const auto requested:events) {
      std::set<StatKey> visited;
      for(const auto &registration:*registrations) {
        const auto key=registration.key;
        const auto &definition=stats.definitions.at(key.id);
        const auto event=itemSkillEvent(definition.name);
        const int function=requested==ItemSkillEvent::HitHeal?28:requested==ItemSkillEvent::GetHit?21:requested==ItemSkillEvent::Cast?33:
            requested==ItemSkillEvent::Death || requested==ItemSkillEvent::LevelUp || requested==ItemSkillEvent::Block ||
            requested==ItemSkillEvent::Critical || requested==ItemSkillEvent::Pierce?30:20;
        if(!event || *event!=requested || registration.binding.function!=function || !visited.insert(key).second) continue;
        // Melee/missile bindings of the same stat are alternatives in this
        // existing phase API; they must not cast the same layer twice.
        const auto chance=nativeStatValue(values,stats,definition.name,key.layer);
        if(requested==ItemSkillEvent::HitHeal?!chance:chance<=0) continue;
        ItemTrigger job;job.actor=actor;job.actor.areaGeneration=ports_.areas.at(actor.area).generation;job.event=requested;job.target=target;job.position=position;job.chance=chance;
        if(requested==ItemSkillEvent::HitHeal) {job.healStat=key;plan.jobs.push_back(std::move(job));continue;}
        if(requested!=ItemSkillEvent::MeleeSplash) job.chanceStat=key;
        job.targetType=ports_.players.findActor(target)?0:1;
        if(requested==ItemSkillEvent::Cast && !target) job.position=caster->position();
        const auto source=decodeItemSkillLayer(key.layer);
        const int id=source.skill,rank=source.rank;
        if(programs.definitions.contains(id)) {
            const auto &definition=programs.definitions.at(id);job.skill.sourceId=id;job.skill.rank=rank;job.collision=definition.collision;
            const int start=programs.formulas->skills.at(id).number("srvstfunc");
            // A null native St is already prepared, including Raven's Druid
            // program. It does not require a Pd2SkillProgram wrapper.
            const bool preparedStart=!start || start==14 || start==19;
            job.policy=resolveItemTriggerPolicy(job.event,{definition.itemEffect,definition.itemTarget,definition.itemTargetDo,definition.itemCheckStart,definition.itemEffectUsesPreparedProgram,programs.pd2,preparedStart});
            if(definition.itemCheckStart) job.startCheck=start;
            if(!programs.pd2 && job.policy.alternativeProgram)
                job.alternativeEffect=definition.itemEffect;
            job.supported=true;
        } else {job.skill.sourceId=id;job.skill.rank=rank;}
        plan.jobs.push_back(std::move(job));
      }
    }
    // Native synchronous contacts cannot wait for a later domain phase to
    // drain an arbitrary item-job count. Admission allocates the actual jobs;
    // reliable output capacity remains a separate publication boundary.
    return {DomainStatus::Applied,std::move(plan)};
}
StepStatus ItemTriggers::advance(uint64_t tick) {
    const auto result=advanceJobs(pending_,tick);
    // Effects advances this phase once per completed frame. Do not schedule
    // again while an earlier item job is waiting on reliable-output capacity.
    if(result==StepStatus::Complete) schedulePd2StreamItemEvents(ports_,tick);
    return result;
}
StepStatus ItemTriggers::advance(ItemEventPlan &plan,uint64_t tick) {
    return advanceJobs(plan.jobs,tick);
}
StepStatus ItemTriggers::advanceContact(ItemEventPlan &plan,uint64_t tick,bool beforeDamage,bool terminal) {
    if(!beforeDamage) {
        // ExecuteEvents sends Killed before Kill, even though the attacker's
        // registrations were prepared first. Retain each subscription order.
        std::list<ItemTrigger> killed;
        for(auto it=plan.jobs.begin();it!=plan.jobs.end();) {
            auto current=it++;
            if(current->event==ItemSkillEvent::Death) killed.splice(killed.end(),plan.jobs,current);
        }
        plan.jobs.splice(plan.jobs.begin(),killed);
        if(!terminal) std::erase_if(plan.jobs,[](const auto &job) {return job.event==ItemSkillEvent::Death || job.event==ItemSkillEvent::Kill;});
        return advanceJobs(plan.jobs,tick);
    }
    // Keep the same callback objects across output pressure. Moving only the
    // nonterminal callbacks leaves Kill at its native post-damage boundary.
    std::list<ItemTrigger> callbacks;
    for(auto it=plan.jobs.begin();it!=plan.jobs.end();) {
        auto current=it++;
        if(current->event!=ItemSkillEvent::Kill && current->event!=ItemSkillEvent::Death)
            callbacks.splice(callbacks.end(),plan.jobs,current);
    }
    const auto result=advanceJobs(callbacks,tick);
    plan.jobs.splice(plan.jobs.begin(),callbacks);
    return result;
}
StepStatus ItemTriggers::advanceJobs(std::list<ItemTrigger> &jobs,uint64_t tick) {
    for(auto it=jobs.begin();it!=jobs.end();) {
        auto &job=*it;const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,job.actor.actor);
        const auto *area=ports_.areas.find(job.actor.area);
        if(!caster || caster->area()!=job.actor.area || (caster->player && (!caster->player->entered || caster->player->player!=job.actor.player)) ||
            !area || area->generation!=job.actor.areaGeneration) {it=jobs.erase(it);continue;}
        auto actor=job.actor;actor.tick=tick;
        if(job.healStat) {
            const auto result=advancePd2ItemHeal(job,tick);
            if(isDeferred(result.status)) return StepStatus::Blocked;
            it=jobs.erase(it);continue;
        }
        const auto consume=[&](uint64_t expected,uint64_t next) {
            return caster->monster?ports_.monsters.consumeRandom(actor.actor,expected,next):ports_.transactions.consumeRandom(actor,expected,next);
        };
        if(!job.rolled) {
            // EventFunc20/21/30 query the aggregate stat when their callback
            // runs. Earlier callbacks may remove equipment or change a layer.
            if(job.chanceStat) {
                const auto current=caster->player?std::optional{caster->player->totals.nativeStats}:
                    ports_.effects.unitStatValues(actor.actor,tick);
                if(!current) {it=jobs.erase(it);continue;}
                job.chance=current->get(*job.chanceStat);
                if(job.chance<=0) {it=jobs.erase(it);continue;}
            }
            const auto expected=caster->random();auto random=expected;
            const bool accepted=int(job.event==ItemSkillEvent::Cast?pd2UnitEventRandom(random)%100:limitedRandom(random,100))<job.chance;
            const auto consumed=consume(expected,random);
            if(isDeferred(consumed.status)) return StepStatus::Blocked;
            if(!consumed) {it=jobs.erase(it);continue;}
            job.rolled=accepted;
        }
        if(!*job.rolled) {it=jobs.erase(it);continue;}
        if(!job.supported) {deferred_="Item trigger skill program is not implemented: "+std::to_string(job.skill.sourceId);it=jobs.erase(it);continue;}
        if(job.policy.support==ItemTriggerSupport::NoEffect) {it=jobs.erase(it);continue;}
        if(job.policy.support==ItemTriggerSupport::AlternativeProgram) {
            deferred_="Original alternative ItemEffect program is not implemented: "+std::to_string(job.skill.sourceId);
            it=jobs.erase(it);continue;
        }
        auto invoking=job.effectActor?skills::nativeCaster(ports_.players,ports_.monsters,job.effectActor->actor):caster;
        if(!invoking || invoking->area()!=job.actor.area) {it=jobs.erase(it);continue;}
        if(job.effectActor) {actor=*job.effectActor;actor.tick=tick;}
        if(job.phase==ItemTriggerPhase::ResolveTarget) {
            if(job.policy.support==ItemTriggerSupport::TargetProgram) {
                deferred_="Item trigger requires a victim/corpse/start-check program: "+std::to_string(job.skill.sourceId);it=jobs.erase(it);continue;
            }
            if(job.policy.victimCasts) {
                invoking=skills::nativeCaster(ports_.players,ports_.monsters,job.target);
                if(!invoking || invoking->area()!=job.actor.area) {it=jobs.erase(it);continue;}
                if(!invoking->programs() || !invoking->programs()->definitions.contains(job.skill.sourceId)) {
                    deferred_="Victim item trigger skill program is unavailable: "+std::to_string(job.skill.sourceId);
                    it=jobs.erase(it);continue;
                }
                actor=invoking->context(ports_.areas,tick);job.effectActor=actor;
                job.target=actor.actor;job.targetType=invoking->player?0:1;job.position=invoking->position();
            }
            if(ports_.effects.unitStates(actor.actor,tick).contains(invoking->programs()->uninterruptable.id)) {
                it=jobs.erase(it);continue;
            }
            if(job.target) {
                const auto position=ports_.monsters.nativeTargetPosition(job.target,actor.area);
                if(position) {job.position=position->first;job.targetType=ports_.players.findActor(job.target)?0:1;}
                else job.target={};
            }
            if(job.policy.target==ItemTriggerTarget::LastAttacker && !job.target) {
                const auto last=invoking->player?invoking->player->lastAttacker:invoking->monster->lastAttacker;
                if(const auto position=ports_.monsters.nativeTargetPosition(last,actor.area)) {
                    job.target=last;job.position=position->first;job.targetType=ports_.players.findActor(last)?0:1;
                }
            }
            if(job.policy.target==ItemTriggerTarget::Self) {job.target=actor.actor;job.targetType=invoking->player?0:1;job.position=invoking->position();}
            if(job.policy.target==ItemTriggerTarget::RandomCorpse) {
                if(!job.target || job.skill.rank<=0) {it=jobs.erase(it);continue;}
                const auto center=job.position+area->definition.origin;EntityId chosen;
                for(const auto candidate:ports_.effects.nativeAuraCandidateRecipients({actor.actor,actor.area,10,0x1002,false,center,effects::NativeAuraTargetMode::TargetRange,tick}))
                    if(ports_.monsters.nativeUsableCorpse(candidate,tick)) {chosen=candidate;break;}
                if(!chosen) {it=jobs.erase(it);continue;}
                job.target=chosen;job.targetType=1;
                const auto &corpse=*ports_.monsters.find(chosen);
                job.position=corpse.position+ports_.areas.at(corpse.area).definition.origin-area->definition.origin;
            }
            if(job.policy.target==ItemTriggerTarget::RandomPosition) {
                bool found=false;Vec chosen;
                const auto expected=invoking->random();auto random=expected;
                const auto *unit=invoking->monster;
                const auto pattern=unit?unit->collisionPattern.value_or(unit->rule.collisionPattern):NativeCollisionPattern::SmallUnit;
                const int size=pattern==NativeCollisionPattern::None?1:
                    pattern==NativeCollisionPattern::BigUnit || pattern==NativeCollisionPattern::BigPet?3:2;
                const uint16_t mask=unit?(unit->airborne || unit->burrowed || unit->submerged?0:
                    !unit->alive() && !unit->rule.deadCollision?0x8000:0x100):invoking->player->persistent.player.hp<=0?0x8000:0x80;
                const auto origin=area->definition.origin;
                const auto current=invoking->position()+origin;
                const auto room=ports_.areas.roomAtPosition(actor.area,invoking->position(),int(std::floor(current.x)),int(std::floor(current.y)));
                if(!room) {it=jobs.erase(it);continue;}
                const auto sample=[&](RoomView source,int32_t x,int32_t y){return ports_.areas.sampleNativeCollision(source,x,y,mask);};
                for(int attempt=0;attempt<10;++attempt) {
                    const int offsetX=int(limitedRandom(random,40))-20,offsetY=int(limitedRandom(random,40))-20;
                    const Vec point{std::floor(invoking->position().x)+float(offsetX),std::floor(invoking->position().y)+float(offsetY)};
                    const auto global=point+origin;
                    if(!ports_.areas.nativeCollisionMask(*room,int(std::floor(global.x)),int(std::floor(global.y)),size,mask,sample)) {chosen=point;found=true;break;}
                }
                const auto consumed=invoking->monster?ports_.monsters.consumeRandom(actor.actor,expected,random):ports_.transactions.consumeRandom(actor,expected,random);
                if(isDeferred(consumed.status)) return StepStatus::Blocked;
                if(!consumed) {it=jobs.erase(it);continue;}
                if(found) {job.position=chosen;job.target={};}
                if(!found) {it=jobs.erase(it);continue;}
            }
            if(job.startCheck==14 || job.startCheck==19) {
                if(job.startCheck==19 && !job.target) {it=jobs.erase(it);continue;}
                const auto global=job.position+area->definition.origin;
                const auto room=ports_.areas.roomAtPosition(actor.area,invoking->position(),int(std::floor(global.x)),int(std::floor(global.y)));
                if(!room || ports_.areas.at(room->area).definition.town) {it=jobs.erase(it);continue;}
            }
            // Earlier callbacks in this event may have changed the caster's
            // equipment or states. Sample formulas only when this callback
            // starts; a deferred execution then retains this exact snapshot.
            const auto &definition=invoking->programs()->definitions.at(job.skill.sourceId);
            if(!invoking->programs()->auras.contains(job.skill.sourceId))
                job.skill=skills::evaluateNativeSkill(*invoking,ports_.effects,job.skill.sourceId,job.skill.rank,tick);
            job.collision=definition.collision;
            if(invoking->programs()->pd2 && job.policy.alternativeProgram && definition.itemEffect>1 && job.skill.pd2) {
                auto program=std::make_shared<Pd2SkillProgram>(*job.skill.pd2);
                program->doFunction=definition.itemEffect;
                job.skill.pd2=std::move(program);job.skill.effect=SkillBehavior::Pd2;
                job.skill.weapon.reset();job.skill.appliedEffect.reset();
            }
            job.phase=ItemTriggerPhase::Execute;
        }
        if(job.phase==ItemTriggerPhase::Execute) {
            const auto result=ports_.skills.itemTrigger(actor,job.skill,job.collision,job.target,job.position,job.policy.death,job.alternativeEffect,&job.continuation);
            if(isDeferred(result.status)) return StepStatus::Blocked;
            if(!result) {
                if(result.status==DomainStatus::NotImplemented) deferred_="Original item trigger behavior requires its skill program: "+std::to_string(job.skill.sourceId);
                it=jobs.erase(it);continue;
            }
            job.phase=ItemTriggerPhase::Notify;
        }
        if(!ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},{ItemSkillFact{actor.actor,job.target,job.targetType,actor.area,job.skill.sourceId,job.skill.rank,job.position,uint16_t(job.policy.alternativeProgram),uint8_t(invoking->player?0:1)}}})) return StepStatus::Blocked;
        it=jobs.erase(it);
    }
    return StepStatus::Complete;
}
}
