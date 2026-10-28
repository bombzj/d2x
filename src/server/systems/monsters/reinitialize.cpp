#include "system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/area_store.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include <algorithm>
#include <utility>
#include <stdexcept>

namespace d2x::server::monsters {
void recordNativeMode(Actor &unit,uint8_t mode) {
    if(unit.nativeMode!=1) unit.nativeAiState=unit.nativeAiState>=16?uint8_t(unit.nativeAiState-16):
        unit.nativeAiState==13 && unit.nativeMode==3?13:unit.nativeMode;
    unit.nativeMode=mode;
}
void System::neutralMode(EntityId id) {
    stop(id);
    if(auto found=state_.actors.find(id);found!=state_.actors.end()) recordNativeMode(found->second,1);
}
DomainResult<> System::suppressTreasure(EntityId id) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return {DomainStatus::InvalidActor,{}};
    auto &unit=found->second;
    if(!unit.rule.noTreasure) {
        if(unit.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        unit.rule.noTreasure=true;++unit.revision;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::setNativeTarget(EntityId id,EntityId target) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return {DomainStatus::InvalidActor,{}};
    auto &unit=found->second;if(unit.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    unit.movementTarget=target;++unit.revision;return {DomainStatus::Applied,std::monostate{}};
}
void System::storeLastAttacker(EntityId target,EntityId source) {
    if(auto found=state_.actors.find(target);found!=state_.actors.end() && found->second.alive() && found->second.lastAttacker!=source) {
        if(found->second.revision==UINT64_MAX) throw std::overflow_error("Monster last-attacker revision exhausted");
        found->second.lastAttacker=source;++found->second.revision;
    }
}
DomainResult<> System::reinitializeBaalToStairs(EntityId id,uint64_t tick) {
    const auto *previous=find(id);
    if(!previous || !previous->alive() || !previous->questNpc || previous->identity.monster!="baalthrone")
        return {DomainStatus::InvalidActor,{}};
    if(previous->revision==UINT64_MAX || previous->admissionRevision==UINT64_MAX || previous->allegianceRevision==UINT64_MAX || previous->aiFunctionRevision==UINT64_MAX)
        return {DomainStatus::Capacity,{}};
    const auto &npcs=ports_.areas.at(previous->area).definition.questNpcs;
    const auto stairs=npcs.find("baalcrabstairs");
    if(stairs==npcs.end() || !stairs->second.combat || stairs->second.combat->ai.kind!=MonsterAiKind::BaalToStairs)
        return {DomainStatus::NotImplemented,{}};
    Admission request;request.identity=previous->identity;request.identity.monster=stairs->first;
    request.identity.nativeClass=stairs->second.nativeClass;request.implementation=MonsterKind::BaalToStairs;
    request.area=previous->area;request.position=previous->position;request.rule=*stairs->second.combat;
    request.tick=tick;request.unitRandom=previous->combatRandom;request.uncounted=previous->uncounted;
    auto random=ports_.random;ComponentPaletteState palettes;
    auto prepared=prepareAdmissionActor(request,id,random,palettes,ports_.ids.cursor(),nullptr,previous->roomAdmission);
    if(!prepared) return {prepared.status,{}};
    auto candidate=std::move(*prepared.value);const auto allocated=candidate.birthEquipmentItems;
    // MONSTER_Free leaves MonsterData and the GUID in place. Reinitialize
    // rebuilds attributes, skills and AI; it does not execute UMod33's buff.
    candidate.rule.enchantment=previous->rule.enchantment;
    candidate.rule.nativeModifiers=previous->rule.nativeModifiers;
    candidate.rule.superUniqueIndex=previous->rule.superUniqueIndex;
    candidate.rule.uniqueTranslation=previous->rule.uniqueTranslation;
    candidate.rule.noTreasure|=previous->rule.noTreasure;candidate.rule.noExperience|=previous->rule.noExperience;
    candidate.questNpc=true;candidate.roomAdmission=previous->roomAdmission;
    candidate.revision=previous->revision+1;candidate.admissionRevision=previous->admissionRevision+1;
    candidate.allegianceRevision=previous->allegianceRevision+1;candidate.aiFunctionRevision=previous->aiFunctionRevision+1;
    candidate.changedClass=stairs->second.nativeClass;candidate.aiStates.insert(candidate.rule.changeClassState);
    candidate.admissionRule=std::make_shared<const MonsterRule>(candidate.rule);
    candidate.aiThinkAt=tick+5;candidate.aiThinkRevision=1;
    candidate.initialAiTimer=std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
        {{},{FrameEventUnit::Monster,id},FrameEventType::AiThink,tick+5}));
    auto publication=prepareActorPublication({{id,std::move(candidate)}},allocated);
    if(!validateActors(publication)) return {DomainStatus::Stale,{}};
    const auto &changed=publication.actors.at(id);
    const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},changed.area},
        {StateFact{id,1,changed.area,changed.rule.changeClassState,true,
            {{StatKey{uint16_t(changed.rule.shortParameterStat),0},*changed.changedClass}}}}});
    if(!sent) return {sent.status,{}};
    ports_.effects.forgetUnit(id);ports_.frameEvents.cancelOwner({FrameEventUnit::Monster,id});
    publishComponents(palettes);publishActors(publication);ports_.random=random;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::reinitializeWhipped(EntityId source,EntityId target,int nativeClass,uint64_t tick) {
    const auto *previous=find(target),*caster=find(source);
    if(!previous || !previous->alive() || !caster) return {DomainStatus::InvalidActor,{}};
    if(previous->revision==UINT64_MAX || previous->admissionRevision==UINT64_MAX || previous->allegianceRevision==UINT64_MAX)
        return {DomainStatus::Capacity,{}};
    auto random=ports_.random;ComponentPaletteState palettes;size_t allocated{};Actor candidate;
    if(nativeClass>=0) {
        const auto &classes=ports_.areas.at(previous->area).definition.nativeMonsterClasses;
        const auto found=classes.find(nativeClass);if(found==classes.end()) return {DomainStatus::NotImplemented,{}};
        const auto &child=found->second;
        Admission request{child.identity,child.implementation,previous->area,previous->position,previous->hostile,child.rule,{},tick};
        request.unitRandom=previous->combatRandom;request.uncounted=previous->uncounted;
        auto prepared=prepareAdmissionActor(request,target,random,palettes,ports_.ids.cursor(),nullptr,previous->roomAdmission);
        if(!prepared) return {prepared.status,{}};
        candidate=std::move(*prepared.value);allocated=candidate.birthEquipmentItems;
        candidate.identity=previous->identity;candidate.identity.monster=child.identity.monster;candidate.identity.nativeClass=child.rule.nativeClass;
        candidate.rule.enchantment=previous->rule.enchantment;
        // Monster_Initialize adds the new base type's modifiers; MonsterData's
        // original UMod slots survive MONSTER_Free.
        for(int modifier:previous->rule.nativeModifiers)
            if(std::ranges::find(candidate.rule.nativeModifiers,modifier)==candidate.rule.nativeModifiers.end()) candidate.rule.nativeModifiers.push_back(modifier);
        candidate.rule.superUniqueIndex=previous->rule.superUniqueIndex;candidate.rule.uniqueTranslation=previous->rule.uniqueTranslation;
        candidate.rule.noExperience|=previous->rule.noExperience;candidate.rule.noTreasure|=previous->rule.noTreasure;
        candidate.owner=previous->conversion?std::nullopt:previous->owner;candidate.necroSummon=previous->necroSummon;candidate.necroPet=previous->necroPet;
        candidate.petRegistrationOrder=previous->petRegistrationOrder;candidate.inactivePet=previous->inactivePet;
        candidate.nativeMinionOwner=EntityId{};candidate.mainOwnerCleared=previous->mainOwnerCleared;
        candidate.roomAdmission=previous->roomAdmission;
        candidate.revision=previous->revision+1;candidate.admissionRevision=previous->admissionRevision+1;candidate.allegianceRevision=previous->allegianceRevision+1;
    } else {candidate=*previous;++candidate.revision;} // Retail continues after a failed class-chain lookup.
    candidate.whippedAi=true;candidate.changedClass=nativeClass;++candidate.aiFunctionRevision;
    if(std::ranges::find(candidate.rule.nativeModifiers,33)==candidate.rule.nativeModifiers.end()) candidate.rule.nativeModifiers.push_back(33);
    candidate.admissionRule=std::make_shared<const MonsterRule>(candidate.rule);
    candidate.aiStates.insert(candidate.rule.changeClassState);
    if(const auto pose=candidate.rule.aiPoseTicks.find(8);pose!=candidate.rule.aiPoseTicks.end()) {
        recordNativeMode(candidate,8);candidate.aiPose=8;candidate.poseUntil=candidate.busyUntil=tick+uint64_t(pose->second);candidate.neutralAt=candidate.poseUntil;
    }
    auto publication=prepareActorPublication({{target,std::move(candidate)}},allocated);
    if(!validateActors(publication)) return {DomainStatus::Stale,{}};
    const auto &changed=publication.actors.at(target);
    std::vector<DomainFact> facts{StateFact{target,1,changed.area,changed.rule.changeClassState,true,{{StatKey{uint16_t(changed.rule.shortParameterStat),0},nativeClass}}}};
    const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},changed.area},std::move(facts)});if(!sent) return {sent.status,{}};
    if(nativeClass>=0) {ports_.effects.forgetUnit(target);ports_.frameEvents.cancelOwner({FrameEventUnit::Monster,target});}
    publishComponents(palettes);publishActors(publication);ports_.random=random;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::reinitializeAfterDeath(EntityId id,uint64_t tick) {
    const auto *previous=find(id);
    if(!previous || previous->alive() || !previous->rule.deathReinitialize) return {DomainStatus::InvalidActor,{}};
    if(previous->revision==UINT64_MAX || previous->admissionRevision==UINT64_MAX || previous->allegianceRevision==UINT64_MAX)
        return {DomainStatus::Capacity,{}};
    const auto &child=*previous->rule.deathReinitialize;
    Admission request{child.identity,child.implementation,previous->area,previous->position,previous->hostile,child.rule,{},tick};
    request.unitRandom=previous->combatRandom;request.uncounted=previous->uncounted;
    auto random=ports_.random;ComponentPaletteState palettes;
    auto prepared=prepareAdmissionActor(request,id,random,palettes,ports_.ids.cursor(),nullptr,previous->roomAdmission);
    if(!prepared) return {prepared.status,{}};
    auto candidate=std::move(*prepared.value);const auto allocated=candidate.birthEquipmentItems;
    // MonsterData (unique flags, name and UMods) and the unit's owner survive
    // MONSTER_Free. StatListEx, skills and AI control are initialized afresh;
    // in particular, do not apply the head's elite HP/level bonus again.
    candidate.identity=previous->identity;candidate.identity.monster=child.identity.monster;candidate.identity.nativeClass=child.rule.nativeClass;
    candidate.rule.enchantment=previous->rule.enchantment;
    candidate.rule.nativeModifiers=previous->rule.nativeModifiers;
    candidate.rule.superUniqueIndex=previous->rule.superUniqueIndex;
    candidate.rule.uniqueTranslation=previous->rule.uniqueTranslation;
    candidate.rule.noExperience|=previous->rule.noExperience;
    candidate.rule.noTreasure|=previous->rule.noTreasure;
    candidate.admissionRule=std::make_shared<const MonsterRule>(candidate.rule);
    // owner represents active player control here. Conversion's control is
    // removed with its StatList; a Revive's permanent unit flag survives.
    candidate.owner=previous->conversion?std::nullopt:previous->owner;
    candidate.necroSummon=previous->necroSummon;candidate.necroPet=previous->necroPet;
    candidate.petRegistrationOrder=previous->petRegistrationOrder;candidate.inactivePet=previous->inactivePet;
    candidate.nativeMinionOwner=EntityId{};candidate.mainOwnerCleared=previous->mainOwnerCleared;
    candidate.roomAdmission=previous->roomAdmission;
    candidate.revision=previous->revision+1;candidate.admissionRevision=previous->admissionRevision+1;
    candidate.allegianceRevision=previous->allegianceRevision+1;
    auto publication=prepareActorPublication({{id,std::move(candidate)}},allocated);
    if(!validateActors(publication)) return {DomainStatus::Stale,{}};
    ports_.effects.forgetUnit(id);
    ports_.frameEvents.cancelOwner({FrameEventUnit::Monster,id});
    publishComponents(palettes);publishActors(publication);ports_.random=random;
    return {DomainStatus::Applied,std::monostate{}};
}
}
