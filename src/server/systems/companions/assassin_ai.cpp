#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/ai/target_query.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/missiles/native_birth.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/attributes/monster_sources.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "gameplay/skills/assassin_summon.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/effects/queries.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::companions {
StepStatus System::assassinStep(Companion &pet,TickContext tick) {
    const auto *body=ports_.monsters.find(pet.actor);const auto *owner=ports_.players.find(pet.owner);
    if(!body || !body->assassinPet || !body->alive() || !owner || !owner->entered || tick.tick<pet.nextDecision ||
       tick.tick<body->busyUntil || ports_.monsters.frozen(body->id,tick.tick)) return StepStatus::Complete;
    const auto &program=*body->assassinPet->definition;const auto &area=ports_.areas.at(body->area);
    if(program.warp) {
        if(!body->route.empty()) return StepStatus::Complete;
        if(pet.nativePetMoving) {pet.nativePetMoving=false;pet.nextDecision=tick.tick+uint64_t(std::max(1,body->rule.neutralDelayTicks));return StepStatus::Complete;}
        return shadowStep(pet,tick);
    }
    if(program.ai==AssassinPetDefinition::Ai::BladeCreeper && !body->route.empty()) return StepStatus::Complete;
    const auto die=[&] {
        const auto result=ports_.monsters.retire(body->id,tick.tick);
        if(result) pet.removeAt=tick.tick+uint64_t(body->rule.deathTicks);
        return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
    };
    auto sample=ports_.effects.unitStatValues(body->id,tick.tick);
    if(!sample || !body->rule.formulas) throw std::logic_error("Missing native assassin pet stats");
    auto input=skills::formulaInput(*body,*sample);SkillFormulaValues values(*body->rule.formulas,input);
    const int primary=program.skills[0];
    const auto rankOf=[&](int skill) {const auto found=input.referenceRanks->effective.find(skill);return found==input.referenceRanks->effective.end()?0:found->second;};
    const auto calc=[&](std::string_view field) {
        const auto result=values.calculation(FormulaDomain::Skill,primary,rankOf(primary),field);
        if(!result) throw std::logic_error("Missing native assassin pet AI calculation");return *result;
    };
    const auto param=[&](size_t index){return program.parameters[index][size_t(body->rule.difficulty)];};
    if(program.ai==AssassinPetDefinition::Ai::BladeCreeper) {
        if(pet.assassinAi[0]<0) pet.assassinAi[0]=int64_t(tick.tick)+calc("calc4");
        if(pet.assassinAi[0]<int64_t(tick.tick)) return die();
        if(!pet.assassinAi[2]) {
            const auto &row=body->rule.formulas->skills.at(primary);
            const auto missile=ports_.missiles.nativeDefinition(row.reference("srvmissilea"));
            if(!missile) throw std::logic_error("Missing native blade creeper missile");
            const auto &ownerArea=ports_.areas.at(owner->area);
            missiles::NativeSpawn spawn{owner->actor,body->area,area.generation,tick.tick,body->position,body->position,*missile,primary,rankOf(primary)};
            spawn.storedOwner=body->id;
            auto birth=ports_.missiles.prepareNativeBirth(std::span{&spawn,size_t{1}});if(!birth) return isDeferred(birth.status)?StepStatus::Blocked:StepStatus::Complete;
            const auto id=(*birth.value)->first();
            auto ownerInput=skills::formulaInput(*owner);SkillFormulaValues ownerValues(*owner->rules.skills->formulas,ownerInput);
            const auto rating=ownerValues.stat("tohit");
            if(!rating) throw std::logic_error("Missing native blade creeper owner attack rating");
            const int percent=nativeStatValue(*ownerInput.stats,*ownerInput.statRules,"item_tohit_percent")+owner->totals.equipment.weapons[0].meleeMastery[0];
            auto publication=std::make_shared<effects::AuraUnitPublication>();
            if(body->revision==UINT64_MAX) return StepStatus::Blocked;
            auto candidate=*body;++candidate.revision;
            std::map<EntityId,monsters::Actor> candidates;candidates.emplace(body->id,std::move(candidate));
            ports_.effects.stageAuraActors(*publication,std::move(candidates),0);
            ports_.effects.stageAuraSource(*publication,body->id,[rating=*rating,percent,tick=tick.tick](monsters::Actor &unit,const CombatEffectSet &states,const effects::AuraUnitPublication::SourceMutation &mutation) {
                attributes::MonsterResourceTrace resources(unit,tick,&states);resources.sample(states,true);
                resources.observeSourceWrites([&](StatValue write,EntityId item,const StatSnapshot &stats,std::shared_ptr<const StatList> root) {
                    attributes::dispatchMonsterItemAura(unit,resources,write,item,stats,std::move(root),mutation);
                });
                const auto sources=combatEffectStatSources(states.entries(),tick,true);
                resources.addBaseStat("tohit",rating,sources);resources.addBaseStat("item_tohit_percent",percent,sources);
                auto change=resources.take();unit.nativeStats=change.intrinsic;unit.life=change.life.raw();unit.maximumLife=change.maximum.raw();
            });
            const ActorContext actor{owner->player,owner->actor,owner->area,ownerArea.generation,0,tick.tick};
            transactions::CharacterEdit edit{actor,owner->inventoryRevision,owner->characterRevision,owner->persistent.player};
            auto plan=ports_.transactions.prepare(std::move(edit),nullptr,{},publication);if(!plan) return StepStatus::Blocked;
            plan.value->missileBirth=std::move(*birth.value);const auto committed=ports_.transactions.commit(std::move(*plan.value));if(!committed) return StepStatus::Blocked;
            pet.assassinMissile=id;pet.assassinAi[2]=1;
        }
        Vec destination=pet.assassinAi[1]?pet.assassinTarget:pet.assassinOrigin;
        const auto move=[&](Vec at,EntityId target={},int stop=0) {
            monsters::MoveRequest request{body->id,{body->area,area.generation,at},target,stop,75,false};
            request.pathType=15;request.pathDistance=20;return ports_.monsters.requestMove(request);
        };
        auto moved=move(destination);
        if(!moved) {
            pet.assassinAi[1]=!pet.assassinAi[1];destination=pet.assassinAi[1]?pet.assassinTarget:pet.assassinOrigin;
            moved=move(destination);
            if(!moved) move(owner->position,owner->actor,5);
        }
        pet.nextDecision=tick.tick+(moved?1:5);return StepStatus::Complete;
    }
    // AI101/104 checks owner town and its finite shot count before acquisition.
    if(ports_.areas.at(owner->area).definition.town || !rankOf(primary)) return die();
    if(pet.assassinAi[1]<0) pet.assassinAi[1]=calc("calc4");
    if(pet.assassinAi[1]<=0) return die();
    if(program.ai==AssassinPetDefinition::Ai::Sentry && body->aiStates.contains(body->rule.formulas->infernoState)) {
        const auto cleared=ports_.monsters.toggleAiState(body->id,body->rule.formulas->infernoState,false,tick.tick);
        if(!cleared) return isDeferred(cleared.status)?StepStatus::Blocked:StepStatus::Complete;
    }
    const auto target=ai::acquireNativeAiTarget({ports_.monsters,ports_.players,ports_.areas,ports_.social},*body,tick,false,true);
    if(!target || (program.ai!=AssassinPetDefinition::Ai::DeathSentry && target->distance>=param(3))) {
        pet.nextDecision=tick.tick+uint64_t(std::max(1,param(program.ai==AssassinPetDefinition::Ai::DeathSentry?1:2)));return StepStatus::Complete;
    }
    int skill=primary;EntityId victim=target->unit.id;uint8_t mode=body->rule.skillActions.at(uint16_t(skill)).nativeMode;
    if(program.ai==AssassinPetDefinition::Ai::DeathSentry) {
        const auto &row=body->rule.formulas->skills.at(primary);
        const int maximum=nativeAdd(row.number("param3"),nativeMultiply(rankOf(primary)-1,row.number("param4")))/2;
        const auto corpses=ports_.effects.nativeAuraCandidateRecipients({body->id,body->area,10,4098,false,
            target->position+area.definition.origin,effects::NativeAuraTargetMode::TargetRange,tick.tick},body);
        for(const auto candidate:corpses) {
            if(!ports_.monsters.consumableCorpse(candidate,body->area,tick.tick)) continue;
            const auto *corpse=ports_.monsters.find(candidate);
            if(corpse && candidate!=pet.lastCorpse && missileDistance(corpse->position,target->position)<maximum) {victim=candidate;mode=9;}
            break; // sub_6FD15210 returns the first matching corpse only.
        }
        if(victim==target->unit.id) {skill=program.skills[1];mode=14;}
    }
    if(victim==target->unit.id && target->distance>=param(3)) {pet.nextDecision=tick.tick+uint64_t(std::max(1,param(1)));return StepStatus::Complete;}
    auto random=body->combatRandom;
    if(victim==target->unit.id && int(limitedRandom(random,100))>=param(program.ai==AssassinPetDefinition::Ai::DeathSentry?2:0)) {
        ports_.monsters.commitCombatRandom(body->id,random);pet.nextDecision=tick.tick+uint64_t(std::max(1,param(1)));return StepStatus::Complete;
    }
    ports_.monsters.commitCombatRandom(body->id,random);
    const auto started=requestAction(pet,{body->id,uint16_t(skill),UnitTarget{victim,0,uint8_t(victim==target->unit.id?target->unit.type:1)},tick.tick,mode});
    if(isDeferred(started.status)) return StepStatus::Blocked;
    --pet.assassinAi[1];if(victim!=target->unit.id) pet.lastCorpse=victim;
    if(started) {pet.nextDecision=body->busyUntil;}
    else pet.nextDecision=tick.tick+uint64_t(std::max(1,param(1)));
    return StepStatus::Complete;
}
}
