#include "system.hpp"
#include "corpse_explosion.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/monsters/life_stats.hpp"
#include "gameplay/skills/native_damage.hpp"
#include "server/runtime/skill_rules.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>

namespace d2x::server::effects {
DomainResult<> System::corpseExplosion(EntityId sourceId,EntityId corpseId,int skill,int rank,uint64_t tick,std::shared_ptr<CorpseExplosionPulse> &retained,const SkillCastSpec *payment) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};const auto source=participants.find(sourceId);
    if(!source.id()) return {DomainStatus::InvalidActor,{}};
    if(!retained) retained=std::make_shared<CorpseExplosionPulse>();
    auto &pulse=*retained;
    if(pulse.complete) return done();
    const auto &rules=source.player?source.player->rules.skills->stats:source.monster->rule.stats;
    const auto &catalog=source.player?source.player->rules.skills->formulas:source.monster->rule.formulas;
    if(!rules || !catalog) return {DomainStatus::Unavailable,{}};
    if(!pulse.prepared) {
        pulse.base={};
        const auto *corpse=ports_.monsters.find(corpseId);
        if(!corpse || corpseId==sourceId || !ports_.monsters.nativeUsableCorpse(corpseId,tick)) return {DomainStatus::Unavailable,{}};
        const auto expected=corpse->combatRandom;auto random=expected;
        const auto ownerExpected=source.player?source.player->combatRandom:source.monster->combatRandom;auto ownerRandom=ownerExpected;
        const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
            if(low>=high) return low;
            return nativeAdd(low,int32_t(limitedRandom(ownerRandom,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
        };
        const auto sampled=unitStatValues(sourceId,tick);if(!sampled) return {DomainStatus::Unavailable,{}};
        auto input=source.player?skills::formulaInput(*source.player,draw):skills::formulaInput(*source.monster,*sampled,draw);
        SkillFormulaValues values(*catalog,input);
        const auto calc=[&](std::string_view field) {const auto value=values.calculation(FormulaDomain::Skill,skill,rank,field);if(!value) throw std::logic_error("Missing native corpse explosion calculation");return *value;};
        const auto &table=corpse->rule.lifeTables.at(size_t(corpse->rule.difficulty));if(!table) throw std::logic_error("Missing original corpse life table");
        const auto bounds=monsterLifeBounds(*table,corpse->rule.level,corpse->rule.difficulty,corpse->rule.statGameType);
        const int maximumHp=nativeShiftLeft(nativeAdd(bounds.minimum,bounds.maximum),7);
        const auto *programs=source.player?source.player->rules.skills.get():ports_.monsters.skillPrograms();
        const bool pd2=programs && programs->pd2;
        const auto percentage=[&](int32_t value,int32_t factor,int32_t denominator) {
            if(!pd2) return nativePercentage(value,factor,denominator);
            const auto result=double(value)*double(factor)/double(std::max(uint32_t(denominator),1u));
            return result>INT32_MAX?INT32_MAX:result<INT32_MIN?INT32_MIN:int32_t(result);
        };
        const int radius=calc("aurarangecalc"),minimum=percentage(calc("calc1"),maximumHp,100),maximum=percentage(calc("calc2"),maximumHp,100);
        int damage=nativeAdd(minimum,int32_t(limitedRandom(random,uint32_t(nativeSubtract(maximum,minimum)))));
        const auto level=values.stat("level");if(!level) throw std::logic_error("Missing native corpse explosion caster level");
        if(corpse->rule.level && *level<corpse->rule.level) damage=percentage(damage,*level,corpse->rule.level);
        const int percent=std::clamp(calc("calc3"),0,100);
        const auto &row=catalog->skills.at(skill);
        if(percent>0 && row.elementType) {
            const auto length=values.parameter(FormulaDomain::Skill,skill,rank,"edln");if(!length) throw std::logic_error("Missing corpse explosion elemental length");
            addNativeSkillElement(pulse.base,MissileDamageElement(row.elementType),percentage(damage,percent,100),*length,ownerRandom);
            damage=percentage(damage,100-percent,100);
        }
        pulse.base.channels[0]=damage;
        if(pd2) {
            // Current Do55 (0x3AD6B0) adds flat skill element and physical
            // damage after splitting the corpse-HP contribution, in that order.
            rollNativeSkillElement(pulse.base,values,skill,rank,ownerRandom);
            const auto physical=values.physicalRange(skill,rank,false);
            if(!physical) throw std::logic_error("Missing PD2 corpse explosion physical range");
            pulse.base.channels[0]=nativeAdd(pulse.base.channels[0],nativeAdd(physical->first,
                int32_t(limitedRandom(ownerRandom,uint32_t(nativeSubtract(physical->second,physical->first))))));
        }
        pulse.source=sourceId;pulse.corpse=corpseId;pulse.area=corpse->area;
        pulse.skill=skill;pulse.rank=rank;pulse.center=corpse->position;pulse.physicalRadiusSquared=(radius/2)*(radius/2);
        pulse.targets=nativeAuraCandidateRecipients({sourceId,corpse->area,(radius+1)/2,uint32_t(row.number("aurafilter")),false,
            corpse->position+ports_.areas.at(corpse->area).definition.origin,NativeAuraTargetMode::TargetRange,tick});
        const auto noDraw=catalog->corpseNoDrawState;
        if(noDraw<0) throw std::logic_error("Missing native corpse_nodraw state");
        // Consume the corpse, its seed and the player's cost in one publication.
        // Target callbacks may block after this point; the retained pulse owns
        // their cursor and never repeats the debit or damage roll.
        auto candidate=*corpse;candidate.corpse.unavailable=true;candidate.corpse.states.insert(noDraw);
        candidate.combatRandom=random;
        if(candidate.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        ++candidate.revision;
        std::map<EntityId,monsters::Actor> actors;actors.emplace(corpseId,std::move(candidate));
        if(source.monster && ownerRandom!=ownerExpected) {
            auto owner=*source.monster;if(owner.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
            owner.combatRandom=ownerRandom;++owner.revision;actors.emplace(sourceId,std::move(owner));
        }
        auto publication=std::make_shared<AuraUnitPublication>();publication->tick=tick;
        stageAuraActors(*publication,std::move(actors),0);
        publication->facts.emplace_back(StateFact{corpseId,1,corpse->area,noDraw,true});
        if(source.player) {
            const auto &player=*source.player;
            const ActorContext actor{player.player,sourceId,player.area,ports_.areas.at(player.area).generation,0,tick};
            transactions::CharacterEdit edit{actor,player.inventoryRevision,player.characterRevision,player.persistent.player};
            if(payment) {
                edit.player.mana-=payment->manaCost;edit.skillPayment=transactions::SkillManaPayment{payment->manaCost,payment->sourceId};edit.charge=payment->charge;
                if(payment->missileId>=0) edit.publicFacts.emplace_back(MissileFact{sourceId,0,corpse->area,payment->missileId,rank,0,pulse.center,pulse.center});
            }
            edit.random=transactions::UnitRandomEdit{ownerExpected,ownerRandom};
            auto plan=ports_.transactions.prepare(std::move(edit),nullptr,{},publication);if(!plan) return {plan.status,{}};
            const auto committed=ports_.transactions.commit(std::move(*plan.value));if(!committed) return committed;
        } else {
            const ActorContext actor{source.monster->owner.value_or(PlayerId{}),corpseId,pulse.area,
                ports_.areas.at(pulse.area).generation,0,tick};
            const auto committed=ports_.transactions.commitUnitAuras({},publication,actor);if(!committed) return committed;
        }
        pulse.prepared=true;
    }
    while(pulse.next<pulse.targets.size()) {
        const auto target=participants.find(pulse.targets[pulse.next]);if(!target.id()) {++pulse.next;continue;}
        if(!pulse.occurrence) {
            const auto targetArea=target.player?target.player->area:target.monster->area;
            auto hit=pulse.base;const auto global=target.position()+ports_.areas.at(targetArea).definition.origin;
            const auto center=pulse.center+ports_.areas.at(pulse.area).definition.origin;
            const int x=int(global.x)-int(center.x),y=int(global.y)-int(center.y);
            if(x*x+y*y>pulse.physicalRadiusSquared) hit.channels[0]=0;
            if(corpseExplosionOccurrence_>=(uint64_t{1}<<55)-1) return {DomainStatus::Capacity,{}};
            const auto occurrence=(uint64_t{1}<<56)|(corpseExplosionOccurrence_+1);
            combat::SpellImpact impact;impact.projectile=impact.source=sourceId;impact.area=targetArea;impact.targets={target.id()};impact.occurrence=occurrence;
            impact.synchronous=impact.nativeAura=impact.nativeDamageFlagsOnly=impact.unblockable=impact.retainDeadSource=true;
            impact.areaDamage=combat::MissileAreaDamage{};impact.areaDamage->rolled=std::make_shared<combat::MissileAreaRoll>();impact.areaDamage->rolled->value=hit;
            impact.missileDamage=std::make_shared<MissileDamageRollState>();impact.missileDamage->formulas=catalog;impact.missileDamage->statRules=rules;
            impact.missileDamage->skill=skill;impact.missileDamage->level=rank;
            impact.missileDamage->uninterruptableState=source.player?source.player->rules.skills->uninterruptable.id:source.monster->rule.uninterruptableState;
            impact.monsterStates=source.player?MonsterHitStates{source.player->rules.skills->chilled,source.player->rules.skills->poisoned,{}}:source.monster->rule.hitStates;
            auto queue=ports_.combat.prepareSpells({std::move(impact)});if(!queue) return {queue.status,{}};
            ports_.combat.commitSpells(std::move(*queue.value));pulse.occurrence=occurrence;++corpseExplosionOccurrence_;
        }
        if(ports_.combat.advanceAuraPulse(sourceId,*pulse.occurrence,{tick})==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
        pulse.occurrence.reset();++pulse.next;
    }
    pulse.complete=true;return done();
}
}
