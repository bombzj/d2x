#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server::effects {
DomainResult<> System::hirelingSkill(EntityId id,const SkillCastSpec &skill,uint64_t tick) {
    const auto *m=ports_.monsters.find(id);
    if(!m || !m->hireling || m->life<=0 || !m->owner || !skill.appliedEffect) return {DomainStatus::InvalidActor,{}};
    const auto *p=ports_.players.find(*m->owner);
    if(!p || !p->entered || p->area!=m->area || p->persistent.player.hp<=0 || !p->rules.skills) return {DomainStatus::InvalidActor,{}};
    auto next=units_;auto &unit=next[id];unit.area=m->area;
    if(unit.states.size()>=128) return {DomainStatus::Capacity,{}};
    auto spec=*skill.appliedEffect;spec.source={CombatEffectSource::Skill,id,skill.sourceId,skill.rank};
    if(!unit.states.apply(spec,tick).accepted) return {DomainStatus::Conflict,{}};
    const int state=spec.state.id;
    std::vector<std::pair<int,int64_t>> stats;
    if(spec.modifiers.combat.defensePercent) stats.emplace_back(p->rules.skills->nativeStats.at("skill_armor_percent"),spec.modifiers.combat.defensePercent);
    std::vector<DomainFact> facts;
    for(const int previous:unitStates(id,tick)) if(!unit.states.hasState(previous,tick)) facts.emplace_back(StateFact{id,1,m->area,previous,false});
    facts.emplace_back(StateFact{id,1,m->area,state,true,stats});
    if(skill.castOverlayId>=0) facts.emplace_back(OverlayFact{id,1,m->area,skill.castOverlayId});
    if(!ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},m->area},std::move(facts)})) return {DomainStatus::Capacity,{}};
    unit.nativeStats[state]=std::move(stats);units_.swap(next);return {DomainStatus::Applied,std::monostate{}};
}
bool System::hirelingAuraActive(EntityId id,int skill,int rank) const {
    const auto found=hirelingCycles_.find(id);
    return found!=hirelingCycles_.end() && found->second.skill==skill && found->second.rank==rank;
}
DomainResult<> System::hirelingAura(EntityId id,const AuraDefinition &a,uint64_t tick) {
    const auto *m=ports_.monsters.find(id);
    if(!m || !m->hireling || !m->owner || m->life<=0) return {DomainStatus::InvalidActor,{}};
    PaladinCycle cycle;cycle.skill=a.skill;cycle.rank=a.rank;cycle.area=m->area;cycle.aura=a;cycle.next=tick;cycle.random=m->combatRandom;
    if(!ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},m->area},
        {AttackFact{id,id,1,1,m->area,m->position,m->position,tick+1,uint16_t(a.skill),uint8_t(a.rank),false,1}}})) return {DomainStatus::Capacity,{}};
    hirelingCycles_.insert_or_assign(id,std::move(cycle));return {DomainStatus::Applied,std::monostate{}};
}
void System::hirelingReact(EntityId id,EntityId attacker,CombatEffectEvent event,uint64_t tick) {
    const auto found=units_.find(id);if(found==units_.end()) return;
    for(auto reaction:found->second.states.reactions(event,tick))
        if(std::holds_alternative<FreezeAttacker>(reaction.action)) unitReactions_.push_back({id,attacker,found->second.area,std::move(reaction)});
}
StepStatus System::advanceHirelings(uint64_t tick) {
    bool blocked=false;const combat::Participants participants{ports_.players,ports_.monsters};
    for(auto it=hirelingCycles_.begin();it!=hirelingCycles_.end();) {
        auto &cycle=it->second;const auto *m=ports_.monsters.find(it->first);
        const auto *p=m && m->owner?ports_.players.find(*m->owner):nullptr;
        if(!m || !m->hireling || m->life<=0 || !p || !p->entered || p->persistent.player.hp<=0 || m->area!=p->area) {it=hirelingCycles_.erase(it);continue;}
        if(cycle.area!=m->area) {cycle.area=m->area;cycle.pending=false;cycle.next=tick;}
        const auto &area=ports_.areas.at(m->area);const auto &a=cycle.aura;
        if(!cycle.pending && tick<cycle.next) {++it;continue;}
        if(!cycle.pending) {
            cycle.targets={m->id};cycle.target=0;
            const auto eligible=[&](combat::ParticipantView target) {
                if(target.id()==m->id || !target.aliveIn(m->area) || (target.position()-m->position).length()>a.radius || !area.definition.activation.nearby(m->position,target.position())) return false;
                if((a.filter&0x200) && !area.definition.collision.missileSegment(m->position,target.position(),{0x0805,1})) return false;
                return a.hostile?!area.definition.town && participants.canHarm({nullptr,m},target):participants.allied({nullptr,m},target,ports_.social);
            };
            if(a.filter&1) for(const auto &[key,player]:ports_.players.all()) {(void)key;if(eligible({&player,nullptr})) cycle.targets.push_back(player.actor);}
            if(a.filter&2) for(const auto &[key,unit]:ports_.monsters.read().actors) if(eligible({nullptr,&unit})) cycle.targets.push_back(key);
            cycle.pending=true;
        }
        const ActorContext actor{p->player,p->actor,p->area,area.generation,0,tick};
        const uint64_t duration=uint64_t(std::max(5,a.periodFrames))+1;
        while(cycle.target<cycle.targets.size()) {
            const auto target=cycle.targets[cycle.target];
            const auto result=paladinAura(actor,a,target,duration,target==m->id,a.lifePerPulse>0,m->id);
            if(result.status==DomainStatus::Capacity) {blocked=true;break;}
            ++cycle.target;
        }
        if(cycle.target==cycle.targets.size()) {
            if(a.element>=0 && a.maximumDamage>0 && !area.definition.town) {
                auto random=cycle.random;std::vector<EntityId> targets;
                for(const auto id:cycle.targets) if(id!=m->id) targets.push_back(id);
                const int64_t low=int64_t(a.minimumDamage*256),spread=int64_t((a.maximumDamage-a.minimumDamage)*256);
                combat::SpellImpact pulse{m->id,m->id,m->area,DamageType(a.element),low+(spread>0?limitedRandom(random,uint32_t(spread)):0),std::move(targets)};
                pulse.occurrence=cycle.occurrence+1;pulse.hitClass=uint8_t(a.hitClass);pulse.reaction=true;pulse.unblockable=true;
                auto plan=ports_.combat.prepareSpells({std::move(pulse)});
                if(!plan) {blocked|=plan.status==DomainStatus::Capacity;++it;continue;}
                ports_.combat.commitSpells(std::move(*plan.value));cycle.random=random;++cycle.occurrence;
            }
            cycle.pending=false;cycle.next=tick+uint64_t(std::max(5,a.periodFrames));cycle.targets.clear();
        }
        ++it;
    }
    while(!unitReactions_.empty()) {
        const auto &r=unitReactions_.front();const auto *m=ports_.monsters.find(r.source);const auto *target=ports_.monsters.find(r.attacker);
        if(!m || !target || !m->owner || m->life<=0 || !target->enemyTarget() || target->life<=0 || m->area!=r.area || target->area!=r.area) {unitReactions_.pop_front();continue;}
        const auto *p=ports_.players.find(*m->owner);if(!p || !p->rules.skills) {unitReactions_.pop_front();continue;}
        combat::SpellImpact impact{r.source,r.source,r.area,DamageType::Cold,0,{r.attacker}};impact.reaction=true;impact.unblockable=true;impact.freeze=true;
        impact.coldFrames=uint64_t(std::max(0.f,std::get<FreezeAttacker>(r.effect.action).duration)*25.f);impact.freezeDivisor=p->rules.skills->freezeDivisor;
        auto plan=ports_.combat.prepareSpells({std::move(impact)});if(!plan) {blocked=true;break;}
        ports_.combat.commitSpells(std::move(*plan.value));unitReactions_.pop_front();
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
