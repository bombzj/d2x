#include "damage_absorption.hpp"
#include "system.hpp"
#include "attribute_projection.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/combat/participants.hpp"
#include "gameplay/units/direction.hpp"
#include "core/random.hpp"
#include "gameplay/combat/shield.hpp"
#include "gameplay/effects/queries.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x::server::effects {
// SkillNec Event22, SkillSor Event24, SkillDruid Event25: D2MOO 5596f5c,
// MIT, docs/licenses/D2MOO.txt. Retail 1.13c corrects Event22's depletion
// subtraction and confirms Event25's signed (new-old)>=5 graphics branch.
DomainResult<> applyDamageAbsorption(CombatEffectSet &states,std::array<int64_t,6> &damage,DamageReception &result,
    const StatRuleCatalog &rules,const SkillFormulaCatalog &catalog,uint64_t tick,const AbsorptionFormula &formula,
    const std::function<void()> &observe,const std::function<int32_t()> &readMana,const std::function<void(int32_t)> &writeMana,std::set<int> &changed) {
    for(const auto &event:states.unitEvents(NativeUnitEvent::AbsorbDamage,tick)) {
        if(!states.registered(event)) continue;
        const auto record=catalog.skills.find(event.source.definition);if(record==catalog.skills.end()) continue;
        const auto &skill=record->second;
        const auto *entry=states.findState(skill.auraState);if(!entry) continue;
        const auto handle=entry->handle;const auto spec=entry->spec;
        const auto remove=[&] {
            states.remove(handle,observe);changed.insert(skill.auraState);
        };
        const auto function=event.binding.function;
        if(function==24) {
            const int rank=event.source.level<0?1:event.source.level;
            const auto percent=formula(skill.id,rank,"calc1");if(percent<=0) continue;
            result.mana=readMana();
            const auto factor=std::max(1,formula(skill.id,rank,"calc2"));
            int32_t absorbed=0;
            for(const auto type:{DamageType::Physical,DamageType::Fire,DamageType::Lightning,DamageType::Cold,DamageType::Magic}) {
                auto &value=damage[size_t(type)];if(value<=0) continue;
                const auto stopped=absorbManaShieldFixed(value,result.mana,percent,factor);
                absorbed=nativeAdd(absorbed,nativeSubtract(nativeChecked(value),nativeChecked(stopped.damage)));
                value=stopped.damage;result.mana=stopped.mana;
            }
            if(result.drain.shieldEligible) for(auto *value:{&result.drain.life,&result.drain.mana,&result.drain.stamina}) {
                if(*value<=0) continue;
                const auto stopped=absorbManaShieldFixed(*value,result.mana,percent,factor);
                absorbed=nativeAdd(absorbed,nativeSubtract(nativeChecked(*value),nativeChecked(stopped.damage)));
                *value=stopped.damage;result.mana=stopped.mana;
            }
            writeMana(nativeChecked(result.mana));
            if(absorbed>0 && skill.procOverlay>=0) result.directionalOverlays.push_back(skill.procOverlay);
            if(result.mana<=0) remove();
            continue;
        }
        if(function!=22 && function!=25) return {DomainStatus::NotImplemented,{}};
        const auto poolId=rules.names.find(skill.reference("aurastat1")),maximumId=rules.names.find(skill.reference("aurastat2"));
        if(poolId==rules.names.end() || maximumId==rules.names.end()) continue;
        const StatKey poolKey{poolId->second,0},maximumKey{maximumId->second,0};
        const auto values=combatEffectNativeValues(spec);if(!values) continue;
        const auto read=[&](StatKey key) {const auto found=values->find(key);return found==values->end()?0:found->second;};
        const auto maximum=read(maximumKey);if(maximum<=0) continue;
        auto pool=read(poolKey);
        const auto write=[&](StatKey key,int32_t value) {
            const std::array<StatValue,1> writes{{{key,value}}};
            if(!states.writeNativeStats(handle,writes,rules,spec,observe,false)) throw std::logic_error("Missing native shield list");
        };
        const auto absorb=[&](DamageType type) {
            auto &value=damage[size_t(type)];
            if(pool>value) {pool=nativeSubtract(pool,nativeChecked(value));value=0;}
            else {value=nativeSubtract(nativeChecked(value),pool);pool=0;}
        };
        if(function==22) {
            if(pool>0) {absorb(DamageType::Physical);write(poolKey,pool);changed.insert(skill.auraState);}
            if(!pool) {remove();continue;}
        } else {
            if(pool) {
                for(const auto type:{DamageType::Fire,DamageType::Cold,DamageType::Lightning}) if(damage[size_t(type)]>0) absorb(type);
                write(poolKey,pool);changed.insert(skill.auraState);
            }
            if(pool<=0) {remove();continue;}
        }
        const auto percentage=function==22?nativePercentage(pool,100,maximum):nativePercentage(100,pool,maximum);
        const auto unsent=rules.names.find("unsentparam1");if(unsent==rules.names.end()) throw std::logic_error("Missing shield graphics stat");
        const StatKey unsentKey{unsent->second,0};
        auto difference=nativeSubtract(percentage,read(unsentKey));
        if(function==22 && difference<0) difference=nativeSubtract(0,difference);
        if(difference>=5) write(unsentKey,percentage);
    }
    result.mana=readMana();return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<UnitReceptionPlan> System::prepareUnitReception(EntityId id,EntityId source,const MonsterHit &hit,uint64_t tick,
    const std::array<int,6> &pierce,bool bypass,bool sanctuary,bool directDrain) const {
    const auto *unit=ports_.monsters.find(id);if(!unit || !unit->alive()) return {DomainStatus::InvalidActor,{}};
    UnitReceptionPlan plan;plan.target=id;plan.tick=tick;
    plan.damage.drain={std::max<int64_t>(0,hit.mana),std::max<int64_t>(0,hit.stamina),directDrain,std::max<int64_t>(0,hit.lifeDrain)};
    auto channels=hit.channels;
    const auto current=units_.find(id);
    if(current!=units_.end() && !current->second.states.unitEvents(NativeUnitEvent::AbsorbDamage,tick).empty()) {
        if(!unit->rule.stats || !unit->rule.formulas) return {DomainStatus::Unavailable,{}};
        auto state=current->second;auto &states=state.states;
        MonsterResourceTrace resources(*unit,tick,&current->second.states);
        const auto sources=[&]{return combatEffectStatSources(states.entries(),tick,true);};
        resources.sample(states,true);
        plan.damage.mana=resources.values(sources()).get({unit->rule.stats->names.at("mana"),0});
        const auto observe=[&]{resources.sample(states,true);};
        auto random=unit->combatRandom;
        const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
        if(low>=high) return low;
        return nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
    };
        const auto formula=[&](int skill,int rank,std::string_view field) {
            auto shadow=*unit;shadow.life=resources.read().life.raw();shadow.maximumLife=resources.read().maximum.raw();shadow.nativeStats=resources.read().intrinsic;
            auto input=skills::formulaInput(shadow,resources.values(sources()),draw);
            SkillFormulaValues values(*unit->rule.formulas,input);
            const auto value=values.calculation(FormulaDomain::Skill,skill,rank,field);
            if(!value) throw std::logic_error("Missing native unit absorption formula");
            return *value;
        };
        const auto readMana=[&] {return resources.values(sources()).get({unit->rule.stats->names.at("mana"),0});};
        const auto writeMana=[&](int32_t mana) {resources.writeBaseStat("mana",mana,sources());};
        std::set<int> changed;
        const auto absorbed=applyDamageAbsorption(states,channels,plan.damage,*unit->rule.stats,*unit->rule.formulas,tick,formula,observe,readMana,writeMana,changed);
        if(!absorbed) return {absorbed.status,{}};
        auto life=resources.take();if(random!=unit->combatRandom) {life.random=std::pair{unit->combatRandom,random};life.changed=true;}
        if(!states.sharesSnapshot(current->second.states) || life.changed || life.random) {
            plan.publication=std::make_shared<AuraUnitPublication>();
            prepareUnitEffects(*plan.publication,id,state,std::move(life),tick);
            for(const auto stateId:changed) {
                const auto *effect=states.findState(stateId);
                auto &projection=plan.publication->units.at(id).nativeStats;
                if(!effect) {projection.erase(stateId);plan.publication->facts.emplace_back(StateFact{id,1,unit->area,stateId,false});}
                else {
                    auto values=nativeEffectStats(*unit->rule.stats,effect->spec);projection[stateId]=values;
                    plan.publication->facts.emplace_back(StateFact{id,1,unit->area,stateId,true,std::move(values)});
                }
            }
        }
    }
    plan.damage=receiveDamageChannels(channels,unitDefenses(id,tick),std::move(plan.damage),!unit->hireling,pierce,bypass,sanctuary);
    if(unit->rule.stats && unit->rule.stats->pd2) pd2RathmaReception(plan);
    if(!plan.damage.directionalOverlays.empty()) {
        const auto attacker=combat::Participants{ports_.players,ports_.monsters,ports_.social}.find(source);
        if(attacker.id()) {
            const auto region=attacker.player?attacker.player->area:attacker.monster->area;
            const auto target=unit->position+ports_.areas.at(unit->area).definition.origin;
            const auto origin=attacker.position()+ports_.areas.at(region).definition.origin;
            const auto direction=nativeDirectionVector(int64_t(std::floor(origin.x))-int64_t(std::floor(target.x)),int64_t(std::floor(origin.y))-int64_t(std::floor(target.y))).index;
            if(!plan.publication) plan.publication=std::make_shared<AuraUnitPublication>();
            for(const auto base:plan.damage.directionalOverlays) plan.publication->facts.emplace_back(OverlayFact{id,1,unit->area,base+nativeOverlayDirection(direction)});
        }
    }
    return {DomainStatus::Applied,std::move(plan)};
}
DomainResult<> System::commitUnitReception(UnitReceptionPlan &plan) {
    if(!plan.publication) return {DomainStatus::Applied,std::monostate{}};
    auto &publication=*plan.publication;
    if(!validateAuraUnits(publication)) return {DomainStatus::Stale,{}};
    const auto *unit=ports_.monsters.find(plan.target);if(!unit) return {DomainStatus::Stale,{}};
    if(!publication.facts.empty()) {
        const auto result=ports_.events.publish({0,plan.tick,{}, {AudienceKind::Area,{},unit->area},publication.facts});
        if(!result) return {result.status,{}};
    }
    // A native direct resource write (including Rathma's mirror) has no
    // mandatory wire fact. An empty event batch is invalid, not backpressure.
    publishAuraUnits(publication);plan.publication.reset();
    return {DomainStatus::Applied,std::monostate{}};
}
}
