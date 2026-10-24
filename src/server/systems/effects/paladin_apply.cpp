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
std::vector<StatValue> nativeEffectStats(const SkillRules &rules,const CharacterModifiers &m) {
    CombatEffectSpec effect;effect.modifiers=m;return nativeEffectStats(rules,effect);
}
std::vector<StatValue> nativeEffectStats(const StatRuleCatalog &rules,const CombatEffectSpec &effect) {
    const auto list=prepareModifierStatSource(rules,{{},effect.modifiers,combatEffectNativeValues(effect)},StatOwnerKind::Other);
    std::vector<StatValue> result;for(const auto &[key,value]:list->base) result.emplace_back(key,value);
    return result;
}
std::vector<StatValue> nativeEffectStats(const SkillRules &rules,const CombatEffectSpec &effect) {
    if(!rules.stats) throw std::runtime_error("Missing native effect stat schema");
    return nativeEffectStats(*rules.stats,effect);
}
AuraDefinition System::evaluateUnitAura(EntityId id,int skill,int rank,uint64_t tick) const {
    const auto *unit=ports_.monsters.find(id);
    if(!unit || !unit->rule.auraPrograms || !unit->rule.auraPrograms->contains(skill) || !unit->rule.formulas)
        throw std::runtime_error("Missing native unit aura program");
    auto sampled=unitStatValues(id,tick);if(!sampled) throw std::runtime_error("Missing native unit aura formula sources");
    auto context=skills::formulaInput(*unit,std::move(*sampled));
    return evaluateNativeAura(unit->rule.auraPrograms->at(skill),rank,*unit->rule.formulas,context);
}
DomainResult<> System::queueAuraPulse(EntityId source,const AuraDefinition &a,const std::vector<EntityId> &targets,uint64_t occurrence,int64_t damage) {
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    std::map<RegionId,std::vector<EntityId>> rooms;
    for(const auto id:targets) {
        const auto target=participants.find(id);
        if(!target.player && !target.monster) continue;
        const auto region=target.player?target.player->area:target.monster->area;
        if(target.aliveIn(region)) rooms[region].push_back(id);
    }
    std::vector<combat::SpellImpact> impacts;
    for(auto &[region,recipients]:rooms) {
        combat::SpellImpact pulse{source,source,region,DamageType(a.element),damage,std::move(recipients)};
        pulse.occurrence=occurrence;pulse.hitClass=uint8_t(a.hitClass);pulse.reaction=true;pulse.unblockable=true;
        pulse.knockback=(a.resultFlags&8)!=0;pulse.nativeAura=true;
        impacts.push_back(std::move(pulse));
    }
    auto plan=ports_.combat.prepareSpells(std::move(impacts));if(!plan) return {plan.status,{}};
    ports_.combat.commitSpells(std::move(*plan.value));return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<int32_t> System::rollAuraDamage(EntityId id,const AuraDefinition &a,uint64_t tick) {
    if(a.element<0 || a.maximumDamage<=0) return {DomainStatus::Applied,0};
    const auto *player=ports_.players.findActor(id);const auto *monster=ports_.monsters.find(id);
    if(!player && !monster) return {DomainStatus::InvalidActor,{}};
    const auto expected=player?player->combatRandom:monster->combatRandom;auto random=expected;
    const auto spread=nativeSubtract(a.maximumDamage.raw(),a.minimumDamage.raw());
    const auto damage=nativeAdd(a.minimumDamage.raw(),int32_t(limitedRandom(random,uint32_t(spread))));
    DomainResult<> result;
    if(player) result=ports_.transactions.consumeRandom({player->player,id,player->area,ports_.areas.at(player->area).generation,0,tick},expected,random);
    else result=ports_.monsters.consumeRandom(id,expected,random);
    return {result.status,result?std::optional<int32_t>{damage}:std::nullopt};
}
DomainResult<> System::holyFreezeShatter(EntityId id,uint64_t duration,uint64_t tick) {
    const auto *player=ports_.players.findActor(id);
    if(!player) return ports_.monsters.holyFreeze(id,tick+duration,tick);
    auto states=player->transient.effects?*player->transient.effects:CombatEffectSet{};
    if(!player->transient.effects && !player->transient.states.empty()) return {DomainStatus::Unavailable,{}};
    auto random=player->combatRandom;const bool active=rollRandom(random)%100<20;
    const auto &definition=player->rules.skills->shatter;
    const bool changed=states.hasState(definition.id,tick)!=active;states.setNativeState(definition.id,active);
    const ActorContext actor{player->player,id,player->area,ports_.areas.at(player->area).generation,0,tick};
    transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,player->persistent.player};
    edit.random=transactions::UnitRandomEdit{player->combatRandom,random};edit.transient=projectEffectAttributes(states,tick);
    if(changed && !definition.noSend) edit.publicFacts.emplace_back(StateFact{id,0,player->area,definition.id,active});
    auto plan=ports_.transactions.prepare(std::move(edit));return plan?ports_.transactions.commit(std::move(*plan.value)):DomainResult<>{plan.status,{}};
}
DomainResult<> System::advanceAuraTarget(const ActorContext &actor,EntityId source,const AuraDefinition &a,EntityId id,uint64_t duration,
    uint64_t occurrence,int32_t damage,AuraTargetProgress &progress,bool owner,bool *affected) {
    if(!progress.state) {
        const auto result=paladinAura(actor,a,id,duration,owner,false,ports_.players.findActor(source)?EntityId{}:source,affected);
        if(isDeferred(result.status)) return result;
        progress.accepted=bool(result);progress.state=true;
    }
    if(!owner && progress.accepted) {
        if(a.element>=0 && a.maximumDamage>0) {
            if(!progress.pulse) {
                const auto result=queueAuraPulse(source,a,std::vector<EntityId>{id},occurrence,damage);
                if(!result) return result;
                progress.pulse=true;
            }
            if(ports_.combat.advanceAuraPulse(source,occurrence,{actor.tick})==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
        }
        if(a.serverDoFunction==81 && !progress.post) {
            const auto result=holyFreezeShatter(id,duration,actor.tick);
            if(isDeferred(result.status)) return result;
            progress.post=true;
        }
    }
    if(progress.accepted && !owner && (a.serverDoFunction==66 || a.serverDoFunction==81) && affected) *affected=true;
    return {DomainStatus::Applied,std::monostate{}};
}
EffectApplication applyNativeAuraState(CombatEffectSet &states,const CombatEffectSpec &spec,
    const std::optional<std::vector<StatValue>> &writes,size_t callbackWrites,const StatRuleCatalog &rules,uint64_t tick,
    const std::function<void()> &observe,const std::function<std::optional<bool>(StatValue)> &procedure,bool *affected) {
    if(!writes) return states.apply(spec,tick,observe);
    if(callbackWrites>writes->size()) throw std::invalid_argument("Invalid native aura callback boundary");
    auto empty=spec;empty.modifiers={};empty.nativeValues=std::map<StatKey,int32_t>{};
    empty.stacking=EffectStacking::SkillLevel;
    auto applied=states.apply(std::move(empty),tick,observe);
    if(!applied.accepted) return applied;
    for(size_t index=0;index<callbackWrites;++index) {
        const auto &write=(*writes)[index];
        if(procedure) if(const auto handled=procedure(write)) {
            if(affected) *affected|=*handled;
            continue;
        }
        if(!states.writeNativeStats(applied.handle,std::span{&write,size_t(1)},rules,spec,observe))
            throw std::logic_error("Missing allocated native aura state");
        if(affected) *affected=true;
    }
    std::vector<StatValue> ordered;
    ordered.emplace_back(StatKey{rules.names.at("modifierlist_skill"),0},spec.source.definition);
    ordered.emplace_back(StatKey{rules.names.at("modifierlist_level"),0},spec.source.level);
    ordered.insert(ordered.end(),writes->begin()+callbackWrites,writes->end());
    if(!states.writeNativeStats(applied.handle,ordered,rules,spec,observe))
        throw std::logic_error("Missing allocated native aura state");
    return applied;
}
static void applyImmediateAuraRecipient(CombatEffectSet &states,const ActorContext &caster,const ActorContext &actor,const AuraDefinition &a,
    const SkillAuraChange &change,const StatRuleCatalog &rules,int noManaState,int passiveState,bool owner,
    const attributes::AuraSourceObserver &observe,const attributes::AuraSourceReader &read,std::vector<DomainFact> &facts,PlayerPoisonCandidate &poison) {
    if(!a.ownerWrites || !a.targetWrites || a.serverDoFunction!=65) throw std::runtime_error("Missing immediate BasicAura program");
    const auto duration=skillPeriodicDuration(nativeNarrow(uint32_t(actor.tick)),change.period);
    CombatEffectSpec spec;spec.state=owner?a.ownerState:a.state;spec.source={CombatEffectSource::Skill,caster.actor,a.skill,a.rank};
    spec.duration=uint32_t(duration);spec.nativeState=true;spec.refreshPassivesOnRemoval=true;
    spec.clearStateOnRemoval=owner?noManaState:-1;spec.modifiers=owner?a.ownerModifiers:a.modifiers;
    spec.nativeRemoval=owner?NativeStateRemoval::BasicAura:NativeStateRemoval::Default;
    const bool wasActive=states.hasState(spec.state.id,actor.tick);
    std::optional<std::map<StatKey,int32_t>> previousValues;
    for(const auto &effect:states.entries()) if(effect.spec.state.id==spec.state.id) {
        previousValues=prepareModifierStatSource(rules,{{},effect.spec.modifiers,combatEffectNativeValues(effect.spec)},StatOwnerKind::Player)->base;
        break;
    }
    auto writes=owner?*a.ownerWrites:*a.targetWrites;const auto rawCallbacks=owner?a.ownerCallbackWrites:writes.size();
    std::vector<StatValue> adjusted;
    for(size_t index=0;index<rawCallbacks;++index) {
        auto write=writes[index];const auto &name=rules.definitions.at(write.key.id).name;
        if(!write.value) continue;
        if(name=="velocitypercent" || name=="attackrate") write.value=std::max(write.value,-50);
        adjusted.push_back(write);
        if(name=="attackrate") adjusted.emplace_back(StatKey{rules.names.at("other_animrate"),0},write.value);
    }
    const auto callbacks=adjusted.size();
    adjusted.insert(adjusted.end(),writes.begin()+rawCallbacks,writes.end());writes=std::move(adjusted);
    const auto publish=[&](std::optional<ResourceValues> resources={},std::vector<int> withdrawn={}) {
        auto projection=projectEffectAttributes(states,actor.tick);
        observe({std::move(*projection.sources),std::move(projection.states),std::move(projection.effects),resources,std::move(withdrawn)});
    };
    const auto procedure=[&](StatValue write)->std::optional<bool> {
        const auto &rule=rules.definitions.at(write.key.id);
        if(rule.name=="item_poisonlengthresist") {
            const bool affected=shortenAuraHarmfulDurations(states,poison.poison,actor.tick,write.value);
            poison.changed|=affected;return affected;
        }
        if(!rule.direct) return {};
        const auto frame=read();
        ResourceValues resources{Fixed8::fromRaw(frame.currentResources.at({rules.names.at("hitpoints"),0})),
            Fixed8::fromRaw(frame.currentResources.at({rules.names.at("mana"),0})),
            Fixed8::fromRaw(frame.currentResources.at({rules.names.at("stamina"),0}))};
        auto *value=rule.name=="hitpoints"?&resources.life:rule.name=="mana"?&resources.mana:rule.name=="stamina"?&resources.stamina:nullptr;
        if(!value) throw std::runtime_error("Immediate aura direct stat requires a unit base writer");
        const auto maximum=rule.maximumStat?std::optional<int32_t>{frame.stats.get({*rule.maximumStat,0})}:std::nullopt;
        const auto next=nativeAuraDirectStatValue(value->raw(),write.value,maximum);
        if(next==value->raw()) return false;
        *value=Fixed8::fromRaw(next);publish(resources);return true;
    };
    const auto application=applyNativeAuraState(states,spec,writes,callbacks,rules,actor.tick,[&]{publish();},procedure);
    if(!application.accepted) return;
    if(passiveState>0) publish({},std::vector<int>{passiveState});
    for(const auto &removed:application.removed) if(!states.hasState(removed.effect.spec.state.id,actor.tick))
        facts.emplace_back(StateFact{actor.actor,0,actor.area,removed.effect.spec.state.id,false});
    const auto *installed=states.find(application.handle);
    if(!installed) throw std::logic_error("Missing immediate aura owner list");
    const auto values=prepareModifierStatSource(rules,{{},installed->spec.modifiers,combatEffectNativeValues(installed->spec)},StatOwnerKind::Player);
    std::vector<StatValue> stats;for(const auto &[key,value]:values->base) stats.emplace_back(key,value);
    if(!wasActive || !previousValues || *previousValues!=values->base)
        facts.emplace_back(StateFact{actor.actor,0,actor.area,spec.state.id,true,std::move(stats)});
}
void applyImmediateAuraOwner(CombatEffectSet &states,const ActorContext &actor,const AuraDefinition &a,const SkillAuraChange &change,
    const StatRuleCatalog &rules,int noManaState,int passiveState,const attributes::AuraSourceObserver &observe,
    const attributes::AuraSourceReader &read,std::vector<DomainFact> &facts,PlayerPoisonCandidate &poison) {
    applyImmediateAuraRecipient(states,actor,actor,a,change,rules,noManaState,passiveState,true,observe,read,facts,poison);
}
void applyImmediateAuraTarget(CombatEffectSet &states,const ActorContext &caster,const ActorContext &recipient,const AuraDefinition &a,const SkillAuraChange &change,
    const StatRuleCatalog &rules,int passiveState,const attributes::AuraSourceObserver &observe,
    const attributes::AuraSourceReader &read,std::vector<DomainFact> &facts,PlayerPoisonCandidate &poison) {
    applyImmediateAuraRecipient(states,caster,recipient,a,change,rules,-1,passiveState,false,observe,read,facts,poison);
}
namespace {
std::vector<RemovedCombatEffect> removeDefinition(CombatEffectSet &states,EntityId source,int skill,const std::function<void()> &observe) {
    std::vector<EffectHandle> handles;
    std::vector<RemovedCombatEffect> removed;
    for(const auto &e:states.entries()) if(e.spec.source.entity==source && e.spec.source.definition==skill) handles.push_back(e.handle);
    for(const auto h:handles) {
        auto values=states.remove(h,observe);removed.insert(removed.end(),values.begin(),values.end());
    }
    return removed;
}
}
DomainResult<> System::removeAura(const ActorContext &actor,int skill) {
    const auto *p=ports_.players.find(actor.player);if(!p) return {DomainStatus::InvalidActor,{}};
    auto next=state_;auto &r=next.players[actor.actor];PlayerResourceTrace resources(*p,actor.tick,{},ports_.areas.statTime(p->area));
    const auto removed=removeDefinition(r.states,actor.actor,skill,[&]{resources.sample(r.states,true);});
    transactions::CharacterEdit edit{actor,p->inventoryRevision,p->characterRevision,resources.record()};
    edit.resourceBase=resources.maximum();
    for(const auto &effect:removed) if(!r.states.hasState(effect.effect.spec.state.id,actor.tick))
        edit.publicFacts.emplace_back(StateFact{actor.actor,0,actor.area,effect.effect.spec.state.id,false});
    edit.transient=projectEffectAttributes(r.states,actor.tick);
    auto plan=ports_.transactions.prepare(std::move(edit));
    if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));if(result) publishPlayers(next.players);return result;
}
CharacterModifiers System::stateModifiers(EntityId id,int state,uint64_t tick) const {
    const auto *effects=unitEffects(id);
    if(effects) for(const auto &e:effects->entries()) if(e.spec.state.id==state && e.activeAt(tick)) return e.spec.modifiers;
    return {};
}
} // namespace d2x::server::effects
