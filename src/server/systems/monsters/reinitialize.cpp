#include "system.hpp"
#include "server/systems/effects/system.hpp"
#include <utility>

namespace d2x::server::monsters {
DomainResult<> System::reinitializeAfterDeath(EntityId id,uint64_t tick) {
    const auto *previous=find(id);
    if(!previous || previous->alive() || !previous->rule.deathReinitialize) return {DomainStatus::InvalidActor,{}};
    if(previous->revision==UINT64_MAX || previous->admissionRevision==UINT64_MAX || previous->allegianceRevision==UINT64_MAX)
        return {DomainStatus::Capacity,{}};
    const auto &child=*previous->rule.deathReinitialize;
    Admission request{child.identity,child.implementation,previous->area,previous->position,previous->hostile,child.rule,{},tick};
    request.unitRandom=previous->combatRandom;request.uncounted=previous->uncounted;
    auto random=ports_.random;ComponentPaletteState palettes;
    auto prepared=prepareAdmissionActor(request,id,random,palettes,ports_.ids.cursor());
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
