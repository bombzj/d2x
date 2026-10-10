#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/skills/behavior.hpp"
#include <algorithm>
namespace d2x::server::effects {
std::set<int> System::unitStates(EntityId id,uint64_t tick) const {
    std::set<int> states;const auto it=units_.find(id);if(it==units_.end()) return states;
    for(const auto &effect:it->second.states.entries()) if(effect.activeAt(tick) && effect.spec.state.id>=0) states.insert(effect.spec.state.id);
    return states;
}
DomainResult<> System::convert(const ActorContext &actor,EntityId id,const WeaponSkillSpec &skill) {
    auto next=units_;auto &unit=next[id];unit.area=actor.area;
    std::vector<DomainFact> facts;std::vector<EffectHandle> removed;
    for(const auto &effect:unit.states.entries())
        if(effect.spec.stacking==EffectStacking::AuraLevel || effect.spec.source.kind==CombatEffectSource::Monster) removed.push_back(effect.handle);
    for(const auto handle:removed) for(const auto &effect:unit.states.remove(handle)) {
        const int state=effect.effect.spec.state.id;unit.nativeStats.erase(state);
        if(!unit.states.hasState(state,actor.tick)) facts.emplace_back(StateFact{id,1,actor.area,state,false});
    }
    if(!ports_.events.hasCapacity(facts.size()+2,facts.empty()?1:2)) return {DomainStatus::Capacity,{}};
    const auto result=ports_.monsters.convert(id,actor,skill);if(!result) return result;
    if(!facts.empty()) ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},std::move(facts)});
    unit.converted=true;units_.swap(next);return result;
}
DomainResult<> System::skillUnit(const ActorContext &actor,const SkillCastSpec &skill,EntityId id) {
    const auto *caster=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    if(!caster || !caster->entered || caster->actor!=actor.actor || caster->area!=actor.area || caster->persistent.player.hp<=0 ||
       !area || area->generation!=actor.areaGeneration || skill.effect!=SkillBehavior::Enchant || !skill.appliedEffect ||
       caster->persistent.player.mana<skill.manaCost) return {DomainStatus::InvalidActor,{}};
    const auto *m=ports_.monsters.find(id);
    const bool allied=m && m->owner && m->life>0 && m->area==actor.area;
    const bool npc=std::any_of(area->definition.npcs.begin(),area->definition.npcs.end(),[&](const auto &n){return n.id==id;});
    if(!allied && !npc) return {DomainStatus::InvalidRequest,{}};
    auto next=units_;auto &target=next[id];target.area=actor.area;
    if(target.states.size()>=128) return {DomainStatus::Capacity,{}};
    auto spec=*skill.appliedEffect;spec.source={CombatEffectSource::Skill,caster->actor,skill.sourceId,skill.rank};
    if(!target.states.apply(std::move(spec),actor.tick).accepted) return {DomainStatus::Conflict,{}};
    transactions::CharacterEdit debit{actor,caster->inventoryRevision,caster->characterRevision,caster->persistent.player};
    debit.player.mana-=skill.manaCost;debit.charge=skill.charge;
    const auto before=unitStates(id,actor.tick);
    for(const auto &effect:target.states.entries()) if(effect.activeAt(actor.tick) && !before.contains(effect.spec.state.id))
        debit.publicFacts.emplace_back(StateFact{id,1,actor.area,effect.spec.state.id,true});
    auto plan=ports_.transactions.prepare(std::move(debit));if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));if(result) units_.swap(next);return result;
}
StepStatus System::advanceUnits(uint64_t tick) {
    bool blocked=false;
    for(auto it=units_.begin();it!=units_.end();) {
        const auto *m=ports_.monsters.find(it->first);const auto *area=ports_.areas.find(it->second.area);
        const bool npc=area && std::any_of(area->definition.npcs.begin(),area->definition.npcs.end(),[&](const auto &n){return n.id==it->first;});
        if(!m && !npc) {it=units_.erase(it);continue;}
        auto next=it->second.states;
        std::vector<RemovedCombatEffect> allegianceRemoved;
        if(it->second.converted && m && !m->conversion) {
            std::vector<EffectHandle> handles;
            for(const auto &effect:next.entries()) if(effect.spec.stacking==EffectStacking::AuraLevel || effect.spec.source.kind==CombatEffectSource::Monster) handles.push_back(effect.handle);
            for(const auto handle:handles) {auto removed=next.remove(handle);allegianceRemoved.insert(allegianceRemoved.end(),removed.begin(),removed.end());}
        }
        const auto removed=m && m->life<=0 ? next.onDeath(EffectUnitKind::Monster):next.expire(tick);
        std::vector<DomainFact> facts;
        for(const auto &e:allegianceRemoved) if(!next.hasState(e.effect.spec.state.id,tick)) facts.emplace_back(StateFact{it->first,1,it->second.area,e.effect.spec.state.id,false});
        for(const auto &e:removed) if(!next.hasState(e.effect.spec.state.id,tick)) facts.emplace_back(StateFact{it->first,1,it->second.area,e.effect.spec.state.id,false});
        if(!facts.empty()) {
            const auto result=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},it->second.area},std::move(facts)});
            if(!result) {blocked=true;++it;continue;}
        }
        std::swap(it->second.states,next);
        if(m && !m->conversion) it->second.converted=false;
        if(m) ports_.monsters.velocityModifier(it->first,it->second.states.modifiers(tick).velocityPercent);
        if(!it->second.states.size()) it=units_.erase(it);else ++it;
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
