#include "system.hpp"
#include "attribute_projection.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/effects/queries.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::effects {
// SKILLS_ApplyRedemptionEffect: D2MOO 5596f5c, MIT; docs/licenses/D2MOO.txt.
DomainResult<std::shared_ptr<AuraUnitPublication>> System::prepareConsumedCorpse(EntityId id,int state,uint64_t tick) {
    const auto *corpse=ports_.monsters.find(id);
    if(!corpse || !ports_.monsters.selectableCorpse(id,tick)) return {DomainStatus::Unavailable,{}};
    if(corpse->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    if(state<0 || !corpse->rule.states || !corpse->rule.states->contains(state)) throw std::logic_error("Missing original consumed-corpse state");
    auto candidate=*corpse;candidate.corpse.unavailable=true;candidate.corpse.states.insert(state);++candidate.revision;
    auto publication=std::make_shared<AuraUnitPublication>();publication->tick=tick;
    stageAuraActors(*publication,{{id,std::move(candidate)}},0);
    publication->facts.emplace_back(StateFact{id,1,corpse->area,state,true});
    return {DomainStatus::Applied,std::move(publication)};
}
DomainResult<std::shared_ptr<AuraUnitPublication>> System::prepareRedeemedCorpse(EntityId id,uint64_t tick) {
    const auto *corpse=ports_.monsters.find(id);if(!corpse) return {DomainStatus::Unavailable,{}};
    return prepareConsumedCorpse(id,corpse->rule.redeemedState,tick);
}

DomainResult<bool> System::redeemUnitCorpse(EntityId source,EntityId corpse,int skill,int rank,uint64_t tick,bool skipChance) {
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto owner=participants.find(source);if(!owner.id()) return {DomainStatus::Unavailable,{}};
    auto prepared=prepareRedeemedCorpse(corpse,tick);
    if(!prepared) return {prepared.status,{}};
    auto publication=std::move(*prepared.value);
    const auto expected=owner.player?owner.player->combatRandom:owner.monster->combatRandom;auto random=expected;
    const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
        if(low>=high) return low;
        return nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
    };
    const auto catalog=owner.player?owner.player->rules.skills->formulas:owner.monster->rule.formulas;
    if(!catalog || !catalog->skills.contains(skill)) throw std::logic_error("Missing native Redemption skill");
    const auto region=owner.player?owner.player->area:owner.monster->area;
    const auto evaluate=[&](SkillFormulaInput input,std::string_view field) {
        SkillFormulaValues values(*catalog,input);const auto result=values.calculation(FormulaDomain::Skill,skill,rank,field);
        if(!result) throw std::logic_error("Missing native Redemption formula");
        return *result;
    };
    // Native callback draws the chance before evaluating Calc1. Nested Rand
    // calls share that actual unit's stream, and Calc3 observes the HP write.
    const auto chanceDraw=skipChance?0:rollRandom(random)%100;
    DomainResult<> committed;
    if(owner.player) {
        const auto &player=*owner.player;
        const auto actor=ActorContext{player.player,source,region,ports_.areas.at(region).generation,0,tick};
        PlayerResourceTrace resources(player,tick,{},ports_.areas.statTime(region));
        const auto input=[&]{return skills::formulaInput(player,resources.record(),resources.totals(),draw);};
        if(!skipChance && chanceDraw>=uint32_t(evaluate(input(),"calc1"))) {
            const auto result=ports_.transactions.consumeRandom(actor,expected,random);
            return result?DomainResult<bool>{DomainStatus::Applied,false}:DomainResult<bool>{result.status,{}};
        }
        const int life=std::min(nativeAdd(resources.record().hp.raw(),nativeShiftLeft(evaluate(input(),"calc2"),8)),resources.maximum().life.raw());
        resources.seedResources(Fixed8::fromRaw(life),resources.record().mana,resources.record().stamina);
        const int mana=std::min(nativeAdd(resources.record().mana.raw(),nativeShiftLeft(evaluate(input(),"calc3"),8)),resources.maximum().mana.raw());
        resources.seedResources(resources.record().hp,Fixed8::fromRaw(mana),resources.record().stamina);
        transactions::CharacterEdit edit{actor,player.inventoryRevision,player.characterRevision,resources.record()};
        edit.resourceBase=resources.maximum();edit.random=transactions::UnitRandomEdit{expected,random};
        auto plan=ports_.transactions.prepare(std::move(edit),nullptr,{},publication);
        committed=plan?ports_.transactions.commit(std::move(*plan.value)):DomainResult<>{plan.status,{}};
    } else {
        const auto &unit=*owner.monster;
        UnitEffect state=units_.contains(source)?units_.at(source):UnitEffect{};state.area=unit.area;
        MonsterResourceTrace resources(unit,tick,&state.states);
        const auto sources=combatEffectStatSources(state.states.entries(),tick,true);
        const auto value=[&](std::string_view field) {
            auto shadow=unit;shadow.life=resources.read().life.raw();shadow.maximumLife=resources.read().maximum.raw();shadow.nativeStats=resources.read().intrinsic;
            return evaluate(skills::formulaInput(shadow,resources.values(sources),draw),field);
        };
        if(!skipChance && chanceDraw>=uint32_t(value("calc1"))) {
            const auto result=ports_.monsters.consumeRandom(source,expected,random);
            return result?DomainResult<bool>{DomainStatus::Applied,false}:DomainResult<bool>{result.status,{}};
        }
        resources.seedLife(Fixed8::fromRaw(std::min(nativeAdd(resources.read().life.raw(),nativeShiftLeft(value("calc2"),8)),resources.read().maximum.raw())));
        const auto values=resources.values(sources);const auto &rules=*unit.rule.stats;
        const auto mana=std::min(nativeAdd(values.get({rules.names.at("mana"),0}),nativeShiftLeft(value("calc3"),8)),
            values.get({rules.names.at("maxmana"),0}));
        resources.writeBaseStat("mana",mana,sources);
        auto life=resources.take();life.random=std::pair{expected,random};life.changed=true;
        prepareUnitEffects(*publication,source,state,std::move(life),tick);
        if(!validateAuraUnits(*publication)) return {DomainStatus::Conflict,{}};
        const auto *recipient=ports_.monsters.find(corpse);if(!recipient) return {DomainStatus::Stale,{}};
        const auto result=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},recipient->area},std::move(publication->facts)});
        committed=result?DomainResult<>{DomainStatus::Applied,std::monostate{}}:DomainResult<>{result.status,{}};
        if(committed) publishAuraUnits(*publication);
    }
    return committed?DomainResult<bool>{DomainStatus::Applied,true}:DomainResult<bool>{committed.status,{}};
}
}
