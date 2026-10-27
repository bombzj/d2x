#include "system.hpp"
#include "attribute_projection.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/effects/queries.hpp"
#include "gameplay/skills/curse_resolve.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "core/random.hpp"
#include <array>
#include <stdexcept>

namespace d2x::server::effects {
DomainResult<> System::skillAuraState(EntityId source,EntityId target,int skill,int rank,int32_t duration,uint64_t tick,NativeSkillStateProcedure procedure,const NativeSkillStateValues *overrides) {
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto owner=participants.find(source),recipient=participants.find(target);
    if(!owner.id() || !recipient.id()) return {DomainStatus::Unavailable,{}};
    const auto region=recipient.player?recipient.player->area:recipient.monster->area;
    if(!recipient.aliveIn(region)) return {DomainStatus::Unavailable,{}};
    const auto catalog=owner.player?owner.player->rules.skills->formulas:owner.monster->rule.formulas;
    if(!catalog || (!overrides && !catalog->skills.contains(skill))) return {DomainStatus::Unavailable,{}};
    const auto &program=catalog->skills.at(overrides && !catalog->skills.contains(skill)?0:skill);
    const bool infection=procedure==NativeSkillStateProcedure::Infection;
    const bool warcry=procedure==NativeSkillStateProcedure::Warcry;
    const bool pd2Debuff=procedure==NativeSkillStateProcedure::Pd2Debuff;
    const bool battleCry=procedure==NativeSkillStateProcedure::BattleCry || pd2Debuff;
    const auto state=overrides?overrides->state:warcry?program.auraState:program.auraTargetState;
    const auto *states=recipient.player?recipient.player->rules.character->states.get():recipient.monster->rule.states.get();
    if(!states || !states->contains(state)) return {DomainStatus::Unavailable,{}};
    if(battleCry && recipient.monster && !recipient.monster->rule.stateTarget.acceptsCurseList()) return {DomainStatus::Unavailable,{}};
    if(infection) if(const auto *current=unitEffects(target);current && current->hasState(state,tick))
        return {DomainStatus::Unavailable,{}};
    CombatEffectSpec spec;spec.state=states->at(state);
    const bool pd2=owner.player?owner.player->rules.skills->pd2:
        owner.monster->rule.stats && owner.monster->rule.stats->pd2;
    if(battleCry && (spec.state.curse || (pd2 && state==89))) {
        const auto resistance=unitCurseResistance(target,tick);
        const auto adjusted=curseStateDuration(duration,resistance,spec.state,pd2);
        if(!adjusted) return {DomainStatus::Unavailable,{}};
        duration=*adjusted;
    }
    spec.source=battleCry?EffectSource{CombatEffectSource::Skill,source,skill,rank}:EffectSource{CombatEffectSource::NativeState,infection?target:source,0,0};
    spec.nativeDirectAllocation=!battleCry;
    spec.nativeState=true;spec.nativeValues.emplace();spec.stacking=battleCry?EffectStacking::CurseLevel:EffectStacking::ReuseState;
    spec.duration=uint32_t(duration);spec.nativeExpiration=pd2 && battleCry?
        NativeStateExpiration::NonzeroDuration:NativeStateExpiration::EvenZeroDuration;
    if(battleCry && !configurePd2Curse(spec,target,tick)) return {DomainStatus::Unavailable,{}};
    spec.refreshPassivesOnRemoval=true;
    const bool sourceFormulas=infection || warcry || pd2Debuff;
    const auto formulaUnit=sourceFormulas?owner:recipient;
    const auto expected=formulaUnit.player?formulaUnit.player->combatRandom:formulaUnit.monster->combatRandom;
    auto random=expected;
    const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
        if(low>=high) return low;
        return nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
    };
    // FingerMageSpider evaluates the six slots on the recipient after
    // posting; Rabies evaluates them on the infector and records the infected
    // unit as list owner. ATTACKRATE immediately writes OTHER_ANIMRATE.
    const auto write=[&](CombatEffectSet &effects,const StatRuleCatalog &rules,const auto &observe,const auto &calculate) {
        const auto key=[&](int slot)->std::optional<uint16_t> {
            if(overrides) {
                if(slot>int(overrides->stats.size())) return {};
                const auto id=overrides->stats[size_t(slot-1)].first;
                return id>=0 && id<=UINT16_MAX && rules.definitions.contains(uint16_t(id))?std::optional{uint16_t(id)}:std::nullopt;
            }
            const auto found=rules.names.find(program.reference("aurastat"+std::to_string(slot)));
            return found==rules.names.end()?std::nullopt:std::optional{found->second};
        };
        const auto amountAt=[&](int slot)->RuleNumber {
            return overrides?RuleNumber{overrides->stats[size_t(slot-1)].second}:calculate("aurastatcalc"+std::to_string(slot));
        };
        const auto primary=key(1);
        const auto adjusted=[&](uint16_t stat,int value) {
            if(!pd2Debuff || !recipient.monster || recipient.monster->hireling) return value;
            const auto &name=rules.definitions.at(stat).name;
            if(name!="damageresist" && name!="magicresist" && name!="fireresist" && name!="lightresist" && name!="coldresist" && name!="poisonresist") return value;
            return curseResistanceAdjustment(value,baseStatValue(rules,*recipient.monster->nativeStats->sources,{stat,0}),true,false,true);
        };
        if(pd2Debuff && primary) {
            const auto value=amountAt(1);if(!value) throw std::logic_error("Missing PD2 primary aura stat");
            auto amount=adjusted(*primary,*value);
            if(spec.pd2CurseSlots && spec.pd2CurseSlots->effectiveness && amount) {
                amount=pd2CursePrimaryValue(amount,spec.pd2CurseSlots->effectiveness);
                if(!amount) return EffectApplication{{},{},false};
            }
            spec.nativeValues->insert_or_assign(StatKey{*primary,0},amount);
            spec.modifiers=nativeCharacterModifiers(StatSnapshot{*spec.nativeValues},rules);
        }
        const auto installed=effects.apply(spec,tick,observe);
        if(!installed.accepted) return installed;
        // Retail 1.13c SrvHit19 (D2Game RVA 3E8A0) keeps the queried
        // list pointer null after allocating the first list. Its six-slot
        // writer therefore runs only on reapplication. Do not repair it.
        if((procedure==NativeSkillStateProcedure::FingerMageSpider || procedure==NativeSkillStateProcedure::SpiderGoo) && !installed.reused) return installed;
        for(int slot=1;slot<=6;++slot) {
            const auto stat=key(slot);if(!stat) continue;
            if(pd2Debuff) {
                if(slot==1) continue;
                const auto *list=effects.find(installed.handle);
                if(list && list->spec.nativeValues && list->spec.nativeValues->contains({*stat,0})) continue;
            }
            const auto value=amountAt(slot);if(!value) throw std::logic_error("Missing native recipient skill formula");
            if(!*value && !overrides) continue;
            const std::array<StatValue,1> entry{{{*stat,adjusted(*stat,*value)}}};
            if(!effects.writeNativeStats(installed.handle,entry,rules,spec,observe)) throw std::logic_error("Missing posted native skill list");
            if(!pd2Debuff && *stat==rules.names.at("attackrate")) {
                const std::array<StatValue,1> animation{{{rules.names.at("other_animrate"),*value}}};
                if(!effects.writeNativeStats(installed.handle,animation,rules,spec,observe)) throw std::logic_error("Missing native animation source");
            }
        }
        return installed;
    };
    const auto sourceValue=[&](std::string_view field) {
        const auto sampled=owner.monster?unitStatValues(source,tick):std::optional<StatSnapshot>{};
        if(owner.monster && !sampled) throw std::logic_error("Missing native infection stat source");
        auto input=owner.player?skills::formulaInput(*owner.player,draw):skills::formulaInput(*owner.monster,*sampled,draw);
        SkillFormulaValues values(*catalog,input);return values.calculation(FormulaDomain::Skill,skill,rank,field);
    };
    auto publication=std::make_shared<AuraUnitPublication>();
    std::vector<transactions::Change> edits;
    const auto externalRandom=[&] {
        if(random==expected || formulaUnit.id()==target) return;
        if(formulaUnit.player) {
            const auto &player=*formulaUnit.player;const auto &area=ports_.areas.at(player.area);
            transactions::CharacterEdit edit{{player.player,source,player.area,area.generation,0,tick},
                player.inventoryRevision,player.characterRevision,player.persistent.player};
            edit.random=transactions::UnitRandomEdit{expected,random};edits.emplace_back(std::move(edit));
        } else {
            const auto &actor=*formulaUnit.monster;UnitEffect unit;
            if(const auto current=units_.find(source);current!=units_.end()) unit=current->second;
            unit.area=actor.area;MonsterResourceTrace resources(actor,tick,&unit.states);auto life=resources.take();
            life.random=std::pair{expected,random};life.changed=true;
            prepareUnitEffects(*publication,source,unit,std::move(life),tick);
        }
    };
    if(recipient.player) {
        const auto &player=*recipient.player;const auto &area=ports_.areas.at(player.area);
        auto effects=player.transient.effects?*player.transient.effects:CombatEffectSet{};
        PlayerResourceTrace resources(player,tick,{},ports_.areas.statTime(region));
        const auto installed=write(effects,*player.rules.character->stats,[&]{resources.sample(effects,true);},[&](std::string_view field) {
            if(sourceFormulas && source!=target) return sourceValue(field);
            auto input=skills::formulaInput(player,resources.record(),resources.totals(),draw);
            SkillFormulaValues values(*catalog,input);return values.calculation(FormulaDomain::Skill,skill,rank,field);
        });
        if(!installed.accepted) return {DomainStatus::Unavailable,{}};
        resources.sample(effects);
        transactions::CharacterEdit edit{{player.player,target,player.area,area.generation,0,tick},player.inventoryRevision,player.characterRevision,resources.record()};
        edit.resourceBase=resources.maximum();edit.transient=projectEffectAttributes(effects,tick);
        if(random!=expected && formulaUnit.id()==target) edit.random=transactions::UnitRandomEdit{expected,random};
        for(const auto &removed:installed.removed) if(!effects.hasState(removed.effect.spec.state.id,tick))
            edit.publicFacts.emplace_back(StateFact{target,0,region,removed.effect.spec.state.id,false});
        const auto *list=effects.find(installed.handle);if(!list) throw std::logic_error("Missing recipient skill state");
        edit.publicFacts.emplace_back(StateFact{target,0,region,spec.state.id,true,nativeEffectStats(*player.rules.character->stats,list->spec)});
        edits.emplace_back(std::move(edit));
    } else {
    const auto &actor=*recipient.monster;UnitEffect unit;if(const auto current=units_.find(target);current!=units_.end()) unit=current->second;unit.area=region;
    MonsterResourceTrace resources(actor,tick,&unit.states);
    const auto installed=write(unit.states,*actor.rule.stats,[&]{resources.sample(unit.states,true);},[&](std::string_view field) {
        if(sourceFormulas && source!=target) return sourceValue(field);
        auto shadow=actor;shadow.life=resources.read().life.raw();shadow.maximumLife=resources.read().maximum.raw();shadow.nativeStats=resources.read().intrinsic;
        auto input=skills::formulaInput(shadow,resources.values(combatEffectStatSources(unit.states.entries(),tick,true)),draw);
        SkillFormulaValues values(*catalog,input);return values.calculation(FormulaDomain::Skill,skill,rank,field);
    });
    if(!installed.accepted) return {DomainStatus::Unavailable,{}};
    resources.sample(unit.states);auto life=resources.take();if(random!=expected && formulaUnit.id()==target) {life.random=std::pair{expected,random};life.changed=true;}
    for(const auto &removed:installed.removed) if(!unit.states.hasState(removed.effect.spec.state.id,tick)) {
        unit.nativeStats.erase(removed.effect.spec.state.id);publication->facts.emplace_back(StateFact{target,1,region,removed.effect.spec.state.id,false});
    }
    const auto *list=unit.states.find(installed.handle);if(!list) throw std::logic_error("Missing recipient skill state");
    unit.nativeStats[spec.state.id]=nativeEffectStats(*actor.rule.stats,list->spec);
    publication->facts.emplace_back(StateFact{target,1,region,spec.state.id,true,unit.nativeStats.at(spec.state.id)});
    prepareUnitEffects(*publication,target,unit,std::move(life),tick);
    }
    externalRandom();
    if(!edits.empty()) {
        auto prepared=ports_.transactions.prepareGroup(std::move(edits),publication);
        return prepared?ports_.transactions.commitCharacters(std::move(*prepared.value)):DomainResult<>{prepared.status,{}};
    }
    if(!validateAuraUnits(*publication)) return {DomainStatus::Conflict,{}};
    const auto output=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},region},std::move(publication->facts)});if(!output) return {output.status,{}};
    publishAuraUnits(*publication);return {DomainStatus::Applied,std::monostate{}};
}
}
