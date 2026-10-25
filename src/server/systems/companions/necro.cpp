#include "system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/skills/evaluation.hpp"
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
#include <algorithm>
#include <cmath>
namespace d2x::server::companions {
StepStatus System::refreshNecro(Companion &pet,TickContext tick) {
    const auto *owner=ports_.players.find(pet.owner);const auto *body=ports_.monsters.find(pet.actor);
    if(!owner || !body || !pet.sourceSkill || body->implementation==MonsterKind::BoneWall || !owner->rules.skills) return StepStatus::Complete;
    if(pet.ownerLevel==owner->persistent.player.level && pet.effectiveRanks==owner->totals.skillRanks && pet.hardRanks==owner->persistent.player.skillRanks) return StepStatus::Complete;
    if(body->revived()) {
        // SrvDo058 installs the owner's contributions on cast. The skill
        // update callback changes PetMax, not the existing pet's life roll.
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
    body.nativeStats=attributes::prepareReviveStatSources(body,*skill.summon,actor.tick,ports_.effects.unitStatSources(id,actor.tick));
    body.owner=actor.player;body.necroSummon=true;body.necroPet=skill.summon->necro;body.hostile=false;++body.allegianceRevision;++body.revision;
    body.rule.noTreasure=body.rule.noExperience=true;body.rule.corpseSelectable=false;body.rule.petType=skill.summon->petType;
    body.petRegistrationOrder=0;body.inactivePet=false;
    body.collisionPattern=nativeUnitCollisionPattern(body.rule.size,!body.rule.curseAiInteract);
    body.petStats.attributes.maxLifeFixed=Fixed8::fromRaw(nativeStatValue(body.nativeStats->values,*body.rule.stats,"maxhp"));
    body.petStats.attributes.maxLife=std::max(1,body.petStats.attributes.maxLifeFixed.raw()>>8);
    body.petStats.attributes.combat=nativeCombatModifiers(body.nativeStats->values,*body.rule.stats,body.petStats.attributes.combat);
    body.petStats.damageRegen=body.rule.damageRegen;
    body.deathTick=body.deathOccurrence=0;body.killer={};body.corpse.unavailable=true;body.rewardComplete=true;
    body.busyUntil=actor.tick;body.neutralAt.reset();body.route.clear();body.poison.reset();body.wound.reset();body.curseControl={};body.attractionTarget={};body.attractionUntil=0;
    body.aiResetTimers.clear();body.nativeAiResetTracked=false;body.specialTargetOrder=0;body.detachedMinion=true;body.aiFunctionRevision=0;body.aiThinkRevision=0;body.aiThinkAt=0;body.tauntTarget={};body.mainOwnerCleared=false;
    body.wanderTargetOrder=0;
    body.chilledUntil=body.frozenUntil=body.stunnedUntil=body.knockedUntil=body.reactionUntil=0;
    std::map<EntityId,monsters::Actor> actors;actors.emplace(id,std::move(body));auto next=state_;
    Companion pet{id,actor.player,Kind::Summon,uint16_t(skill.sourceId)};pet.rank=skill.rank;pet.expires=spec.lifetimeFrames>0?actor.tick+uint64_t(spec.lifetimeFrames):UINT64_MAX;pet.ownerTeleport=owner.teleportRevision;pet.nextDecision=actor.tick+15;pet.ownerLevel=owner.persistent.player.level;pet.effectiveRanks=owner.totals.skillRanks;pet.hardRanks=owner.persistent.player.skillRanks;next.companions.emplace(id,std::move(pet));
    size_t count=0;for(const auto &[key,existing]:next.companions) {(void)key;if(existing.owner==actor.player && existing.sourceSkill==uint16_t(skill.sourceId) && !existing.removeAt) ++count;}
    std::vector<EntityId> retire;for(auto &[key,existing]:next.companions) if(count>size_t(skill.summon->limit) && key!=id && existing.owner==actor.player && existing.sourceSkill==uint16_t(skill.sourceId) && !existing.removeAt) {const auto *old=ports_.monsters.find(key);if(old) {existing.removeAt=actor.tick+uint64_t(old->rule.deathTicks);retire.push_back(key);--count;}}
    transactions::CharacterEdit edit{actor,owner.inventoryRevision,owner.characterRevision,owner.persistent.player};edit.player.mana-=skill.manaCost;edit.skillPayment=transactions::SkillManaPayment{skill.manaCost,skill.sourceId};edit.charge=skill.charge;
    edit.publicFacts.emplace_back(StateFact{id,1,actor.area,spec.reviveState.id,true});
    const auto visual=spec.reviveVisuals.at(size_t(std::clamp(actors.at(id).rule.size,1,3)-1));edit.publicFacts.emplace_back(MissileFact{id,1,actor.area,visual.first,skill.rank,0,actors.at(id).position,actors.at(id).position});
    auto events=std::make_shared<effects::AuraUnitPublication>();ports_.effects.stageAuraActors(*events,std::move(actors),0);
    ports_.effects.stageSummonEvents(*events,id,skill.sourceId,skill.rank,*owner.rules.skills->formulas,actor.tick);
    const auto retired=ports_.monsters.stageRetirements(*events,retire,actor.tick);if(!retired) return retired;
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
            if(body && body->necroMaterial && body->necroMaterial->id==owner.persistent.ironGolem->id) {present=true;dead|=body->life<=0 || bool(pet.removeAt);}
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
DomainResult<> System::necro(const ActorContext &actor,const SkillCastSpec &original,Vec target,EntityId corpse,bool restoring) {
    auto skill=original;
    const auto *owner=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    if(owner && skill.bone && skill.bone->barrier) {
        SummonCastSpec wall;wall.monster="bonewall";wall.kind=MonsterKind::BoneWall;wall.corpse=false;wall.limit=INT32_MAX;
        wall.stats=skill.bone->barrierStats.at(size_t(owner->persistent.difficulty));
        if(!skill.bone->barrierNative) return {DomainStatus::Unavailable,{}};
        wall.native=*skill.bone->barrierNative;wall.native->base=wall.stats;skill.summon=std::move(wall);
    }
    if(!owner || !owner->entered || owner->actor!=actor.actor || owner->area!=actor.area || owner->persistent.player.hp<=0 || !area || area->generation!=actor.areaGeneration || !skill.summon || skill.summon->amazon) return {DomainStatus::InvalidActor,{}};
    const auto &summon=*skill.summon;
    if(summon.necro && summon.necro->kind==NecroSummonKind::Revive) return revive(actor,skill,corpse);
    const bool iron=summon.necro && summon.necro->kind==NecroSummonKind::Iron;
    const auto material=iron?(restoring?owner->persistent.ironGolem:ports_.inventory.ironGolemMaterial(actor,target,corpse)):std::optional<ItemInstance>{};
    if(iron && !material) return {DomainStatus::Unavailable,{}};
    if(summon.corpse && !ports_.monsters.consumableCorpse(corpse,actor.area,actor.tick)) return {DomainStatus::Unavailable,{}};
    auto prior=pending_.find(actor.actor);
    if(prior!=pending_.end() && (prior->second.actor.sequence!=actor.sequence || prior->second.skill!=skill.sourceId)) {cancel(actor.actor);prior=pending_.end();}
    if(prior==pending_.end()) {
        if(nextPreparation_==UINT64_MAX) return {DomainStatus::Capacity,{}};
        auto random=ports_.random;Preparation source{actor,owner->inventoryRevision,childRandom(random),skill.sourceId,skill.rank,owner->persistent.difficulty,owner->persistent.player.level,owner->persistent.player.skillRanks,summon,{}};
        source.material=material;
        source.birthRandom=std::pair{ports_.random,random};
        source.effectiveRanks=owner->totals.skillRanks;source.token=nextPreparation_++;
        if(summon.kind!=MonsterKind::BoneWall) {
            source.ownerRandom=owner->combatRandom;
            const auto palettes=ports_.monsters.read().componentPalettes.find(actor.area);
            source.componentPalettes=palettes==ports_.monsters.read().componentPalettes.end()?area->definition.componentPalettes:palettes->second;
        }
        pending_.emplace(actor.actor,std::move(source));return {DomainStatus::Capacity,{}};
    }
    if(prior->second.rank!=skill.rank || prior->second.effectiveRanks!=owner->totals.skillRanks) {cancel(actor.actor);return necro(actor,original,target,corpse,restoring);}
    if(prior->second.inventoryRevision!=owner->inventoryRevision || prior->second.ownerLevel!=owner->persistent.player.level || prior->second.hardRanks!=owner->persistent.player.skillRanks || prior->second.actor.area!=actor.area || prior->second.actor.areaGeneration!=actor.areaGeneration) {cancel(actor.actor);return {DomainStatus::Stale,{}};}
    if(iron && (!prior->second.material || prior->second.material->id!=material->id || prior->second.material->revision!=material->revision)) {cancel(actor.actor);return {DomainStatus::Stale,{}};}
    if((prior->second.birthRandom && prior->second.birthRandom->first!=ports_.random) ||
        (prior->second.ownerRandom && *prior->second.ownerRandom!=owner->combatRandom)) {
        cancel(actor.actor);return necro(actor,original,target,corpse,restoring);
    }
    if(prior->second.componentPalettes) {
        const auto palettes=ports_.monsters.read().componentPalettes.find(actor.area);
        const auto &current=palettes==ports_.monsters.read().componentPalettes.end()?area->definition.componentPalettes:palettes->second;
        if(current!=*prior->second.componentPalettes) {cancel(actor.actor);return necro(actor,original,target,corpse,restoring);}
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
        const auto room=ports_.areas.roomAtPosition(actor.area,owner->position,int32_t(std::floor(global.x)),int32_t(std::floor(global.y)));
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
        const auto placed=place(summon.corpse?ports_.monsters.find(corpse)->position:target);
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
    for(const auto &[key,body]:actors) {(void)body;Companion pet{key,actor.player,iron?Kind::IronGolem:Kind::Summon,uint16_t(skill.sourceId)};pet.rank=skill.rank;pet.expires=expires;pet.nextDecision=actor.tick+(ready->second.components || (skill.bone && skill.bone->barrier)?25:1);pet.ownerTeleport=owner->teleportRevision;pet.ownerLevel=owner->persistent.player.level;pet.effectiveRanks=owner->totals.skillRanks;pet.hardRanks=owner->persistent.player.skillRanks;next.companions.emplace(key,std::move(pet));}
    const auto sameType=[&](const Companion &existing) {
        if(existing.sourceSkill==uint16_t(skill.sourceId)) return true;
        if(owner->rules.skills->pd2) {
            const auto *body=ports_.monsters.find(existing.actor);
            return body && body->rule.petType==ready->second.summon.petType;
        }
        if(!summon.golem || !existing.sourceSkill) return false;
        const auto found=owner->rules.skills->definitions.find(*existing.sourceSkill);
        return found!=owner->rules.skills->definitions.end() && found->second.spec.summon && found->second.spec.summon->golem;
    };
    size_t count=0;for(const auto &[key,existing]:next.companions) {(void)key;if(existing.owner==actor.player && sameType(existing) && !existing.removeAt) ++count;}
    std::vector<EntityId> retire;
    for(auto &[key,existing]:next.companions) {
        if(count<=size_t(summon.limit)) break;
        if(existing.owner!=actor.player || !sameType(existing) || existing.removeAt) continue;
        const auto *body=ports_.monsters.find(key);if(!body) continue;
        existing.removeAt=actor.tick+uint64_t(body->rule.deathTicks);existing.release=0;retire.push_back(key);--count;
    }
    auto auraUnits=std::make_shared<effects::AuraUnitPublication>();
    ports_.effects.stageAuraActors(*auraUnits,std::move(actors),positions.size());
    for(const auto &[eventId,body]:auraUnits->actors->actors) {
        (void)body;ports_.effects.stageSummonEvents(*auraUnits,eventId,skill.sourceId,skill.rank,*owner->rules.skills->formulas,actor.tick);
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
    const auto retired=ports_.monsters.stageRetirements(*auraUnits,retire,actor.tick);if(!retired) return retired;
    std::shared_ptr<missiles::NativeBirthPublication> makers;
    if(skill.bone && skill.bone->barrier && !skill.bone->prison && skill.bone->sideCount>1) {
        auto direction=missileWallDirection(owner->position,positions.front());if(!direction.length()) direction={0,1};
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
    } else {
        transactions::CharacterEdit edit{actor,owner->inventoryRevision,owner->characterRevision,owner->persistent.player};
        edit.player.mana-=skill.manaCost;edit.skillPayment=transactions::SkillManaPayment{skill.manaCost,skill.sourceId};edit.charge=skill.charge;
        if(ownerRandom) edit.random=transactions::UnitRandomEdit{owner->combatRandom,*ownerRandom};
        if(summon.golem && !iron && owner->persistent.ironGolem && !owner->rules.skills->pd2) edit.ironGolem=std::optional<ItemInstance>{};
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
    if(summon.corpse) ports_.monsters.consumeCorpse(corpse);
    cancel(actor.actor);
    return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::necroStep(Companion &pet,TickContext tick) {
    const auto *body=ports_.monsters.find(pet.actor);const auto *owner=ports_.players.find(pet.owner);
    if(!body || !owner || !owner->entered || owner->persistent.player.hp<=0 || body->implementation==MonsterKind::BoneWall || tick.tick<body->busyUntil || ports_.monsters.frozen(body->id,tick.tick) || body->stunnedUntil>tick.tick) return StepStatus::Complete;
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
    const auto *owner=ports_.players.findActor(source);const auto *parent=ports_.monsters.find(root);const auto *area=ports_.areas.find(region);
    if(!owner || !parent || !area || parent->implementation!=MonsterKind::BoneWall || !owner->rules.skills)
        return {DomainStatus::Unavailable,{}};
    const auto skill=skills::evaluate(*owner,skillId,rank);
    if(!skill.bone || !skill.bone->barrierNative) return {DomainStatus::Unavailable,{}};
    const auto &rule=parent->rule;
    position={std::floor(position.x)+.5f,std::floor(position.y)+.5f};
    const auto global=position+area->definition.origin;
    const auto room=ports_.areas.roomAtPosition(owner->area,owner->position,int32_t(std::floor(global.x)),int32_t(std::floor(global.y)));
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
    summon.stats=skill.bone->barrierStats.at(size_t(owner->persistent.difficulty));summon.native=*skill.bone->barrierNative;summon.native->base=summon.stats;
    summon.native->initialLife=summon.stats.attributes.maxLifeFixed;
    ActorContext context{owner->player,source,region,area->generation,0,tick};
    const auto expectedRandom=ports_.random;
    auto random=expectedRandom,seed=childRandom(random);
    const auto components=chooseMonsterComponents(rule.componentCounts,{},seed);limitedRandom(seed,1);
    auto prepared=ports_.monsters.prepareNecro(context,rule,position,summon,seed,0,{},components);if(!prepared) return {prepared.status,{}};
    const auto id=prepared.value->begin()->first;auto &body=prepared.value->begin()->second;body.nativeMinionOwner=root;
    auto updated=*parent;++updated.revision;updated.nativeMinions.insert(updated.nativeMinions.begin(),id);
    prepared.value->emplace(root,std::move(updated));
    auto next=state_;Companion pet{id,owner->player,Kind::Summon,uint16_t(skillId)};
    pet.rank=rank;pet.expires=tick+uint64_t(rule.summonLifeFrames);pet.nextDecision=tick+25;
    pet.ownerTeleport=owner->teleportRevision;pet.ownerLevel=owner->persistent.player.level;
    pet.effectiveRanks=owner->totals.skillRanks;pet.hardRanks=owner->persistent.player.skillRanks;next.companions.emplace(id,std::move(pet));
    effects::AuraUnitPublication publication;ports_.effects.stageAuraActors(publication,std::move(*prepared.value),1);
    ports_.effects.stageSummonEvents(publication,id,skillId,rank,*owner->rules.skills->formulas,tick);
    ports_.effects.prepareStagedSkillEvents(publication,tick);
    if(ports_.random!=expectedRandom || !ports_.effects.validateAuraUnits(publication) || !ports_.areas.validateRoomRandom(*roomRandom.value)) return {DomainStatus::Stale,{}};
    if(!publication.facts.empty()) {const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},region},std::move(publication.facts)});if(!sent) return {sent.status,{}};}
    ports_.effects.publishAuraUnits(publication);state_.companions.swap(next.companions);ports_.random=random;
    const auto consumed=ports_.transactions.consumeRoomRandom(*roomRandom.value);if(!consumed) std::terminate();
    return {DomainStatus::Applied,true};
}
}
