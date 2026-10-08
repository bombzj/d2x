#include "system.hpp"
#include "gameplay/combat/life.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "gameplay/combat/geometry.hpp"
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
    MonsterHit hit; hit.channels[size_t(type)] = raw;
    return receiveMonster(actor, {}, hit, {});
}
DomainResult<float> System::receiveMonster(const ActorContext &actor, EntityId source, const MonsterHit &hit, const MonsterHitStates &definitions) {
    const auto *p = ports_.players.find(actor.player);
    if (!p || !p->entered || p->actor != actor.actor || p->area != actor.area || p->persistent.player.hp <= 0 ||
        hit.mana < 0 || hit.stamina < 0 || std::any_of(hit.channels.begin(), hit.channels.end(), [](int64_t n) { return n < 0; }))
        return {DomainStatus::InvalidActor,{}};
    auto next = state_; auto &recovery = next.players[actor.actor]; auto &states = recovery.states;
    transactions::CharacterEdit edit{actor,p->inventoryRevision,p->characterRevision,p->persistent.player};
    float dealt = 0, absorbed = 0;
    std::optional<SkillCastSpec> shield;
    for (const auto &effect : states.entries()) {
        if (!effect.activeAt(actor.tick) || effect.spec.source.kind != CombatEffectSource::Skill || !p->rules.skills ||
            !p->rules.skills->definitions.contains(effect.spec.source.definition)) continue;
        auto skill = skills::evaluate(*p,effect.spec.source.definition,effect.spec.source.level);
        if (skill.effect == SkillBehavior::EnergyShield) { shield = std::move(skill); break; }
    }
    for (size_t i = 0; i < hit.channels.size(); ++i) {
        if (i == 5 && hit.poisonFrames) continue;
        float raw = float(hit.channels[i]) / 256.f;
        if (shield) {
            const auto stopped = absorbSkillShield(raw,edit.player.mana,shield->shieldPercent,shield->shieldManaFactor);
            raw = stopped.damage; edit.player.mana = stopped.mana;
        }
        const auto channel = mitigatePlayerDamage(raw,DamageType(i),p->totals.character);
        dealt += channel.dealt; absorbed += channel.absorbed;
    }
    edit.player.hp = std::clamp(edit.player.hp-dealt+absorbed,0.f,float(p->totals.character.maxLife));
    edit.player.mana = std::max(0.f,edit.player.mana-float(hit.mana)/256.f);
    edit.player.stamina = std::max(0.f,edit.player.stamina-float(hit.stamina)/256.f);
    if(edit.player.hp>0 && hit.slowFrames && definitions.slow.id>=0) {
        if(states.size()>=128 && !states.hasState(definitions.slow.id,actor.tick)) return {DomainStatus::Capacity,{}};
        CombatEffectSpec slow;slow.state=definitions.slow;slow.duration=hit.slowFrames;
        slow.source={CombatEffectSource::Monster,source,definitions.slow.id,0};slow.modifiers.velocityPercent=hit.slowPercent;
        states.apply(std::move(slow),actor.tick);
    }
    if (edit.player.hp > 0 && hit.coldFrames && !p->totals.character.combat.cannotBeFrozen && definitions.cold.id >= 0) {
        auto duration = hit.coldFrames * uint64_t(std::clamp(100-p->totals.character.coldResist,0,200)) / 100;
        if (p->totals.character.combat.halfFreezeDuration) duration /= 2;
        for (const auto &effect : states.entries()) if (effect.spec.state.id == definitions.cold.id && effect.expiresAt && *effect.expiresAt > actor.tick)
            duration = std::max(duration,*effect.expiresAt-actor.tick);
        if (duration) {
            if (states.size() >= 128 && !states.hasState(definitions.cold.id,actor.tick)) return {DomainStatus::Capacity,{}};
            CombatEffectSpec cold; cold.state = definitions.cold; cold.duration = duration;
            cold.source = {CombatEffectSource::Monster,source,definitions.cold.id,0};
            cold.modifiers.velocityPercent = cold.modifiers.otherAnimationRate = cold.modifiers.combat.attackRate = -50;
            states.apply(std::move(cold),actor.tick);
        }
    }
    if (edit.player.hp > 0 && hit.poisonFrames && hit.channels[5] && !p->totals.character.combat.preventPoison && definitions.poison.id >= 0) {
        const int64_t rate = int64_t(mitigatePlayerDamage(float(hit.channels[5])/256.f,DamageType::Poison,p->totals.character).dealt*256.f);
        const int lengthResist = std::clamp(p->totals.character.combat.poisonLengthResist+(p->rules.character?p->rules.character->resistancePenalty:0),-100,75);
        const auto duration = hit.poisonFrames * unsigned(100-lengthResist) / 100;
        if (duration && replacesPoison(recovery.poison?recovery.poison->damage.rate:0,rate)) {
            if (states.size() >= 128 && !states.hasState(definitions.poison.id,actor.tick)) return {DomainStatus::Capacity,{}};
            CombatEffectSpec poison; poison.state = definitions.poison; poison.duration = duration;
            poison.source = {CombatEffectSource::Monster,source,definitions.poison.id,0};
            states.apply(std::move(poison),actor.tick);
            recovery.poison = PoisonStatus{{rate,duration,definitions.poison.id},source,actor.tick+duration,actor.tick+1};
        }
    }
    if (edit.player.mana <= 0 && shield) {
        std::vector<EffectHandle> depleted;
        for (const auto &effect : states.entries()) if (effect.spec.source.kind == CombatEffectSource::Skill &&
            effect.spec.source.definition == shield->sourceId) depleted.push_back(effect.handle);
        for (const auto handle : depleted) states.remove(handle);
    }
    edit.transient = projection(states,actor.tick);
    const auto percent = playerLifePercentage(int64_t(edit.player.hp*256),int64_t(p->totals.character.maxLife)*256);
    if(dealt>0 || hit.mana>0 || hit.stamina>0) edit.publicFacts.emplace_back(HitFact{actor.actor,0,actor.area,percent,edit.player.hp<=0,p->position,hit.hitClass});
    if(hit.knockback && dealt>0 && edit.player.hp>0) if(const auto *attacker=ports_.monsters.find(source)) {
        const auto &area=ports_.areas.at(actor.area);const Vec destination=knockbackDestination(p->position,attacker->position,3);
        if(area.definition.collision.nativeMovementSegment(p->position,destination,playerMovement)) {
            edit.knockback=PointTarget{actor.area,area.generation,destination};
            for(auto &fact:edit.publicFacts) if(auto *damage=std::get_if<HitFact>(&fact)) damage->knockback=destination;
        }
    }
    auto plan = ports_.transactions.prepare(std::move(edit)); if (!plan) return {plan.status,{}};
    auto result = ports_.transactions.commit(std::move(*plan.value)); if (result) {
        state_.players.swap(next.players);
        if(hit.knockback && dealt>0) ports_.skills.cancel(actor.player,actor.actor);
    }
    return {result.status,result?std::optional{dealt}:std::nullopt};
}
void System::react(const ActorContext &actor, EntityId attacker, CombatEffectEvent event, bool returnFire) {
    const auto entry=state_.players.find(actor.actor);if(entry==state_.players.end()) return;
    if(event==CombatEffectEvent::HitByMissile && !returnFire) return;
    for(auto reaction:entry->second.states.reactions(event,actor.tick)) reactions_.push_back({actor,attacker,std::move(reaction)});
}
bool System::missileHitAllowed(EntityId id,uint64_t tick) const {const auto it=state_.players.find(id);return it==state_.players.end() || it->second.nextMissileHit<=tick;}
void System::missileHitDelay(EntityId id,uint64_t until) {state_.players[id].nextMissileHit=until;}

}
