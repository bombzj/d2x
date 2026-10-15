#include "item_triggers.hpp"
#include "item_trigger_runtime.hpp"
#include "server/runtime/events.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "core/random.hpp"
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
void ItemTriggers::commit(ItemEventPlan plan, bool terminal) noexcept {
    if (!terminal) std::erase_if(plan.jobs, [](const auto &job) {
        return job.event == ItemSkillEvent::Death || job.event == ItemSkillEvent::Kill;
    });
    pending_.splice(pending_.end(), plan.jobs);
}
DomainResult<ItemEventPlan> ItemTriggers::prepare(const ActorContext &actor,std::initializer_list<ItemSkillEvent> events,EntityId target,Vec position) const {
    const auto *player=ports_.players.find(actor.player);
    if(!player || !player->entered || player->actor!=actor.actor || player->area!=actor.area) return {DomainStatus::InvalidActor,{}};
    if(!player->rules.character || !player->rules.character->stats) throw std::runtime_error("Missing native item event stat rules");
    const auto &stats=*player->rules.character->stats;
    ItemEventPlan plan;
    // Trigger phases follow the caller's order. Within one phase use the
    // native newest-first subscription list, never stat/layer map ordering.
    for(const auto requested:events) {
      std::set<StatKey> visited;
      for(const auto &registration:player->totals.itemEvents) {
        const auto key=registration.key;
        const auto &definition=stats.definitions.at(key.id);
        const auto event=itemSkillEvent(definition.name);
        const int function=requested==ItemSkillEvent::GetHit?21:
            requested==ItemSkillEvent::Death || requested==ItemSkillEvent::LevelUp?30:20;
        if(!event || *event!=requested || registration.binding.function!=function || !visited.insert(key).second) continue;
        // Melee/missile bindings of the same stat are alternatives in this
        // existing phase API; they must not cast the same layer twice.
        const auto chance=nativeStatValue(player->totals.nativeStats,stats,definition.name,key.layer);
        if(chance<=0) continue;
        ItemTrigger job;job.actor=actor;job.actor.areaGeneration=ports_.areas.at(actor.area).generation;job.event=requested;job.target=target;job.position=position;job.chance=chance;
        job.targetType=ports_.players.findActor(target)?0:1;
        const auto source=decodeItemSkillLayer(key.layer);
        const int id=source.skill,rank=source.rank;
        if(player->rules.skills && player->rules.skills->definitions.contains(id)) {
            const auto &definition=player->rules.skills->definitions.at(id);job.skill=skills::evaluate(*player,id,rank);job.collision=definition.collision;
            job.policy=resolveItemTriggerPolicy(job.event,{definition.itemEffect,definition.itemTarget,definition.itemTargetDo,definition.itemCheckStart,definition.itemEffectUsesPreparedProgram});
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
    for(auto it=pending_.begin();it!=pending_.end();) {
        auto &job=*it;const auto *player=ports_.players.find(job.actor.player);const auto *area=ports_.areas.find(job.actor.area);
        if(!player || !player->entered || player->actor!=job.actor.actor || player->area!=job.actor.area || !area || area->generation!=job.actor.areaGeneration) {it=pending_.erase(it);continue;}
        auto actor=job.actor;actor.tick=tick;
        if(!job.rolled) {
            const auto expected=player->combatRandom;auto random=expected;
            const bool accepted=int(limitedRandom(random,100))<job.chance;
            const auto consumed=ports_.transactions.consumeRandom(actor,expected,random);
            if(consumed.status==DomainStatus::Capacity) return StepStatus::Blocked;
            if(!consumed) {it=pending_.erase(it);continue;}
            job.rolled=accepted;
        }
        if(!*job.rolled) {it=pending_.erase(it);continue;}
        if(!job.supported) {deferred_="Item trigger skill program is not implemented: "+std::to_string(job.skill.sourceId);it=pending_.erase(it);continue;}
        if(job.policy.support==ItemTriggerSupport::NoEffect) {it=pending_.erase(it);continue;}
        if(job.policy.support==ItemTriggerSupport::AlternativeProgram) {
            deferred_="Original alternative ItemEffect program is not implemented: "+std::to_string(job.skill.sourceId);
            it=pending_.erase(it);continue;
        }
        if(job.phase==ItemTriggerPhase::ResolveTarget) {
            if(job.policy.support==ItemTriggerSupport::TargetProgram) {
                deferred_="Item trigger requires a victim/corpse/start-check program: "+std::to_string(job.skill.sourceId);it=pending_.erase(it);continue;
            }
            if(job.policy.target==ItemTriggerTarget::Self) {job.target=actor.actor;job.targetType=0;job.position=player->position;}
            if(job.policy.target==ItemTriggerTarget::RandomPosition) {
                bool found=false;Vec chosen;
                const auto expected=player->combatRandom;auto random=expected;
                for(int attempt=0;attempt<10;++attempt) {
                    const int offsetX=int(limitedRandom(random,40))-20,offsetY=int(limitedRandom(random,40))-20;
                    const Vec point{std::floor(player->position.x)+float(offsetX),std::floor(player->position.y)+float(offsetY)};
                    if(area->definition.collision.walkable(point,playerMovement)) {chosen=point;found=true;break;}
                }
                const auto consumed=ports_.transactions.consumeRandom(actor,expected,random);
                if(consumed.status==DomainStatus::Capacity) return StepStatus::Blocked;
                if(!consumed) {it=pending_.erase(it);continue;}
                if(found) {job.position=chosen;job.target={};}
                if(!found) {it=pending_.erase(it);continue;}
            }
            job.phase=ItemTriggerPhase::Execute;
        }
        if(job.phase==ItemTriggerPhase::Execute) {
            const auto result=ports_.skills.itemTrigger(actor,job.skill,job.collision,job.target,job.position,job.policy.death,false);
            if(result.status==DomainStatus::Capacity) return StepStatus::Blocked;
            if(!result) {
                if(result.status==DomainStatus::NotImplemented) deferred_="Original item trigger behavior requires its skill program: "+std::to_string(job.skill.sourceId);
                it=pending_.erase(it);continue;
            }
            job.phase=ItemTriggerPhase::Notify;
        }
        if(!ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},{ItemSkillFact{actor.actor,job.target,job.targetType,actor.area,job.skill.sourceId,job.skill.rank,job.position,uint16_t(job.policy.alternativeProgram)}}})) return StepStatus::Blocked;
        it=pending_.erase(it);
    }
    return StepStatus::Complete;
}
}
