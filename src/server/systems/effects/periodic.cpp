#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/skills/behavior.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::effects {
StepStatus System::advanceSkills(const ActorContext &actor, Recovery &recovery) {
    const auto &p=*ports_.players.find(actor.player);const auto &area=ports_.areas.at(actor.area);
    bool blocked=false;
    std::erase_if(recovery.cycles,[&](const auto &cycle) { return std::none_of(recovery.states.entries().begin(),recovery.states.entries().end(),[&](const auto &e) {return e.handle==cycle.first;}); });
    for(const auto &effect:recovery.states.entries()) {
        if(!effect.activeAt(actor.tick) || effect.spec.source.kind!=CombatEffectSource::Skill || !p.rules.skills ||
            !p.rules.skills->definitions.contains(effect.spec.source.definition)) continue;
        auto skill=skills::evaluate(p,effect.spec.source.definition,effect.spec.source.level);
        if(skill.effect==SkillBehavior::Blaze) {
            if(!area.definition.town && p.moving && recovery.previous &&
                (int(recovery.previous->x)!=int(p.position.x) || int(recovery.previous->y)!=int(p.position.y))) {
                const auto result=ports_.missiles.spawn({actor,skill,{},p.position,true});
                if(result.status==DomainStatus::Capacity) {blocked=true;continue;}
            }
        } else if(skill.effect==SkillBehavior::ThunderStorm) {
            auto &cycle=recovery.cycles[effect.handle];
            if(!cycle.next) cycle.next=((actor.tick+uint64_t(skill.stormPeriod)-1)/uint64_t(skill.stormPeriod))*uint64_t(skill.stormPeriod)+1;
            if(actor.tick<cycle.next) continue;
            EntityId next,fallback;
            if(!area.definition.town) for(const auto &[id,t]:ports_.monsters.read().actors) {
                if(!t.enemyTarget() || t.life<=0 || t.area!=actor.area || !area.definition.activation.nearby(p.position,t.position)) continue;
                const Vec d{float(int(t.position.x)-int(p.position.x)),float(int(t.position.y)-int(p.position.y))};
                if(d.x*d.x+d.y*d.y>float(skill.stormRadius*skill.stormRadius) || !area.definition.collision.missileSegment(p.position,t.position,{4,1})) continue;
                if(!fallback) fallback=id;
                if(id>cycle.last && !next) next=id;
            }
            if(!next) next=fallback;
            if(next) {
                const auto result=ports_.missiles.direct({actor,skill,{},ports_.monsters.find(next)->position,true},{next});
                if(result.status==DomainStatus::Capacity) {blocked=true;continue;}
            }
            cycle.last=next;cycle.next=((actor.tick+uint64_t(skill.stormPeriod)-1)/uint64_t(skill.stormPeriod))*uint64_t(skill.stormPeriod)+1;
        }
    }
    if(!blocked) recovery.previous=p.position;
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
StepStatus System::advanceReactions(uint64_t tick) {
    bool blocked=false;
    for(auto it=reactions_.begin();it!=reactions_.end();) {
        auto actor=it->actor;actor.tick=tick;
        const auto *p=ports_.players.find(actor.player);const auto *target=ports_.monsters.find(it->attacker);
        if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || p->persistent.player.hp<=0 ||
            !target || !target->enemyTarget() || target->life<=0 || target->area!=actor.area || !p->rules.skills ||
            !p->rules.skills->definitions.contains(it->effect.source.definition)) {it=reactions_.erase(it);continue;}
        auto skill=skills::evaluate(*p,it->effect.source.definition,it->effect.source.level);
        DomainResult<> result;
        if(const auto *freeze=std::get_if<FreezeAttacker>(&it->effect.action)) {
            skill.minimumDamage=skill.maximumDamage=0;skill.coldDuration=freeze->duration;
            // Frozen Armor freezes a successful melee attacker without a second damage roll.
            result=ports_.missiles.direct({actor,skill,{},target->position,true},{target->id});
        } else if(std::holds_alternative<ColdMeleeRetaliation>(it->effect.action)) {
            result=ports_.missiles.direct({actor,skill,{},target->position,true},{target->id});
        } else if(std::holds_alternative<ColdMissileRetaliation>(it->effect.action)) {
            const auto spawned=ports_.missiles.spawn({actor,skill,{},target->position,true});result={spawned.status,spawned?std::optional{std::monostate{}}:std::nullopt};
        } else {it=reactions_.erase(it);continue;}
        if(result.status==DomainStatus::Capacity) {blocked=true;++it;} else it=reactions_.erase(it);
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
