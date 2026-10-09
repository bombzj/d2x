#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::effects {
DomainResult<ItemEventPlan> System::prepareItemEvents(const ActorContext &actor,std::initializer_list<ItemSkillEvent> events,EntityId target,Vec position) const {
    const auto *p=ports_.players.find(actor.player);
    if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area) return {DomainStatus::InvalidActor,{}};
    ItemEventPlan plan;
    for(const auto &[key,chance]:p->totals.character.combat.itemTriggers) {
        if(chance<=0 || std::find(events.begin(),events.end(),key.first)==events.end()) continue;
        ItemTrigger job;job.actor=actor;job.actor.areaGeneration=ports_.areas.at(actor.area).generation;job.event=key.first;job.target=target;job.position=position;job.chance=chance;
        const int id=key.second>>6,rank=key.second&63;
        if(p->rules.skills && p->rules.skills->definitions.contains(id)) {
            const auto &definition=p->rules.skills->definitions.at(id);job.skill=skills::evaluate(*p,id,rank);job.collision=definition.collision;job.itemTargetDo=definition.itemTargetDo;
            job.itemEffect=definition.itemEffect;job.itemTarget=definition.itemTarget;job.itemCheckStart=definition.itemCheckStart;job.alternativeCompatible=definition.itemEffectUsesPreparedProgram;job.supported=true;
        } else {job.skill.sourceId=id;job.skill.rank=rank;}
        plan.jobs.push_back(std::move(job));
    }
    if(plan.jobs.size()>4096-itemTriggers_.size()) return {DomainStatus::Capacity,{}};
    return {DomainStatus::Applied,std::move(plan)};
}
StepStatus System::advanceItemTriggers(uint64_t tick) {
    bool blocked=false;size_t count=0;
    for(auto it=itemTriggers_.begin();it!=itemTriggers_.end() && count<128;) {
        auto &job=*it;const auto *p=ports_.players.find(job.actor.player);const auto *area=ports_.areas.find(job.actor.area);
        if(!p || !p->entered || p->actor!=job.actor.actor || p->area!=job.actor.area || !area || area->generation!=job.actor.areaGeneration) {it=itemTriggers_.erase(it);continue;}
        ++count;
        if(!job.rolled) job.rolled=int(limitedRandom(ports_.random,100))<job.chance;
        if(!*job.rolled) {it=itemTriggers_.erase(it);continue;}
        if(!job.supported) {state_.itemDeferred="Item trigger skill program is not implemented: "+std::to_string(job.skill.sourceId);it=itemTriggers_.erase(it);continue;}
        // SkillItem::HandleItemEffectSkill refuses an empty ItemEffect. This
        // does not turn a passive or an unsupported item program into a cast.
        if(!job.itemEffect) {it=itemTriggers_.erase(it);continue;}
        auto actor=job.actor;actor.tick=tick;
        const bool attackEvent=job.event==ItemSkillEvent::Attack || job.event==ItemSkillEvent::Hit || job.event==ItemSkillEvent::Kill;
        // Skills::Handler selects ItemEffect instead of SrvDo only when
        // EventFunc20 passes a7=1. Never substitute the ordinary skill program
        // for a different, unimplemented item handler.
        if(attackEvent && !job.itemTargetDo && !job.alternativeCompatible) {
            state_.itemDeferred="Original alternative ItemEffect program is not implemented: "+std::to_string(job.skill.sourceId);
            it=itemTriggers_.erase(it);continue;
        }
        if(!job.resolved) {
            // EventFunc20 alone uses ItemTgtDo to cast on the victim. The
            // defender remains the caster for GetHit (notably Oculus).
            if((attackEvent && job.itemTargetDo) || job.itemTarget==3 || job.itemCheckStart) {
                state_.itemDeferred="Item trigger requires a victim/corpse/start-check program: "+std::to_string(job.skill.sourceId);it=itemTriggers_.erase(it);continue;
            }
            if(job.itemTarget==1) {job.target=actor.actor;job.targetType=0;job.position=p->position;}
            if(job.itemTarget==2) {
                bool found=false;
                for(int attempt=0;attempt<10;++attempt) {
                    const int x=int(limitedRandom(ports_.random,40))-20,y=int(limitedRandom(ports_.random,40))-20;
                    const Vec point{std::floor(p->position.x)+float(x),std::floor(p->position.y)+float(y)};
                    if(area->definition.collision.walkable(point,playerMovement)) {job.position=point;job.target={};found=true;break;}
                }
                if(!found) {it=itemTriggers_.erase(it);continue;}
            }
            job.resolved=true;
        }
        if(!job.executed) {
            const auto result=ports_.skills.itemTrigger(actor,job.skill,job.collision,job.target,job.position,job.event==ItemSkillEvent::Death,false);
            if(result.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
            if(!result) {
                if(result.status==DomainStatus::NotImplemented) state_.itemDeferred="Original item trigger behavior requires its skill program: "+std::to_string(job.skill.sourceId);
                it=itemTriggers_.erase(it);continue;
            }
            job.executed=true;
        }
        // Keep the accepted effect and notification separately: backpressure
        // on the original item-cast packet must never execute the effect twice.
        if(!ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},{ItemSkillFact{actor.actor,job.target,job.targetType,actor.area,job.skill.sourceId,job.skill.rank,job.position,uint16_t(attackEvent && !job.itemTargetDo)}}})) {blocked=true;++it;continue;}
        it=itemTriggers_.erase(it);
    }
    return blocked || !itemTriggers_.empty()?StepStatus::Blocked:StepStatus::Complete;
}
}
