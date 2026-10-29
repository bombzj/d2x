#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "attribute_projection.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include "gameplay/skills/assassin_progressive.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/effects/queries.hpp"
#include "gameplay/skills/weapon_damage.hpp"
#include "gameplay/skills/native_damage.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace d2x::server::effects {
DomainResult<> System::setNativeSkillState(EntityId id,int state,bool active,uint64_t tick) {
    if(unitStates(id,tick).contains(state)==active) return {DomainStatus::Applied,std::monostate{}};
    if(const auto *player=ports_.players.findActor(id)) {
        auto next=state_;auto &states=next.players[id].states;states.setNativeState(state,active);
        PlayerResourceTrace resources(*player,tick,{},ports_.areas.statTime(player->area));resources.sample(states,true);
        const ActorContext actor{player->player,id,player->area,ports_.areas.at(player->area).generation,0,tick};
        transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,resources.record()};
        edit.resourceBase=resources.maximum();edit.petLimitChanges=resources.petLimits();edit.transient=projectEffectAttributes(states,tick);
        edit.publicFacts.emplace_back(StateFact{id,0,player->area,state,active});
        auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
        const auto result=ports_.transactions.commit(std::move(*plan.value));if(result) publishPlayers(next.players);return result;
    }
    const auto *body=ports_.monsters.find(id);if(!body) return {DomainStatus::InvalidActor,{}};
    UnitEffect unit;if(const auto found=units_.find(id);found!=units_.end()) unit=found->second;unit.area=body->area;
    unit.states.setNativeState(state,active);MonsterResourceTrace resources(*body,tick,&unit.states);resources.sample(unit.states,true);
    AuraUnitPublication publication;publication.facts.emplace_back(StateFact{id,1,body->area,state,active});
    prepareUnitEffects(publication,id,unit,resources.take(),tick);if(!validateAuraUnits(publication)) return {DomainStatus::Stale,{}};
    const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},body->area},std::move(publication.facts)});if(!sent) return {sent.status,{}};
    publishAuraUnits(publication);return {DomainStatus::Applied,std::monostate{}};
}
namespace {
std::vector<AssassinCharge> progressiveViews(const SkillRules &programs,const SkillFormulaInput &input,const CombatEffectSet *states) {
    std::vector<AssassinCharge> result;if(!states) return result;
    const auto &rules=*programs.stats;
    std::map<int,const ActiveCombatEffect *> ordered;
    for(const auto &effect:states->entries()) ordered.insert_or_assign(effect.spec.state.id,&effect);
    for(const auto &[state,effect]:ordered) {
        if(!states->hasState(state,0)) continue;
        const auto native=combatEffectNativeValues(effect->spec);if(!native) continue;
        const StatSnapshot stats{*native};
        const auto skill=stats.get({rules.names.at("modifierlist_skill"),0});
        const auto definition=programs.definitions.find(skill);
        if(definition==programs.definitions.end() || !definition->second.spec.assassin) continue;
        const auto &program=*definition->second.spec.assassin;
        if(program.chargeStat<0 || program.state.id!=state) continue;
        const auto count=stats.get({uint16_t(program.chargeStat),0});if(count<=0) continue;
        auto rank=stats.get({rules.names.at("modifierlist_level"),0});
        if(const auto current=input.referenceRanks->effective.find(skill);current!=input.referenceRanks->effective.end()) rank=std::max(rank,current->second);
        if(rank>0) result.push_back({skill,rank,count,state,definition->second.spec.assassin});
    }
    return result;
}
}
std::vector<AssassinCharge> System::progressiveCharges(EntityId actor) const {
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,actor);
    if(!caster || !caster->programs()) return {};
    const auto input=caster->formulaInput(*this,0);
    return progressiveViews(*caster->programs(),input,unitEffects(actor));
}
DomainResult<> System::progressiveValue(const ActorContext &actor,const AssassinCharge &charge,int count) {
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs() || !charge.program) return {DomainStatus::InvalidActor,{}};
    const auto current=unitEffects(actor.actor);if(!current) return {DomainStatus::Unavailable,{}};
    auto states=*current;const auto *entry=states.findState(charge.state);if(!entry) return {DomainStatus::Unavailable,{}};
    const auto handle=entry->handle;const auto spec=entry->spec;
    const auto &rules=*caster->programs()->stats;
    const std::array<StatValue,1> writes{{{StatKey{uint16_t(charge.program->chargeStat),0},count}}};
    if(caster->monster) {
        const auto *unit=caster->monster;UnitEffect next;if(const auto found=units_.find(unit->id);found!=units_.end()) next=found->second;
        next.area=unit->area;next.states=std::move(states);
        MonsterResourceTrace resources(*unit,actor.tick,&next.states);
        if(!next.states.writeNativeStats(handle,writes,rules,spec,[&]{resources.sample(next.states,true);})) return {DomainStatus::Unavailable,{}};
        AuraUnitPublication publication;prepareUnitEffects(publication,unit->id,next,resources.take(),actor.tick);
        if(!validateAuraUnits(publication)) return {DomainStatus::Stale,{}};
        publishAuraUnits(publication);return {DomainStatus::Applied,std::monostate{}};
    }
    const auto *player=caster->player;
    PlayerResourceTrace resources(*player,actor.tick,{},ports_.areas.statTime(actor.area));
    if(!states.writeNativeStats(handle,writes,rules,spec,[&]{resources.sample(states,true);})) return {DomainStatus::Unavailable,{}};
    transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,resources.record()};
    edit.resourceBase=resources.maximum();edit.petLimitChanges=resources.petLimits();edit.transient=projectEffectAttributes(states,actor.tick);
    auto prepared=ports_.transactions.prepare(std::move(edit));if(!prepared) return {prepared.status,{}};
    auto next=state_;next.players[actor.actor].states=std::move(states);
    const auto committed=ports_.transactions.commit(std::move(*prepared.value));if(committed) publishPlayers(next.players);
    return committed;
}
bool System::hasProgressive(EntityId actor) const {
    return !progressiveCharges(actor).empty();
}
int32_t System::progressivePhysicalPercent(EntityId actor) const {
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,actor);if(!caster || !caster->programs()) return 0;
    auto input=caster->formulaInput(*this,0);SkillFormulaValues values(*caster->programs()->formulas,input);int32_t result{};
    for(const auto &charge:progressiveViews(*caster->programs(),input,unitEffects(actor))) if(charge.program->progressiveDamage==1) {
        const auto amount=values.calculation(FormulaDomain::Skill,charge.skill,charge.rank,"calc1");
        if(!amount) throw std::logic_error("Unavailable native progressive physical formula");
        result=nativeAdd(result,nativeMultiply(charge.count,*amount));
    }
    return result;
}
void System::progressiveDamage(EntityId actor,WeaponSkillDamage &attack,uint64_t &random) const {
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,actor);if(!caster || !caster->programs()) return;
    const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
        return low>=high?low:nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
    };
    auto input=caster->formulaInput(*this,0,draw);SkillFormulaValues values(*caster->programs()->formulas,input);
    for(const auto &charge:progressiveViews(*caster->programs(),input,unitEffects(actor))) {
        const auto kind=charge.program->progressiveDamage;
        if(kind==2) {
            if(caster->programs()->pd2) continue; // PD2 Cobra uses Do170's short contact list.
            const auto &source=caster->programs()->formulas->skills.at(charge.skill);
            const auto leech=nativeAdd(source.number("param1"),nativeMultiply(nativeSubtract(charge.rank,1),source.number("param2")));
            const auto [life,mana]=assassinCobraLeech(charge.count,leech);
            attack.lifeLeech=nativeAdd(attack.lifeLeech,life);attack.manaLeech=nativeAdd(attack.manaLeech,mana);
        } else if(kind==3 || kind==4) {
            MissileHitDamage hit;for(size_t channel=0;channel<hit.channels.size();++channel) hit.channels[channel]=nativeChecked(attack.channels[channel]);
            hit.coldLength=attack.coldFrames;hit.freezeLength=attack.freezeFrames;hit.poisonLength=attack.poisonFrames;
            rollNativeSkillElement(hit,values,charge.skill,charge.rank,random);
            if(hit.hitClass) attack.hitClass=hit.hitClass;
            const auto &source=caster->programs()->formulas->skills.at(charge.skill);
            if(source.elementType==uint8_t(MissileDamageElement::Cold)) {
                const auto divisor=source.number(kind==3?"param2":"param5");
                if(divisor && (kind==3?(charge.count>=2 && charge.count<=3):charge.count==3))
                    hit.freezeLength=nativeAdd(hit.freezeLength,nativeDivide(hit.coldLength,divisor));
            }
            for(size_t channel=0;channel<hit.channels.size();++channel) attack.channels[channel]=hit.channels[channel];
            attack.coldFrames=hit.coldLength;attack.freezeFrames=hit.freezeLength;attack.poisonFrames=hit.poisonLength;attack.softHit=true;
            if(kind==4) {
                const auto percentage=values.calculation(FormulaDomain::Skill,charge.skill,charge.rank,"calc1");
                if(!percentage) throw std::logic_error("Unavailable native progressive conversion formula");
                if(*percentage>0) {
                    const auto element=source.elementType==uint8_t(MissileDamageElement::Fire)?DamageType::Fire:
                        source.elementType==uint8_t(MissileDamageElement::Lightning)?DamageType::Lightning:DamageType::Cold;
                    attack.progressiveConversions.push_back({element,std::min(*percentage,100)});
                }
            }
        }
    }
}
DomainResult<> System::progressiveCharge(const ActorContext &actor,const SkillCastSpec &skill) {
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs() || !skill.assassin || skill.assassin->chargeStat<0) return {DomainStatus::InvalidActor,{}};
    const auto &program=*skill.assassin;const auto &rules=*caster->programs()->stats;
    const auto *states=unitEffects(actor.actor);const auto *previous=states?states->findState(program.state.id):nullptr;
    const auto key=StatKey{uint16_t(program.chargeStat),0};
    const auto oldValues=previous?combatEffectNativeValues(previous->spec):std::optional<std::map<StatKey,int32_t>>{};
    const auto old=oldValues?StatSnapshot{*oldValues}.get(key):0;
    int next=assassinNextCharge(old);
    if(caster->programs()->pd2) {
        int maximum{};for(const auto &charge:progressiveCharges(actor.actor)) maximum=std::max(maximum,charge.count);
        next=std::min(maximum+1,3);
    }
    auto input=caster->formulaInput(*this,actor.tick);SkillFormulaValues formulas(*caster->programs()->formulas,input);
    const auto duration=formulas.calculation(FormulaDomain::Skill,skill.sourceId,skill.rank,"auralencalc");
    if(!duration) return {DomainStatus::Unavailable,{}};
    CombatEffectSpec effect;effect.state=program.state;
    effect.source={CombatEffectSource::Skill,actor.actor,skill.sourceId,skill.rank};
    effect.nativeState=true;effect.nativeDirectAllocation=true;effect.nativeValues.emplace();
    effect.duration=uint32_t(*duration);effect.nativeExpiration=NativeStateExpiration::EvenZeroDuration;
    effect.stacking=EffectStacking::ReuseState;
    std::vector<StatValue> writes;
    if(!previous) {
        writes.emplace_back(StatKey{rules.names.at("modifierlist_skill"),0},skill.sourceId);
        writes.emplace_back(StatKey{rules.names.at("modifierlist_level"),0},skill.rank);
    }
    if(old!=next) {
        writes.emplace_back(key,next);
        if(caster->programs()->pd2) {
            const auto previousStats=oldValues?StatSnapshot{*oldValues}:StatSnapshot{};
            for(const auto &write:formulas.auraStats(skill.sourceId,skill.rank,6,false)) if(write.key!=key)
                writes.emplace_back(write.key,nativeAdd(previousStats.get(write.key),nativeMultiply(write.value,next-old)));
        } else if(program.chargeHitStat>=0) {
            const auto bonus=formulas.calculation(FormulaDomain::Skill,skill.sourceId,skill.rank,"aurastatcalc2");
            if(!bonus) return {DomainStatus::Unavailable,{}};
            const auto hit=StatKey{uint16_t(program.chargeHitStat),0};
            writes.emplace_back(hit,nativeAdd(oldValues?StatSnapshot{*oldValues}.get(hit):0,*bonus));
        }
    }
    if(caster->monster) {auto cast=skill;cast.appliedEffect=std::move(effect);cast.nativeEffectWrites=std::move(writes);return unitSkill(actor.actor,cast,actor.tick);}
    return apply(actor,std::move(effect),false,writes);
}
}
