#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/monsters/native_birth.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/runtime/skill_rules.hpp"
#include "gameplay/monsters/native_spawn.hpp"
#include "gameplay/skills/assassin_summon.hpp"
#include "gameplay/skills/druid_summon.hpp"
#include "gameplay/skills/druid_spec.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::companions {
DomainResult<> System::assassin(const ActorContext &actor,const SkillCastSpec &skill,Vec target) {
    return nativeSummon(actor,skill,target);
}
DomainResult<> System::druid(const ActorContext &actor,const SkillCastSpec &skill,Vec target) {
    return nativeSummon(actor,skill,target);
}
DomainResult<> System::nativeSummon(const ActorContext &actor,const SkillCastSpec &skill,Vec target) {
    const auto *owner=ports_.players.find(actor.player);
    const auto *caster=ports_.monsters.find(actor.actor);
    const bool controlled=caster && caster->assassinPet && caster->owner==actor.player && caster->alive() && caster->area==actor.area;
    if(!owner || !owner->entered || (owner->actor!=actor.actor && !controlled) || owner->area!=actor.area || !skill.summon || (!skill.summon->assassin && !skill.summon->druid))
        return {DomainStatus::InvalidActor,{}};
    const auto &area=ports_.areas.at(actor.area);if(area.generation!=actor.areaGeneration) return {DomainStatus::Stale,{}};
    const auto *pet=skill.summon->assassin.get();const auto *druid=skill.summon->druid.get();
    const auto ai=pet?pet->definition->ai:AssassinPetDefinition::Ai::Sentry;
    const bool blade=pet && ai==AssassinPetDefinition::Ai::BladeCreeper;
    const bool shadow=pet && pet->definition->warp;
    auto prior=pending_.find(actor.actor);
    if(prior!=pending_.end() && (prior->second.actor.sequence!=actor.sequence || prior->second.skill!=skill.sourceId)) {cancel(actor.actor);prior=pending_.end();}
    if(prior==pending_.end()) {
        if(nextPreparation_==UINT64_MAX) return {DomainStatus::Capacity,{}};
        auto random=ports_.random;
        Preparation source{actor,owner->inventoryRevision,childRandom(random),skill.sourceId,skill.rank,
            owner->persistent.difficulty,owner->persistent.player.level,owner->persistent.player.skillRanks,*skill.summon,{}};
        source.birthRandom=std::pair{ports_.random,random};
        source.effectiveRanks=owner->totals.skillRanks;source.token=nextPreparation_++;
        if(pet && !pet->definition->inheritOwnerSkills) {
            const auto palettes=ports_.monsters.read().componentPalettes.find(actor.area);
            source.componentPalettes=palettes==ports_.monsters.read().componentPalettes.end()?area.definition.componentPalettes:palettes->second;
        }
        if(shadow) source.ownerEquipment=owner->persistent;
        pending_.emplace(actor.actor,std::move(source));return {DomainStatus::Capacity,{}};
    }
    if(prior->second.rank!=skill.rank || prior->second.effectiveRanks!=owner->totals.skillRanks) {cancel(actor.actor);return nativeSummon(actor,skill,target);}
    if(prior->second.inventoryRevision!=owner->inventoryRevision || prior->second.ownerLevel!=owner->persistent.player.level ||
       prior->second.hardRanks!=owner->persistent.player.skillRanks || prior->second.actor.areaGeneration!=actor.areaGeneration) {
        cancel(actor.actor);return {DomainStatus::Stale,{}};
    }
    if(prior->second.birthRandom && prior->second.birthRandom->first!=ports_.random) {
        cancel(actor.actor);return nativeSummon(actor,skill,target);
    }
    if(prior->second.componentPalettes) {
        const auto palettes=ports_.monsters.read().componentPalettes.find(actor.area);
        const auto &current=palettes==ports_.monsters.read().componentPalettes.end()?area.definition.componentPalettes:palettes->second;
        if(current!=*prior->second.componentPalettes) {cancel(actor.actor);return nativeSummon(actor,skill,target);}
    }
    const auto ready=prepared_.find(actor.actor);if(ready==prepared_.end()) return {DomainStatus::Capacity,{}};
    if(!ready->second.deferred.empty()) {cancel(actor.actor);return {DomainStatus::Unavailable,{}};}
    const auto &prepared=ready->second;const auto origin=controlled?caster->position:owner->position;
    if(controlled && owner->rules.skills->pd2 && pet &&
       (ai==AssassinPetDefinition::Ai::Sentry || ai==AssassinPetDefinition::Ai::DeathSentry)) {
        size_t count{};
        for(const auto &[id,entry]:state_.companions) {
            const auto *body=ports_.monsters.find(id);
            if(entry.owner==actor.player && !entry.removeAt && body && body->alive() &&
               body->rule.petType==prepared.summon.petType) ++count;
        }
        // Current Do45 refuses a shadow's placement once the controlling
        // player's trap pool is full; a player cast can replace the oldest.
        if(count>=size_t(std::max(0,prepared.summon.limit))) {cancel(actor.actor);return {DomainStatus::Unavailable,{}};}
    }
    const Vec center=blade || (shadow && pet->definition->inheritOwnerSkills)?origin:target;
    const auto global=center+area.definition.origin;
    const auto room=ports_.areas.roomAtPosition(actor.area,origin,int(global.x),int(global.y));if(!room) return {DomainStatus::Unavailable,{}};
    auto roomSeed=ports_.areas.roomRandom(*room);if(!roomSeed) return {roomSeed.status,{}};
    const auto collision=ports_.spatial.nativeCollisionSample();
    monsters::Admission admission;admission.identity.monster=prepared.summon.monster;admission.identity.spawnKey="pet.search";
    admission.area=actor.area;admission.position=center;admission.rule=prepared.rule;admission.tick=actor.tick;
    auto point=ports_.monsters.searchRoomSpawn(admission,*roomSeed.value,-1,collision);
    if(!point) return {point.status,{}};
    if(!*point.value) point=ports_.monsters.searchRoomSpawn(admission,*roomSeed.value,4,collision);
    if(!point) return {point.status,{}};
    if(!*point.value) {const auto consumed=ports_.transactions.consumeRoomRandom(*roomSeed.value);return {consumed?DomainStatus::Unavailable:consumed.status,{}};}
    std::optional<Vec> position=**point.value;
    if(blade) {
        const auto point=origin+area.definition.origin;
        const auto originRoom=ports_.areas.roomAtPosition(actor.area,origin,int(point.x),int(point.y));
        if(originRoom) if(const auto free=ports_.areas.nativeFreeCoordinates(*originRoom,int(point.x),int(point.y),prepared.rule.size,0x1c09,collision))
            if(free->room.area==actor.area) position=free->position;
    }
    auto equipment=shadow?std::make_shared<PersistentCharacter>(prepared.equipment):std::shared_ptr<PersistentCharacter>{};
    size_t items{};const auto count=[&](auto &&self,const ItemInstance &item)->void {++items;for(const auto &child:item.socketedItems) self(self,child);};
    if(equipment) for(const auto &[id,item]:equipment->inventory.items) {(void)id;count(count,item);}
    const ActorContext ownerContext{owner->player,owner->actor,owner->area,area.generation,actor.sequence,actor.tick};
    auto summon=prepared.summon;
    if(shadow) {
        auto &skills=summon.native->prePassiveSkills;
        if(!prepared.rule.baseSkillRanks.contains(0)) skills.emplace_back(0,1);
        if(ai==AssassinPetDefinition::Ai::ShadowMaster && pet->definition->inheritOwnerSkills) for(const auto &[id,row]:owner->rules.skills->formulas->skills) {
            if(row.classId!=int(owner->definition.sourceRow)) continue;
            const auto definition=owner->rules.skills->definitions.find(id);
            if(definition!=owner->rules.skills->definitions.end() && definition->second.spec.summon &&
               (definition->second.spec.summon->petType==prepared.summon.petType || definition->second.spec.summon->monster==prepared.summon.monster)) continue;
            const auto rank=owner->persistent.player.skillRanks.find(id);
            skills.emplace_back(id,std::clamp(skill.rank/2+(rank==owner->persistent.player.skillRanks.end()?1:rank->second)/2,1,24));
        }
    }
    auto bodies=ports_.monsters.prepareNativePet(ownerContext,prepared.rule,*position,summon,equipment,items,
        prepared.weapon.item?std::optional{prepared.weapon}:std::nullopt,prepared.equipmentSources,prepared.random,prepared.equipmentStats);
    if(!bodies) return {bodies.status,{}};
    const auto id=bodies.value->begin()->first;auto next=state_;
    if(prepared.components) bodies.value->at(id).components=*prepared.components;
    if(prepared.equipmentStats) bodies.value->at(id).shield=bool(prepared.equipmentStats->shield);
    if(!shadow && !blade && prepared.rule.spawnTicks>0) {
        auto &body=bodies.value->at(id);body.riseMode=prepared.rule.spawnMode;
        body.riseUntil=body.busyUntil=actor.tick+uint64_t(prepared.rule.spawnTicks);body.neutralAt=body.riseUntil;
    }
    Companion companion{id,actor.player,Kind::Summon,uint16_t(skill.sourceId)};
    companion.rank=skill.rank;companion.expires=pet && pet->lifeFrames>0?actor.tick+uint64_t(pet->lifeFrames):UINT64_MAX;
    if(shadow) {companion.shadowInitialized=true;companion.assassinAi={0,0,std::max(1,skill.rank)};}
    companion.nextDecision=actor.tick+(shadow?20:blade?1:25);companion.assassinOrigin=origin;companion.assassinTarget=target;
    if(blade) companion.assassinAi={-1,1,0};
    if(druid) {
        const auto &program=*prepared.summon.druid;
        if(program.definition->ai==DruidPetDefinition::Ai::Raven)
            companion.druidAi={-1,program.definition->parameters[2][size_t(prepared.rule.difficulty)]+1,
                bodies.value->at(id).ravenInitialCircle.value()};
        else companion.druidAi={0,0,0};
    }
    next.companions.emplace(id,std::move(companion));
    // PlayerPets enforces capacity by PET TYPE, across all five trap skills
    // and Blade Sentinel, and across both shadow skills.
    size_t active{};for(const auto &[key,entry]:next.companions) {
        const auto *body=key==id?&bodies.value->at(id):ports_.monsters.find(key);
        if(entry.owner==actor.player && !entry.removeAt && body && body->rule.petType==prepared.summon.petType) ++active;
    }
    std::vector<EntityId> retired;
    if(druid && druid->definition->group) for(auto &[key,entry]:next.companions) {
        const auto *body=ports_.monsters.find(key);
        if(key==id || entry.owner!=actor.player || entry.removeAt || !body || !body->druidPet ||
           body->rule.petType==prepared.summon.petType || body->druidPet->definition->group!=druid->definition->group) continue;
        entry.removeAt=actor.tick+uint64_t(body->rule.deathTicks);entry.release=0;retired.push_back(key);
    }
    for(auto &[key,entry]:next.companions) {
        if(active<=size_t(std::max(1,prepared.summon.limit))) break;
        const auto *body=ports_.monsters.find(key);
        if(key==id || entry.owner!=actor.player || entry.removeAt || !body || body->rule.petType!=prepared.summon.petType) continue;
        entry.removeAt=actor.tick+uint64_t(body->rule.deathTicks);entry.release=0;retired.push_back(key);--active;
    }
    auto publication=std::make_shared<effects::AuraUnitPublication>();
    if(shadow && skill.assassin->state.id>0) {
        bodies.value->at(id).aiStates.insert(skill.assassin->state.id);
        publication->facts.emplace_back(StateFact{id,1,actor.area,skill.assassin->state.id,true});
    }
    if(druid && skill.druid && skill.druid->state.id>0) {
        bodies.value->at(id).aiStates.insert(skill.druid->state.id);
        publication->facts.emplace_back(StateFact{id,1,actor.area,skill.druid->state.id,true});
    }
    ports_.effects.stageAuraActors(*publication,std::move(*bodies.value),items+1);
    ports_.effects.stageSummonEvents(*publication,id,skill.sourceId,skill.rank,*owner->rules.skills->formulas,actor.tick);
    const auto removal=ports_.monsters.stageRetirements(*publication,retired,actor.tick);if(!removal) return removal;
    transactions::CharacterEdit edit{ownerContext,owner->inventoryRevision,owner->characterRevision,owner->persistent.player};
    if(!controlled) {edit.player.mana-=skill.manaCost;edit.skillPayment=transactions::SkillManaPayment{skill.manaCost,skill.sourceId};edit.charge=skill.charge;}
    auto plan=ports_.transactions.prepare(std::move(edit),nullptr,{},publication);if(!plan) return {plan.status,{}};
    plan.value->roomRandom=std::make_shared<const RoomRandomLoan>(*roomSeed.value);
    const auto nextRandom=prepared.equipmentRandom?prepared.equipmentRandom:
        prior->second.birthRandom?std::optional{prior->second.birthRandom->second}:std::nullopt;
    if(nextRandom) plan.value->gameRandom=transactions::UnitRandomEdit{prior->second.birthRandom->first,*nextRandom};
    const auto result=ports_.transactions.commit(std::move(*plan.value));if(!result) return result;
    if(prepared.componentPalettes) {
        monsters::NativeBirthBatch components;components.palettes.emplace(actor.area,*prepared.componentPalettes);
        ports_.monsters.publishBirthComponents(components);
    }
    state_.companions.swap(next.companions);cancel(actor.actor);return {DomainStatus::Applied,std::monostate{}};
}
}
