#include "system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/missiles/native_birth.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/monsters/native_spawn.hpp"
#include "server/systems/attributes/monster_sources.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"
#include "gameplay/skills/bone_spec.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "native_pet_move.hpp"
#include "server/systems/ai/target_query.hpp"
#include "server/systems/ai/system.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/units/distance.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::companions {
StepStatus System::refreshNecro(Companion &pet,TickContext tick) {
    const auto *owner=ports_.players.find(pet.owner);const auto *body=ports_.monsters.find(pet.actor);
    if(!owner || !body || !pet.sourceSkill || body->implementation==MonsterKind::BoneWall || !owner->rules.skills) return StepStatus::Complete;
    if(pet.ownerLevel==owner->persistent.player.level && pet.effectiveRanks==owner->totals.skillRanks && pet.hardRanks==owner->persistent.player.skillRanks) return StepStatus::Complete;
    if(body->revived() || !owner->rules.skills->pd2) {
        // SkillNec installs the owner's contributions on cast. Native skill
        // callbacks update PlayerPets caps, not an existing summon's birth stats.
        pet.ownerLevel=owner->persistent.player.level;pet.effectiveRanks=owner->totals.skillRanks;pet.hardRanks=owner->persistent.player.skillRanks;
        return StepStatus::Complete;
    }
    auto skill=skills::evaluate(*owner,*pet.sourceSkill,pet.rank);if(!skill.summon) return StepStatus::Complete;
    {
        if(ports_.skills.busy(owner->actor,tick.tick)) return StepStatus::Complete;
        const auto &area=ports_.areas.at(owner->area);ActorContext actor{owner->player,owner->actor,owner->area,area.generation,UINT64_MAX-body->id.value-2,tick.tick};
        const auto waiting=pending_.find(owner->actor);
        if(waiting!=pending_.end() && waiting->second.refresh!=body->id) return StepStatus::Complete;
        if(waiting==pending_.end()) {
            if(nextPreparation_==UINT64_MAX) return StepStatus::Blocked;
            Preparation source{actor,owner->inventoryRevision,body->combatRandom,skill.sourceId,skill.rank,owner->persistent.difficulty,owner->persistent.player.level,owner->persistent.player.skillRanks,*skill.summon,body->rule};
            source.effectiveRanks=owner->totals.skillRanks;source.token=nextPreparation_++;
            source.refresh=body->id;source.material=body->necroMaterial;pending_.emplace(owner->actor,std::move(source));return StepStatus::Complete;
        }
        if(waiting->second.inventoryRevision!=owner->inventoryRevision || waiting->second.ownerLevel!=owner->persistent.player.level ||
            waiting->second.rank!=skill.rank || waiting->second.hardRanks!=owner->persistent.player.skillRanks ||
            waiting->second.effectiveRanks!=owner->totals.skillRanks || waiting->second.actor.area!=owner->area ||
            waiting->second.actor.areaGeneration!=area.generation ||
            (body->necroMaterial && (!waiting->second.material || waiting->second.material->id!=body->necroMaterial->id ||
                waiting->second.material->revision!=body->necroMaterial->revision))) {cancel(owner->actor);return StepStatus::Complete;}
        const auto ready=prepared_.find(owner->actor);if(ready==prepared_.end()) return StepStatus::Complete;
        if(!ready->second.deferred.empty()) {cancel(owner->actor);return StepStatus::Complete;}
        auto updated=*body;updated.rule=ready->second.rule;updated.rule.statGameType=body->rule.statGameType;
        updated.rule.level=ready->second.summon.stats.level;updated.rule.alignment=2;
        const auto &stats=ready->second.summon.stats;const auto &attributes=stats.attributes;
        updated.rule.defense=attributes.defense;updated.rule.attackRating=attributes.attackRating;
        updated.rule.minimumDamage=int(stats.minimumDamage);updated.rule.maximumDamage=int(stats.maximumDamage);
        updated.rule.criticalChance=stats.critical;updated.rule.blockChance=stats.block;updated.rule.damageRegen=stats.damageRegen;
        updated.rule.resistances={attributes.combat.physicalResist,attributes.combat.magicResist,
            attributes.fireResist,attributes.lightningResist,attributes.coldResist,attributes.poisonResist};
        for(auto &[mode,attack]:updated.rule.attacks) {
            (void)mode;attack.minimum=updated.rule.minimumDamage;attack.maximum=updated.rule.maximumDamage;attack.rating=updated.rule.attackRating;
        }
        updated.petStats=ready->second.summon.stats;updated.necroPet=ready->second.summon.necro;
        StatList binding;binding.ownerKind=StatOwnerKind::Monster;binding.source.owner=EntityId{1};
        binding.contributions=ready->second.equipmentSources;
        const auto equipment=statListWithOwner(binding,body->id);
        updated.nativeStats=attributes::prepareSummonStatSources(updated,ready->second.summon,equipment->contributions,
            ports_.areas.statTime(body->area),tick.tick,{});
        updated.rule.level=nativeStatValue(updated.nativeStats->values,*updated.rule.stats,"level");
        updated.petStats.attributes.maxLifeFixed=Fixed8::fromRaw(nativeStatValue(updated.nativeStats->values,*updated.rule.stats,"maxhp"));
        updated.petStats.attributes.maxLife=std::max(1,updated.petStats.attributes.maxLifeFixed.raw()>>8);
        updated.petStats.attributes.combat=nativeCombatModifiers(updated.nativeStats->values,*updated.rule.stats,updated.petStats.attributes.combat);
        if(body->necroMaterial && updated.nativeStats && updated.nativeStats->sources) {
            auto replacement=*body;replacement.rule=updated.rule;replacement.petStats=updated.petStats;replacement.necroPet=updated.necroPet;
            replacement.necroMaterialRestoration=ready->second.materialRestoration;
            if(replacement.revision==UINT64_MAX) return StepStatus::Blocked;
            ++replacement.revision;
            replacement.petWeapon=ready->second.weapon.item?std::optional{ready->second.weapon}:std::nullopt;
            const auto target=statListWithOwner(*updated.nativeStats->sources,body->id);
            std::map<EntityId,monsters::Actor> actors;actors.emplace(body->id,std::move(replacement));
            auto units=std::make_shared<effects::AuraUnitPublication>();ports_.effects.stageAuraActors(*units,std::move(actors),0);
            ports_.effects.stageSummonEvents(*units,body->id,skill.sourceId,skill.rank,*owner->rules.skills->formulas,tick.tick);
            ports_.effects.stageAuraSource(*units,body->id,[target,tick=tick.tick]
                (monsters::Actor &unit,const CombatEffectSet &states,const effects::AuraUnitPublication::SourceMutation &mutation) {
                    attributes::refreshMonsterStatSources(unit,target,states,tick,mutation);
                    unit.petStats.attributes.maxLifeFixed=Fixed8::fromRaw(nativeStatValue(unit.nativeStats->values,*unit.rule.stats,"maxhp"));
                    unit.petStats.attributes.maxLife=std::max(1,unit.petStats.attributes.maxLifeFixed.raw()>>8);
                    const auto intrinsic=evaluateStatListWithSources(*unit.rule.stats,*unit.nativeStats->sources,{});
                    unit.petStats.attributes.combat=nativeCombatModifiers(intrinsic,*unit.rule.stats,unit.petStats.attributes.combat);
                });
            transactions::CharacterEdit edit{actor,owner->inventoryRevision,owner->characterRevision,owner->persistent.player};
            auto plan=ports_.transactions.prepare(std::move(edit),nullptr,{},units);
            if(!plan) return isDeferred(plan.status)?StepStatus::Blocked:StepStatus::Complete;
            const auto result=ports_.transactions.commit(std::move(*plan.value));
            if(!result) return isDeferred(result.status)?StepStatus::Blocked:StepStatus::Complete;
        } else {
            effects::AuraUnitPublication events;
            ports_.effects.stageSummonEvents(events,body->id,skill.sourceId,skill.rank,*owner->rules.skills->formulas,tick.tick);
            ports_.monsters.refreshNecro(body->id,std::move(updated.rule),std::move(updated.petStats),std::move(updated.necroPet),ready->second.weapon.item?std::optional{ready->second.weapon}:std::nullopt,std::move(updated.nativeStats),tick.tick);
            ports_.effects.prepareStagedSkillEvents(events,tick.tick);ports_.effects.publishAuraUnits(events);
        }
        cancel(owner->actor);
    }
    pet.ownerLevel=owner->persistent.player.level;pet.effectiveRanks=owner->totals.skillRanks;pet.hardRanks=owner->persistent.player.skillRanks;return StepStatus::Complete;
}
DomainResult<> System::revive(const ActorContext &actor,const SkillCastSpec &skill,EntityId id) {
    if(!ports_.monsters.consumableCorpse(id,actor.area,actor.tick,true)) return {DomainStatus::Unavailable,{}};
    const auto &owner=*ports_.players.find(actor.player);auto body=*ports_.monsters.find(id);
    const auto &spec=*skill.summon->necro;
    if(!skill.summon->native || !body.nativeStats) return {DomainStatus::Unavailable,{}};
    const bool native=owner.rules.skills && !owner.rules.skills->pd2;
    if(body.revision==UINT64_MAX || body.allegianceRevision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    if(native && body.aiThinkRevision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    std::map<EntityId,monsters::Actor> actors;bool landed=false;
    if(native) {
        const auto &area=ports_.areas.at(body.area);const auto global=body.position+area.definition.origin;
        const auto room=ports_.areas.roomAtPosition(body.area,body.position,int(global.x),int(global.y));
        const auto point=room?ports_.areas.nativeFreeCoordinates(*room,int(global.x),int(global.y),body.rule.size,0x1c09,
            ports_.spatial.nativeCollisionSample(0x1c09)):std::nullopt;
        body.reviveCorpseCollisionCleared=true;
        if(point) {
            landed=true;
            // Do58 enters NU before the HP roll, removes the old AIGENERAL
            // relationship and periodic skills, then teleports this same GUID.
            const auto oldOwner=ports_.monsters.minionOwner(id);
            if(oldOwner==id) {
                for(const auto child:body.nativeMinions) if(child!=id) if(const auto *live=ports_.monsters.find(child)) {
                    if(live->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
                    auto detached=*live;detached.nativeMinionOwner=EntityId{};detached.detachedMinion=true;++detached.revision;
                    actors.emplace(child,std::move(detached));
                }
                body.nativeMinions.clear();body.ownsNativeMinions=false;
            } else if(oldOwner) if(const auto *live=ports_.monsters.find(oldOwner)) {
                if(live->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
                auto previous=*live;std::erase(previous.nativeMinions,id);++previous.revision;
                actors.emplace(previous.id,std::move(previous));
            }
            body.deathMode=body.modeAlive=false;monsters::recordNativeMode(body,1);
            body.targetFlagsDisabled=false;body.nativeActionUntil=actor.tick;
            body.aiPose=1;body.holdPose=false;body.poseUntil=body.riseUntil=body.reactionUntil=0;
            body.area=point->room.area;body.position=point->position;
            body.revivalPosition=PointTarget{body.area,ports_.areas.at(body.area).generation,body.position};
            body.rightSkill.reset();
        }
    }
    body.nativeStats=attributes::prepareReviveStatSources(body,*skill.summon,actor.tick,ports_.effects.unitStatSources(id,actor.tick));
    body.owner=actor.player;body.necroSummon=true;body.necroPet=skill.summon->necro;body.hostile=false;++body.allegianceRevision;++body.revision;
    if(!native || landed) body.rule.noTreasure=body.rule.noExperience=true;
    body.rule.corpseSelectable=false;body.rule.petType=skill.summon->petType;
    body.petRegistrationOrder=0;body.inactivePet=body.petUnregistered=false;
    if(!native) body.collisionPattern=nativeUnitCollisionPattern(body.rule.size,!body.rule.curseAiInteract);
    body.petStats.attributes.maxLifeFixed=Fixed8::fromRaw(nativeStatValue(body.nativeStats->values,*body.rule.stats,"maxhp"));
    body.petStats.attributes.maxLife=std::max(1,body.petStats.attributes.maxLifeFixed.raw()>>8);
    body.petStats.attributes.combat=nativeCombatModifiers(body.nativeStats->values,*body.rule.stats,body.petStats.attributes.combat);
    body.petStats.damageRegen=body.rule.damageRegen;
    body.deathTick=body.deathOccurrence=0;body.killer={};body.corpse.unavailable=true;body.rewardComplete=true;
    body.busyUntil=actor.tick;body.neutralAt.reset();body.route.clear();body.moving=body.running=false;body.movementTarget={};
    if(!native) {
        body.poison.reset();body.wound.reset();body.curseControl={};body.attractionTarget={};body.attractionUntil=0;
        body.aiResetTimers.clear();body.nativeAiResetTracked=false;body.detachedMinion=true;body.aiFunctionRevision=0;body.aiThinkRevision=0;body.aiThinkAt=0;body.tauntTarget={};
    } else {
        body.nativeMinionOwner=actor.actor;body.detachedMinion=false;
        body.aiThinkAt=actor.tick+15;++body.aiThinkRevision;
        body.initialAiTimer=std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
            {{},{FrameEventUnit::Monster,id},FrameEventType::AiThink,actor.tick+15}));
        if(spec.lifetimeFrames>0) {
            if(std::find(body.rule.nativeModifiers.begin(),body.rule.nativeModifiers.end(),21)==body.rule.nativeModifiers.end()) body.rule.nativeModifiers.push_back(21);
            body.initialModifierTimers.push_back(std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
                {{},{FrameEventUnit::Monster,id},FrameEventType::MonsterModifier,actor.tick+uint64_t(spec.lifetimeFrames),false,effects::monsterModifierTimerHandler})));
        }
    }
    body.specialTargetOrder=0;body.mainOwnerCleared=false;
    body.wanderTargetOrder=0;
    body.chilledUntil=body.frozenUntil=body.stunnedUntil=body.knockedUntil=body.reactionUntil=0;
    actors.insert_or_assign(id,std::move(body));auto next=state_;
    Companion pet{id,actor.player,Kind::Summon,uint16_t(skill.sourceId)};pet.rank=skill.rank;pet.expires=!native && spec.lifetimeFrames>0?actor.tick+uint64_t(spec.lifetimeFrames):UINT64_MAX;pet.ownerTeleport=owner.teleportRevision;pet.nextDecision=actor.tick+15;pet.ownerLevel=owner.persistent.player.level;pet.effectiveRanks=owner.totals.skillRanks;pet.hardRanks=owner.persistent.player.skillRanks;next.companions.emplace(id,std::move(pet));
    size_t count=0;for(const auto &[key,existing]:next.companions) {(void)key;if(existing.owner==actor.player && existing.sourceSkill==uint16_t(skill.sourceId) && !existing.removeAt) ++count;}
    std::vector<EntityId> retire;for(auto &[key,existing]:next.companions) if(!native && count>size_t(skill.summon->limit) && key!=id && existing.owner==actor.player && existing.sourceSkill==uint16_t(skill.sourceId) && !existing.removeAt) {const auto *old=ports_.monsters.find(key);if(old) {existing.removeAt=actor.tick+uint64_t(old->rule.deathTicks);retire.push_back(key);--count;}}
    transactions::CharacterEdit edit{actor,owner.inventoryRevision,owner.characterRevision,owner.persistent.player};edit.player.mana-=skill.manaCost;edit.skillPayment=transactions::SkillManaPayment{skill.manaCost,skill.sourceId};edit.charge=skill.charge;
    edit.publicFacts.emplace_back(StateFact{id,1,actor.area,spec.reviveState.id,true});
    const auto visual=spec.reviveVisuals.at(size_t(std::clamp(actors.at(id).rule.size,1,3)-1));edit.publicFacts.emplace_back(MissileFact{id,1,actor.area,visual.first,skill.rank,0,actors.at(id).position,actors.at(id).position});
    auto events=std::make_shared<effects::AuraUnitPublication>();ports_.effects.stageAuraActors(*events,std::move(actors),0);
    if(native && landed) events->skillTimers.insert_or_assign(id,ports_.frameEvents.prepareEdit({FrameEventUnit::Monster,id},FrameEventType::PeriodicSkills,{}));
    ports_.effects.stageSummonEvents(*events,id,skill.sourceId,skill.rank,*owner.rules.skills->formulas,actor.tick);
    const auto retired=native?ports_.effects.stagePetLimit(*events,actor.player,{skill.summon->petType,std::max(0,skill.summon->limit-1)},actor.tick,false):
        ports_.monsters.stageRetirements(*events,retire,actor.tick);if(!retired) return retired;
    auto plan=ports_.transactions.prepare(std::move(edit),nullptr,{},events);if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));if(!result) return result;
    state_.companions.swap(next.companions);
    return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::synchronizeIronGolems(TickContext tick) {
    bool blocked=false;
    for(const auto &[key,owner]:ports_.players.all()) {
        (void)key;if(!owner.entered || !owner.persistent.ironGolem || !owner.rules.skills) continue;
        const auto &area=ports_.areas.at(owner.area);const ActorContext actor{owner.player,owner.actor,owner.area,area.generation,UINT64_MAX-1,tick.tick};
        bool present=false,dead=owner.persistent.player.hp<=0;
        for(const auto &[id,pet]:state_.companions) if(pet.owner==owner.player && pet.kind==Kind::IronGolem) {
            const auto *body=ports_.monsters.find(id);
            if(body && body->necroMaterial && body->necroMaterial->id==owner.persistent.ironGolem->id) {present=true;dead|=!body->alive() || bool(pet.removeAt);}
        }
        if(dead) {
            transactions::CharacterEdit edit{actor,owner.inventoryRevision,owner.characterRevision,owner.persistent.player};edit.ironGolem=std::optional<ItemInstance>{};
            auto plan=ports_.transactions.prepare(std::move(edit));if(!plan || !ports_.transactions.commit(std::move(*plan.value))) blocked=true;continue;
        }
        if(present || ports_.skills.busy(owner.actor,tick.tick)) continue;
        const auto rank=owner.totals.skillRanks.find(90);if(rank==owner.totals.skillRanks.end() || rank->second<=0 || !owner.rules.skills->definitions.contains(90)) continue;
        auto skill=skills::evaluate(owner,90,rank->second);skill.manaCost=skill.startMana=0;skill.charge.reset();
        if(isDeferred(necro(actor,skill,owner.position,{},true).status)) blocked=true;
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
DomainResult<> System::necro(const ActorContext &actor,const SkillCastSpec &original,Vec target,EntityId corpse,bool restoring,
    std::shared_ptr<CorpseSummonExecution> *continuation) {
    auto skill=original;
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,actor.actor);
    const auto *owner=caster?caster->player:nullptr;const auto *area=ports_.areas.find(actor.area);
    if(!caster || !caster->programs() || (!owner && (!skill.bone || !skill.bone->barrier))) return {DomainStatus::Unavailable,{}};
    const auto &programs=*caster->programs();const int difficulty=owner?int(owner->persistent.difficulty):caster->monster->rule.difficulty;
    const auto ranks=caster->formulaInput(ports_.effects,actor.tick);
    const auto &hardRanks=ranks.baseRanks;const auto &effectiveRanks=ranks.effectiveRanks;
    const auto revision=owner?owner->inventoryRevision:0;
    const auto ownerLevel=caster->level();const auto origin=caster->position();
    if(skill.bone && skill.bone->barrier) {
        SummonCastSpec wall;wall.monster="bonewall";wall.kind=MonsterKind::BoneWall;wall.corpse=false;wall.limit=INT32_MAX;
        wall.stats=skill.bone->barrierStats.at(size_t(difficulty));
        if(!skill.bone->barrierNative) return {DomainStatus::Unavailable,{}};
        wall.native=*skill.bone->barrierNative;wall.native->base=wall.stats;skill.summon=std::move(wall);
    }
    if((owner?(!owner->entered || owner->persistent.player.hp<=0):!caster->monster->alive()) || caster->area()!=actor.area || !area || area->generation!=actor.areaGeneration || !skill.summon || skill.summon->amazon) return {DomainStatus::InvalidActor,{}};
    const auto &summon=*skill.summon;
    if(summon.necro && summon.necro->kind==NecroSummonKind::Revive) return revive(actor,skill,corpse);
    const bool iron=summon.necro && summon.necro->kind==NecroSummonKind::Iron;
    const auto material=iron?(restoring?owner->persistent.ironGolem:ports_.inventory.ironGolemMaterial(actor,target,corpse)):std::optional<ItemInstance>{};
    if(iron && !material) return {DomainStatus::Unavailable,{}};
    const bool nativeRaise=summon.corpse && !programs.pd2 && programs.formulas->skills.at(skill.sourceId).number("srvdofunc")==31;
    std::shared_ptr<CorpseSummonExecution> local;auto &execution=continuation?*continuation:local;
    if(nativeRaise) {
        if(!execution) {
            if(summon.monster.empty() || !programs.petTypeBaseLimits.contains(summon.petType)) return {DomainStatus::Unavailable,{}};
            auto next=std::make_shared<CorpseSummonExecution>();next->corpse=corpse;
            const auto removed=ports_.monsters.removeConsumableCorpse(corpse,actor.area,actor.tick);
            if(!removed) return {removed.status,{}};
            next->position=*removed.value;execution=std::move(next);
        }
        if(execution->corpse!=corpse) return {DomainStatus::InvalidRequest,{}};
        target=execution->position;
    } else if(summon.corpse && !ports_.monsters.consumableCorpse(corpse,actor.area,actor.tick)) return {DomainStatus::Unavailable,{}};
    auto prior=pending_.find(actor.actor);
    if(prior!=pending_.end() && (prior->second.actor.sequence!=actor.sequence || prior->second.skill!=skill.sourceId)) {cancel(actor.actor);prior=pending_.end();}
    if(prior==pending_.end()) {
        if(nextPreparation_==UINT64_MAX) return {DomainStatus::Capacity,{}};
        auto random=ports_.random;Preparation source{actor,revision,childRandom(random),skill.sourceId,skill.rank,difficulty,ownerLevel,hardRanks,summon,{}};
        source.material=material;
        source.birthRandom=std::pair{ports_.random,random};
        source.effectiveRanks=effectiveRanks;source.token=nextPreparation_++;
        if(summon.kind!=MonsterKind::BoneWall) {
            source.ownerRandom=caster->random();
            const auto palettes=ports_.monsters.read().componentPalettes.find(actor.area);
            source.componentPalettes=palettes==ports_.monsters.read().componentPalettes.end()?area->definition.componentPalettes:palettes->second;
        }
        pending_.emplace(actor.actor,std::move(source));return {DomainStatus::Capacity,{}};
    }
    if(prior->second.rank!=skill.rank || prior->second.effectiveRanks!=effectiveRanks) {cancel(actor.actor);return necro(actor,original,target,corpse,restoring,&execution);}
    if(prior->second.inventoryRevision!=revision || prior->second.ownerLevel!=ownerLevel || prior->second.hardRanks!=hardRanks || prior->second.actor.area!=actor.area || prior->second.actor.areaGeneration!=actor.areaGeneration) {cancel(actor.actor);return {DomainStatus::Stale,{}};}
    if(iron && (!prior->second.material || prior->second.material->id!=material->id || prior->second.material->revision!=material->revision)) {cancel(actor.actor);return {DomainStatus::Stale,{}};}
    if((prior->second.birthRandom && prior->second.birthRandom->first!=ports_.random) ||
        (prior->second.ownerRandom && *prior->second.ownerRandom!=caster->random())) {
        cancel(actor.actor);return necro(actor,original,target,corpse,restoring,&execution);
    }
    if(prior->second.componentPalettes) {
        const auto palettes=ports_.monsters.read().componentPalettes.find(actor.area);
        const auto &current=palettes==ports_.monsters.read().componentPalettes.end()?area->definition.componentPalettes:palettes->second;
        if(current!=*prior->second.componentPalettes) {cancel(actor.actor);return necro(actor,original,target,corpse,restoring,&execution);}
    }
    const auto ready=prepared_.find(actor.actor);if(ready==prepared_.end()) return {DomainStatus::Capacity,{}};
    if(!ready->second.deferred.empty()) {cancel(actor.actor);return {DomainStatus::Unavailable,{}};}
    const auto &rule=ready->second.rule;std::vector<Vec> positions;
    std::map<RoomView,RoomRandomLoan> roomRandoms;
    const auto collision=ports_.spatial.nativeCollisionSample();
    const auto sample=[&](RoomView room,int32_t column,int32_t row) {
        auto mask=collision(room,column,row);
        for(const auto position:positions) {
            const auto global=position+area->definition.origin;
            mask|=nativeCollisionFootprint(rule.collisionPattern,column-int32_t(std::floor(global.x)),
                row-int32_t(std::floor(global.y)),0x100);
        }
        return mask;
    };
    const auto place=[&](Vec center)->DomainResult<bool> {
        const auto global=center+area->definition.origin;
        const auto room=ports_.areas.roomAtPosition(actor.area,origin,int32_t(std::floor(global.x)),int32_t(std::floor(global.y)));
        if(!room || room->area!=actor.area) return {DomainStatus::Applied,false};
        auto loan=roomRandoms.find(*room);
        if(loan==roomRandoms.end()) {
            const auto borrowed=ports_.areas.roomRandom(*room);if(!borrowed) return {borrowed.status,{}};
            loan=roomRandoms.emplace(*room,*borrowed.value).first;
        }
        monsters::Admission admission;admission.identity.monster=summon.monster;admission.identity.spawnKey="summon.search";
        admission.area=actor.area;admission.position=center;admission.rule=rule;admission.tick=actor.tick;
        auto point=ports_.monsters.searchRoomSpawn(admission,loan->second,-1,sample);
        if(!point) return {point.status,{}};
        if(!*point.value) point=ports_.monsters.searchRoomSpawn(admission,loan->second,4,sample);
        if(!point) return {point.status,{}};
        if(!*point.value) return {DomainStatus::Applied,false};
        positions.push_back(**point.value);
        return {DomainStatus::Applied,true};
    };
    if(skill.bone && skill.bone->prison) {
        constexpr Vec offsets[]{{-1,-4},{1,-4},{3,-3},{4,-1},{4,1},{3,3},{-1,4},{1,4},{-3,3},{-4,-1},{-4,1},{-3,-3}};
        for(const auto offset:offsets) {const auto placed=place(target+offset);if(!placed) return {placed.status,{}};}
    } else {
        const auto placed=place(summon.corpse && !nativeRaise?ports_.monsters.find(corpse)->position:target);
        if(!placed) return {placed.status,{}};
    }
    if(positions.empty() && !(skill.bone && skill.bone->prison)) {
        for(const auto &[room,loan]:roomRandoms) {
            (void)room;const auto consumed=ports_.transactions.consumeRoomRandom(loan);if(!consumed) return consumed;
        }
        return {DomainStatus::Unavailable,{}};
    }
    std::map<EntityId,monsters::Actor> actors;auto random=ready->second.random;
    auto gameRandom=prior->second.birthRandom->second;
    auto ownerRandom=ready->second.ownerRandom;std::optional<bool> shield;
    if(ownerRandom) {
        shield=false;
        if(summon.kind==MonsterKind::NecroSkeleton && skill.rank>2) shield=int(limitedRandom(*ownerRandom,100))<summon.shieldChance;
    }
    for(const auto position:positions) {
        auto components=ready->second.components;
        if(summon.kind==MonsterKind::BoneWall) {
            random=actors.empty()?prior->second.seed:childRandom(gameRandom);
            components=chooseMonsterComponents(rule.componentCounts,{},random);
            limitedRandom(random,1);
        }
        auto candidate=ports_.monsters.prepareNecro(actor,rule,position,ready->second.summon,
            random,actors.size(),ready->second.equipmentSources,components,shield);if(!candidate) return {candidate.status,{}};
        if(iron) {auto &body=candidate.value->begin()->second;body.necroMaterial=material;body.necroMaterialRestoration=ready->second.materialRestoration;body.petWeapon=ready->second.weapon.item?std::optional{ready->second.weapon}:std::nullopt;}
        actors.merge(*candidate.value);}
    const EntityId id=actors.empty()?EntityId{}:actors.begin()->first;
    if(id && skill.bone && skill.bone->prison) {
        auto &root=actors.at(id);
        for(auto &[key,body]:actors) if(key!=id) {body.nativeMinionOwner=id;root.nativeMinions.insert(root.nativeMinions.begin(),key);}
    }
    const uint64_t expires=skill.bone && skill.bone->barrier?actor.tick+uint64_t(rule.summonLifeFrames):UINT64_MAX;
    auto next=state_;
    for(const auto &[key,body]:actors) {
        Companion pet{key,owner?actor.player:PlayerId{},iron?Kind::IronGolem:Kind::Summon,uint16_t(skill.sourceId)};pet.rank=skill.rank;pet.expires=expires;pet.nextDecision=actor.tick+(ready->second.components || (skill.bone && skill.bone->barrier)?25:1);pet.ownerTeleport=owner?owner->teleportRevision:0;pet.ownerLevel=ownerLevel;pet.effectiveRanks=effectiveRanks;pet.hardRanks=hardRanks;
        if(!programs.pd2) {
            pet.necroInitialized=true;pet.observedNecroAiFunctionRevision=body.aiFunctionRevision;
            if(body.necroPet && body.necroPet->kind==NecroSummonKind::Mage) pet.necroAi={1,0,-666};
        }
        next.companions.emplace(key,std::move(pet));
    }
    const auto sameType=[&](const Companion &existing) {
        if(existing.sourceSkill==uint16_t(skill.sourceId)) return true;
        if(programs.pd2) {
            const auto *body=ports_.monsters.find(existing.actor);
            return body && body->rule.petType==ready->second.summon.petType;
        }
        if(!summon.golem || !existing.sourceSkill) return false;
        const auto found=programs.definitions.find(*existing.sourceSkill);
        return found!=programs.definitions.end() && found->second.spec.summon && found->second.spec.summon->golem;
    };
    size_t count=0;for(const auto &[key,existing]:next.companions) {(void)key;if(existing.owner==actor.player && sameType(existing) && !existing.removeAt) ++count;}
    std::vector<EntityId> retire;
    for(auto &[key,existing]:next.companions) {
        if(!programs.pd2) break;
        if(count<=size_t(summon.limit)) break;
        if(!owner || existing.owner!=actor.player || !sameType(existing) || existing.removeAt) continue;
        const auto *body=ports_.monsters.find(key);if(!body) continue;
        existing.removeAt=actor.tick+uint64_t(body->rule.deathTicks);existing.release=0;retire.push_back(key);--count;
    }
    auto auraUnits=std::make_shared<effects::AuraUnitPublication>();
    ports_.effects.stageAuraActors(*auraUnits,std::move(actors),positions.size());
    for(const auto &[eventId,body]:auraUnits->actors->actors) {
        (void)body;ports_.effects.stageSummonEvents(*auraUnits,eventId,skill.sourceId,skill.rank,*programs.formulas,actor.tick);
    }
    if(iron) {
        const auto sourceProgram=ready->second.summon;
        const auto materialSources=ready->second.equipmentSources;
        const auto time=ports_.areas.statTime(actor.area);
        ports_.effects.stageAuraSource(*auraUnits,id,[sourceProgram,materialSources,time,tick=actor.tick]
            (monsters::Actor &body,const CombatEffectSet &,const effects::AuraUnitPublication::SourceMutation &mutation) {
                StatList binding;binding.ownerKind=StatOwnerKind::Monster;binding.source.owner=EntityId{1};binding.contributions=materialSources;
                const auto bound=statListWithOwner(binding,body.id);
                body.nativeStats=attributes::prepareSummonStatSources(body,sourceProgram,bound->contributions,time,tick,mutation);
                body.petStats.attributes.maxLifeFixed=Fixed8::fromRaw(nativeStatValue(body.nativeStats->values,*body.rule.stats,"maxhp"));
                body.petStats.attributes.maxLife=std::max(1,body.petStats.attributes.maxLifeFixed.raw()>>8);
                const auto intrinsic=evaluateStatListWithSources(*body.rule.stats,*body.nativeStats->sources,{});
                body.petStats.attributes.combat=nativeCombatModifiers(intrinsic,*body.rule.stats,body.petStats.attributes.combat);
            });
    }
    const auto retired=owner && !programs.pd2 && summon.kind!=MonsterKind::BoneWall?
        ports_.effects.stagePetLimit(*auraUnits,actor.player,{summon.petType,std::max(0,summon.limit-int(positions.size()))},actor.tick,false):
        ports_.monsters.stageRetirements(*auraUnits,retire,actor.tick);if(!retired) return retired;
    std::shared_ptr<missiles::NativeBirthPublication> makers;
    if(skill.bone && skill.bone->barrier && !skill.bone->prison && skill.bone->sideCount>1) {
        auto direction=missileWallDirection(origin,positions.front());if(!direction.length()) direction={0,1};
        std::array<missiles::NativeSpawn,2> births;
        for(size_t index=0;index<births.size();++index) {
            auto &birth=births[index];birth.owner=actor.actor;birth.area=actor.area;birth.generation=actor.areaGeneration;birth.tick=actor.tick;
            birth.position=positions.front();birth.target=birth.position+direction*(index?-1.f:1.f);
            birth.definition=skill.missileId;birth.skill=skill.sourceId;birth.rank=skill.rank;
            birth.parameters={int32_t(uint32_t(id.value)),skill.bone->sideCount};
        }
        auto prepared=ports_.missiles.prepareNativeBirth(births,positions.size(),{},std::pair{prior->second.birthRandom->first,gameRandom});if(!prepared) return {prepared.status,{}};
        makers=std::move(*prepared.value);
    }
    const bool missileRandom=bool(makers);
    DomainResult<> debit;
    monsters::NativeBirthBatch components;
    if(ready->second.componentPalettes) components.palettes.emplace(actor.area,*ready->second.componentPalettes);
    if(iron && !restoring) {
        auto cost=ports_.inventory.ironGolemCost(actor,skill,*material,auraUnits);if(!cost) return {cost.status,{}};
        if(!positions.empty()) cost.value->gameRandom=transactions::UnitRandomEdit{prior->second.birthRandom->first,gameRandom};
        for(const auto &[room,loan]:roomRandoms) {(void)room;cost.value->roomRandomLoans.push_back(std::make_shared<const RoomRandomLoan>(loan));}
        debit=ports_.transactions.commit(std::move(*cost.value));
    } else if(!owner) {
        ports_.effects.prepareUnitTimers(*auraUnits);ports_.effects.prepareStagedSkillEvents(*auraUnits,actor.tick);
        if(ports_.random!=prior->second.birthRandom->first || !ports_.effects.validateAuraUnits(*auraUnits) ||
            (makers && !makers->validate(positions.size()))) return {DomainStatus::Stale,{}};
        for(const auto &[room,loan]:roomRandoms) {(void)room;if(!ports_.areas.validateRoomRandom(loan)) return {DomainStatus::Stale,{}};}
        auto facts=auraUnits->facts;
        if(makers) facts.insert(facts.end(),makers->facts().begin(),makers->facts().end());
        const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},std::move(facts)});
        if(!sent) return {sent.status,{}};
        ports_.effects.publishAuraUnits(*auraUnits);
        if(makers) makers->publish();else if(!positions.empty()) ports_.random=gameRandom;
        for(const auto &[room,loan]:roomRandoms) {(void)room;if(!ports_.transactions.consumeRoomRandom(loan)) std::terminate();}
        debit={DomainStatus::Applied,std::monostate{}};
    } else {
        transactions::CharacterEdit edit{actor,revision,owner->characterRevision,owner->persistent.player};
        edit.player.mana-=skill.manaCost;edit.skillPayment=transactions::SkillManaPayment{skill.manaCost,skill.sourceId};edit.charge=skill.charge;
        if(ownerRandom) edit.random=transactions::UnitRandomEdit{caster->random(),*ownerRandom};
        if(summon.golem && !iron && owner->persistent.ironGolem && !programs.pd2) edit.ironGolem=std::optional<ItemInstance>{};
        auto cost=ports_.transactions.prepare(std::move(edit),nullptr,{},auraUnits);if(!cost) return {cost.status,{}};
        cost.value->missileBirth=std::move(makers);
        if(!positions.empty() && !missileRandom) cost.value->gameRandom=transactions::UnitRandomEdit{prior->second.birthRandom->first,gameRandom};
        for(const auto &[room,loan]:roomRandoms) {(void)room;cost.value->roomRandomLoans.push_back(std::make_shared<const RoomRandomLoan>(loan));}
        debit=ports_.transactions.commit(std::move(*cost.value));
    }
    if(!debit) return debit;
    ports_.monsters.publishBirthComponents(components);
    state_.companions.swap(next.companions);
    synchronizeMaterialRestoration(actor.tick);
    if(summon.corpse && !nativeRaise) ports_.monsters.consumeCorpse(corpse);
    cancel(actor.actor);
    return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::nativeNecroStep(Companion &pet,TickContext tick) {
    const auto *body=ports_.monsters.find(pet.actor);const auto *owner=ports_.players.find(pet.owner);
    if(!body || !body->alive() || !owner || !owner->entered || !owner->rules.skills ||
        tick.tick<pet.nextDecision || tick.tick<body->busyUntil || ports_.monsters.frozen(body->id,tick.tick) || body->stunnedUntil>tick.tick) return StepStatus::Complete;
    if(!ports_.events.hasCapacity(8,8)) return StepStatus::Blocked;
    if(!pet.necroInitialized) {
        // SkillNec::SetUnitComponent initializes only the mage's Fn067 branch.
        pet.necroAi=body->necroPet && body->necroPet->kind==NecroSummonKind::Mage?std::array<int64_t,3>{1,0,-666}:std::array<int64_t,3>{};
        pet.necroInitialized=true;pet.observedNecroAiFunctionRevision=body->aiFunctionRevision;
    } else if(pet.observedNecroAiFunctionRevision!=body->aiFunctionRevision) {
        pet.necroAi.fill(0);pet.observedNecroAiFunctionRevision=body->aiFunctionRevision;
    }
    const auto &program=*owner->rules.skills;
    const auto &area=ports_.areas.at(body->area);const auto &ownerArea=ports_.areas.at(owner->area);
    const auto ownerPoint=owner->position+ownerArea.definition.origin-area.definition.origin;
    const auto distance=[&](Vec point) {return unitCoordinateDistanceWithSize(int(body->position.x),int(body->position.y),int(point.x),int(point.y),body->rule.size);};
    const auto idle=[&] {ports_.monsters.neutralMode(body->id);pet.nextDecision=tick.tick+10;};
    const auto follow=[&](const PlayerState &leader,EntityId target,bool combat,int motion=-1,int velocity=0,int spacing=7,bool suppress=false) {
        return followNativePet(ports_,pet,leader,*body,target,combat,tick.tick,
            {program.nativePetProbeClass,program.nativePetProbeCollision,motion,velocity,spacing,false,suppress,true});
    };
    pet.nextDecision=tick.tick+1;
    if(!body->route.empty()) return StepStatus::Complete;
    if(body->detachedMinion || !body->nativeMinionOwner || *body->nativeMinionOwner!=owner->actor) {
        const auto *previous=pet.necroAi[2]?ports_.players.findActor(EntityId{uint32_t(pet.necroAi[2])}):nullptr;
        if(previous && previous->entered) follow(*previous,{},false,3);else idle();
        return StepStatus::Complete;
    }
    pet.necroAi[2]=owner->actor.value;
    const bool mage=pet.necroAi[0]!=0;
    if(!mage) {
        if(distance(ownerPoint)>50) {follow(*owner,{},false,3);return StepStatus::Complete;}
        const int velocity=body->rule.nativeVelocity<=0?100:std::min(100*body->rule.nativeRunVelocity/body->rule.nativeVelocity-100,100);
        if(distance(ownerPoint)>28) {follow(*owner,{},false,0,velocity);return StepStatus::Complete;}
    }
    const ai::NativeAiTargetPorts targets{ports_.monsters,ports_.players,ports_.areas,ports_.social};
    auto potential=ai::acquireNativeOwnerTarget(targets,*owner);
    if(potential) {
        if(const auto *unit=ports_.monsters.find(potential->unit.id);unit && unit->rule.petIgnore) potential.reset();
        else potential->position=potential->position+(ownerArea.definition.origin-area.definition.origin);
    }
    auto target=ai::acquireNativeAiTarget(targets,*body,tick,mage,false,mage?0:24);
    // Fn067 retains the original acquisition's bCombat even if it later
    // substitutes the owner's potential target.
    const bool combat=target && meleeDistance(body->position,body->rule.size,target->position,target->size)<=body->rule.meleeRange;
    if(!target || target->distance>(mage?15:6)) {
        target.reset();
        if(potential && distance(potential->position)<(mage?20:36) &&
            (!mage || area.definition.collision.missileSegment(body->position,potential->position,{4,1}))) target=potential;
    }
    if(target && !mage) {
        // AiUtil::sub_6FCF14D0 accepts any of three rays, including the two
        // perpendicular offsets. WALL | MISSILE_BARRIER | DOOR is 0x805.
        const auto point=target->position;const int range=distance(point),offset=range<3?2:range<11?3:range<25?4:3;
        const int x=((int(body->position.x)>int(point.x))-(int(body->position.x)<int(point.x)))*offset;
        const int y=((int(body->position.y)>int(point.y))-(int(body->position.y)<int(point.y)))*offset;
        const auto clear=[&](Vec to) {return area.definition.collision.missileSegment(body->position,to,{0x805,1});};
        if(!clear(point) && !clear(point+Vec{float(-y),float(x)}) && !clear(point+Vec{float(y),float(-x)})) target.reset();
    }
    const auto chance=[&](int threshold) {
        auto random=ports_.monsters.find(body->id)->combatRandom;const bool selected=int(rollRandom(random)%100)<threshold;
        ports_.monsters.commitCombatRandom(body->id,random);return selected;
    };
    const bool suppress=chance(15);
    if(follow(*owner,target?target->unit.id:EntityId{},combat,-1,0,suppress?8:7,suppress)) return StepStatus::Complete;
    const auto move=[&](Vec point,EntityId unit={},int maximum=100,int circle=0) {
        monsters::MoveRequest request{body->id,{body->area,area.generation,point},unit,0,75,false};
        request.pathType=13;request.pathDistance=uint8_t(maximum);request.pathSteps=1;
        request.circleTurn=circle;request.circleDistance=circle?3:0;
        return ports_.monsters.requestMove(request);
    };
    if(!target || area.definition.town) {
        auto random=ports_.monsters.find(body->id)->combatRandom;
        const auto point=monsterWanderPoint(ownerPoint,4,random)-Vec{.5f,.5f};
        ports_.monsters.commitCombatRandom(body->id,random);move(point);return StepStatus::Complete;
    }
    if(mage || combat) {
        if(chance(80)) {
            const int skill=mage?body->rule.skillIds[0]:0;
            const auto action=body->rule.skillActions.find(uint16_t(skill));
            if(mage && (skill<0 || action==body->rule.skillActions.end())) {idle();return StepStatus::Complete;}
            pet.pendingNecroAction=true;pet.pendingNecroUseSkill=mage;
            const auto accepted=requestAction(pet,{body->id,uint16_t(skill),target->unit,tick.tick,uint8_t(mage?action->second.nativeMode:4)});
            if(!pet.pendingAction) {
                pet.pendingNecroAction=pet.pendingNecroUseSkill=false;
                if(!accepted) {if(mage) idle();else pet.cancelAi=true;}
            }
        } else if(mage && !chance(75)) {
            auto random=ports_.monsters.find(body->id)->combatRandom;const int turn=(rollRandom(random)&255)<128?1:-1;
            ports_.monsters.commitCombatRandom(body->id,random);move(target->position,target->unit.id,3,turn);
        } else idle();
    } else move(target->position,target->unit.id,12);
    return StepStatus::Complete;
}
StepStatus System::necroStep(Companion &pet,TickContext tick) {
    const auto *body=ports_.monsters.find(pet.actor);const auto *owner=ports_.players.find(pet.owner);
    if(!body || !owner || !owner->entered || owner->persistent.player.hp<=0 || body->implementation==MonsterKind::BoneWall || tick.tick<body->busyUntil || ports_.monsters.frozen(body->id,tick.tick) || body->stunnedUntil>tick.tick) return StepStatus::Complete;
    if(owner->rules.skills && !owner->rules.skills->pd2) {
        // Revive keeps its family AI; AICORE's main-owner leash runs there.
        const auto special=body->curseControl.ai;
        const auto control=ports_.ai.read().controllers.find(body->id);
        const bool pending=control!=ports_.ai.read().controllers.end() && !control->second.pending.empty();
        if(body->revived() || pending || special==CurseAi::DimVision || special==CurseAi::Terror || special==CurseAi::Taunt) {
            pet.nextDecision=tick.tick+1;pet.nativePetMoving=false;return StepStatus::Complete;
        }
        return nativeNecroStep(pet,tick);
    }
    const auto &area=ports_.areas.at(owner->area);const ActorContext context{owner->player,owner->actor,owner->area,area.generation,0,tick.tick};
    if(body->area!=owner->area || missileDistance(body->position,owner->position)>50 || pet.ownerTeleport!=owner->teleportRevision) {
        for(int radius=1;radius<=4;++radius) for(int x=-radius;x<=radius;++x) for(int y=-radius;y<=radius;++y) {
            if(std::max(std::abs(x),std::abs(y))!=radius) continue;
            if(ports_.monsters.warpPet(pet.actor,context,{std::floor(owner->position.x)+x+.5f,std::floor(owner->position.y)+y+.5f})) {pet.ownerTeleport=owner->teleportRevision;pet.nativePetMoving=false;pet.nextDecision=tick.tick+1;return StepStatus::Complete;}
        }
        return StepStatus::Complete;
    }
    if(body->necroPet && body->necroPet->aura && !ports_.effects.hirelingAuraActive(body->id,body->necroPet->aura->skill,body->necroPet->aura->rank))
        if(isDeferred(ports_.effects.hirelingAura(body->id,*body->necroPet->aura,tick.tick).status)) return StepStatus::Blocked;
    if(tick.tick<pet.nextDecision) return StepStatus::Complete;
    // Preserve both pet follow paths and a Revive's original family movement.
    // Warp, aura and lifecycle handling remain outside this decision boundary.
    if(!body->route.empty()) {pet.nextDecision=tick.tick+1;return StepStatus::Complete;}
    pet.nativePetMoving=false;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    EntityId target;int nearest=36;
    for(const auto id:participants.enemies(body->id,body->area)) {
        const auto candidate=participants.find(id);
        const int distance=missileDistance(body->position,candidate.position());
        if(distance<nearest && area.definition.collision.segment(body->position,candidate.position())) {nearest=distance;target=id;}
    }
    if(target) {
        if(body->revived()) return StepStatus::Complete; // Its prepared original family AI owns combat decisions.
        const auto enemy=participants.find(target);
        const bool mage=body->necroPet && (body->necroPet->kind==NecroSummonKind::Mage || body->necroPet->kind==NecroSummonKind::Archer);
        if((mage && missileDistance(body->position,enemy.position())<=25) || meleeDistance(body->position,body->rule.size,enemy.position(),enemy.size())<=body->rule.meleeRange) {
            ports_.monsters.stop(pet.actor);
            const uint16_t skill=mage && !body->rule.skillActions.empty()?body->rule.skillActions.begin()->first:0;
            const auto result=requestAction(pet,{pet.actor,skill,UnitTarget{target,0,uint8_t(enemy.player?0:1)},tick.tick});
            if(isDeferred(result.status)) return StepStatus::Blocked;
        } else ports_.monsters.requestMove({pet.actor,{body->area,area.generation,enemy.position()},target,body->rule.meleeRange,75,false});
    } else if(missileDistance(body->position,owner->position)>4) {
        const auto moved=ports_.monsters.requestMove({pet.actor,{body->area,area.generation,owner->position},owner->actor,4,75,false});
        if(moved && body->revived()) pet.nativePetMoving=true;
    }
    pet.nextDecision=tick.tick+uint64_t(std::max(1,body->rule.decisionTicks));return StepStatus::Complete;
}
DomainResult<bool> System::growBoneWall(EntityId source,EntityId root,int skillId,int rank,RegionId region,Vec position,uint64_t tick) {
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,source);
    const auto *owner=caster?caster->player:nullptr;const auto *parent=ports_.monsters.find(root);const auto *area=ports_.areas.find(region);
    if(!caster || !caster->programs() || !parent || !area || parent->implementation!=MonsterKind::BoneWall)
        return {DomainStatus::Unavailable,{}};
    const auto skill=skills::evaluateNativeSkill(*caster,ports_.effects,skillId,rank,tick);
    if(!skill.bone || !skill.bone->barrierNative) return {DomainStatus::Unavailable,{}};
    const auto &rule=parent->rule;
    position={std::floor(position.x)+.5f,std::floor(position.y)+.5f};
    const auto global=position+area->definition.origin;
    const auto room=ports_.areas.roomAtPosition(caster->area(),caster->position(),int32_t(std::floor(global.x)),int32_t(std::floor(global.y)));
    if(!room || room->area!=region) return {DomainStatus::Applied,false};
    auto roomRandom=ports_.areas.roomRandom(*room);if(!roomRandom) return {roomRandom.status,{}};
    const auto collision=ports_.spatial.nativeCollisionSample();
    monsters::Admission admission;admission.identity.monster="bonewall";admission.identity.spawnKey="bonewall.search";
    admission.area=region;admission.position=position;admission.rule=rule;admission.tick=tick;
    const auto point=ports_.monsters.searchRoomSpawn(admission,*roomRandom.value,-1,collision);
    if(!point) return {point.status,{}};
    if(!*point.value) {
        const auto consumed=ports_.transactions.consumeRoomRandom(*roomRandom.value);
        return {consumed?DomainStatus::Applied:consumed.status,consumed?std::optional{false}:std::nullopt};
    }
    position=**point.value;
    if(parent->revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    SummonCastSpec summon;summon.monster="bonewall";summon.kind=MonsterKind::BoneWall;summon.limit=INT32_MAX;summon.petType=rule.petType;
    summon.stats=skill.bone->barrierStats.at(size_t(owner?int(owner->persistent.difficulty):caster->monster->rule.difficulty));summon.native=*skill.bone->barrierNative;summon.native->base=summon.stats;
    summon.native->initialLife=summon.stats.attributes.maxLifeFixed;
    ActorContext context{owner?owner->player:PlayerId{},source,region,area->generation,0,tick};
    const auto expectedRandom=ports_.random;
    auto random=expectedRandom,seed=childRandom(random);
    const auto components=chooseMonsterComponents(rule.componentCounts,{},seed);limitedRandom(seed,1);
    auto prepared=ports_.monsters.prepareNecro(context,rule,position,summon,seed,0,{},components);if(!prepared) return {prepared.status,{}};
    const auto id=prepared.value->begin()->first;auto &body=prepared.value->begin()->second;body.nativeMinionOwner=root;
    auto updated=*parent;++updated.revision;updated.nativeMinions.insert(updated.nativeMinions.begin(),id);
    prepared.value->emplace(root,std::move(updated));
    auto next=state_;Companion pet{id,owner?owner->player:PlayerId{},Kind::Summon,uint16_t(skillId)};
    pet.rank=rank;pet.expires=tick+uint64_t(rule.summonLifeFrames);pet.nextDecision=tick+25;
    pet.ownerTeleport=owner?owner->teleportRevision:0;pet.ownerLevel=caster->level();
    const auto ranks=caster->formulaInput(ports_.effects,tick);pet.effectiveRanks=ranks.effectiveRanks;pet.hardRanks=ranks.baseRanks;next.companions.emplace(id,std::move(pet));
    effects::AuraUnitPublication publication;ports_.effects.stageAuraActors(publication,std::move(*prepared.value),1);
    ports_.effects.stageSummonEvents(publication,id,skillId,rank,*caster->programs()->formulas,tick);
    ports_.effects.prepareStagedSkillEvents(publication,tick);
    if(ports_.random!=expectedRandom || !ports_.effects.validateAuraUnits(publication) || !ports_.areas.validateRoomRandom(*roomRandom.value)) return {DomainStatus::Stale,{}};
    if(!publication.facts.empty()) {const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},region},std::move(publication.facts)});if(!sent) return {sent.status,{}};}
    ports_.effects.publishAuraUnits(publication);state_.companions.swap(next.companions);ports_.random=random;
    const auto consumed=ports_.transactions.consumeRoomRandom(*roomRandom.value);if(!consumed) std::terminate();
    return {DomainStatus::Applied,true};
}
}
