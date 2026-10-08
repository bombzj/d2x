#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include <algorithm>
namespace d2x::server::effects {
namespace {
TransientAttributes projection(const CombatEffectSet &states, uint64_t tick) {
    TransientAttributes result;result.modifiers=states.modifiers(tick);
    for(const auto &e:states.entries()) if(e.activeAt(tick) && e.spec.state.id>=0) result.states.insert(e.spec.state.id);
    return result;
}
}
DomainResult<> System::skill(const ActorContext &actor, const SkillCastSpec &skill, std::optional<PlayerId> recipient) {
    const auto *caster=ports_.players.find(actor.player);const auto *target=ports_.players.find(recipient.value_or(actor.player));
    if(!caster || !target || !caster->entered || !target->entered || caster->actor!=actor.actor || caster->area!=actor.area ||
        target->area!=actor.area || caster->persistent.player.hp<=0 || target->persistent.player.hp<=0 || !skill.appliedEffect ||
        caster->persistent.player.mana<skill.manaCost) return {DomainStatus::InvalidActor,{}};
    auto next=state_;auto &recovery=next.players[target->actor];
    if(recovery.states.size()>=128) return {DomainStatus::Capacity,{}};
    auto spec=*skill.appliedEffect;spec.source={CombatEffectSource::Skill,caster->actor,skill.sourceId,skill.rank};
    if(!recovery.states.apply(std::move(spec),actor.tick).accepted) return {DomainStatus::Conflict,{}};
    recovery.previous=target->position;
    const auto &area=ports_.areas.at(target->area);
    const ActorContext targetActor{target->player,target->actor,target->area,area.generation,0,actor.tick};
    transactions::CharacterEdit edit{targetActor,target->inventoryRevision,target->characterRevision,target->persistent.player};
    edit.transient=projection(recovery.states,actor.tick);
    std::vector<transactions::Plan> plans;
    if(target==caster) edit.player.mana-=skill.manaCost;
    else {
        transactions::CharacterEdit debit{actor,caster->inventoryRevision,caster->characterRevision,caster->persistent.player};
        debit.player.mana-=skill.manaCost;
        auto prepared=ports_.transactions.prepare(std::move(debit));if(!prepared) return {prepared.status,{}};
        plans.push_back(std::move(*prepared.value));
    }
    auto prepared=ports_.transactions.prepare(std::move(edit));if(!prepared) return {prepared.status,{}};
    plans.push_back(std::move(*prepared.value));
    const auto result=ports_.transactions.commitCharacters(std::move(plans));if(result) state_.players.swap(next.players);
    return result;
}
DomainResult<float> System::receive(const ActorContext &actor, int64_t raw, DamageType type) {
    const auto *p=ports_.players.find(actor.player);if(!p || p->actor!=actor.actor || raw<0) return {DomainStatus::InvalidActor,{}};
    auto next=state_;auto &states=next.players[actor.actor].states;
    float damage=float(raw)/256.f,mana=p->persistent.player.mana;
    for(const auto &e:states.entries()) {
        if(!e.activeAt(actor.tick) || e.spec.source.kind!=CombatEffectSource::Skill || !p->rules.skills ||
            !p->rules.skills->definitions.contains(e.spec.source.definition)) continue;
        const auto skill=skills::evaluate(*p,e.spec.source.definition,e.spec.source.level);
        if(skill.effect!=SkillBehavior::EnergyShield) continue;
        const auto result=absorbSkillShield(damage,mana,skill.shieldPercent,skill.shieldManaFactor);
        damage=result.damage;mana=result.mana;break;
    }
    const auto hit=mitigatePlayerDamage(damage,type,p->totals.character);
    transactions::CharacterEdit edit{actor,p->inventoryRevision,p->characterRevision,p->persistent.player};
    edit.player.hp=std::clamp(edit.player.hp-hit.dealt+hit.absorbed,0.f,float(p->totals.character.maxLife)); edit.player.mana=mana;
    if(mana<=0) {
        std::vector<EffectHandle> depleted;
        for(const auto &e:states.entries()) if(e.spec.source.kind==CombatEffectSource::Skill && p->rules.skills &&
            p->rules.skills->definitions.contains(e.spec.source.definition) &&
            skills::evaluate(*p,e.spec.source.definition,e.spec.source.level).effect==SkillBehavior::EnergyShield) depleted.push_back(e.handle);
        for(const auto handle:depleted) states.remove(handle);
    }
    edit.transient=projection(states,actor.tick);
    const auto percent=uint8_t(std::clamp(int(edit.player.hp*128/p->totals.character.maxLife),edit.player.hp>0?1:0,128));
    edit.publicFacts.emplace_back(HitFact{actor.actor,0,actor.area,percent,edit.player.hp<=0,p->position});
    auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
    auto result=ports_.transactions.commit(std::move(*plan.value));if(result) state_.players.swap(next.players);
    return {result.status,result?std::optional{hit.dealt}:std::nullopt};
}
void System::react(const ActorContext &actor, EntityId attacker, CombatEffectEvent event, bool returnFire) {
    const auto entry=state_.players.find(actor.actor);if(entry==state_.players.end()) return;
    if(event==CombatEffectEvent::HitByMissile && !returnFire) return;
    for(auto reaction:entry->second.states.reactions(event,actor.tick)) reactions_.push_back({actor,attacker,std::move(reaction)});
}
}
