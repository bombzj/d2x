#include "system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/skills/targeting.hpp"
#include "gameplay/missiles/damage_data.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "core/random.hpp"
#include <cmath>
#include <algorithm>
#include <utility>
namespace d2x::server::missiles {
DomainResult<> System::direct(const Spawn &request, std::vector<EntityId> targets) {
    const auto result=directContact(request,std::move(targets));
    return {result.status,result?std::optional{std::monostate{}}:std::nullopt};
}
DomainResult<DirectContact> System::directContact(const Spawn &request,std::vector<EntityId> targets,DirectNotice notice) {
    const auto *p=ports_.players.find(request.actor.player);const auto *a=ports_.areas.find(request.actor.area);
    if(!p || !p->entered || p->actor!=request.actor.actor || p->area!=request.actor.area || (p->persistent.player.hp<=0 && !request.deathTrigger) ||
        !a || a->generation!=request.actor.areaGeneration || a->definition.town) return {DomainStatus::InvalidActor,{}};
    if(request.skill.minimumDamage.raw()<0 || request.skill.maximumDamage<request.skill.minimumDamage) return {DomainStatus::InvalidRequest,{}};
    if(ports_.ids.cursor()>=UINT32_MAX) return {DomainStatus::Capacity,{}};
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    auto random=ports_.random;
    const auto ownerExpected=request.cost && request.cost->random?request.cost->random->expected:p->combatRandom;
    auto ownerRandom=request.cost && request.cost->random?request.cost->random->next:p->combatRandom;
    Missile m;m.rules=p->rules.skills;m.playerActor=p->actor; m.id=EntityId{ports_.ids.cursor()};m.player=p->player;m.owner=p->actor;m.area=p->area;m.skill=request.skill;
    m.emitter=request.emitter ? request.emitter : p->actor;m.emitterType=request.emitterType;m.definition=request.skill.missileId;m.position=request.target;m.lifetimeFrames=std::max(1,int(std::lround(request.skill.missileLifetime*25.f)));
    if(request.directArea) {
        const auto *destination=ports_.areas.find(*request.directArea);
        if(!request.directOccurrence || !destination || destination->definition.town) return {DomainStatus::InvalidRequest,{}};
        m.area=*request.directArea;m.generation=destination->generation;
    }
    const auto range=fixedMissileDamageRange(request.skill.minimumDamage.raw(),request.skill.maximumDamage.raw());
    // Thunder Storm creates/removes a real temporary missile. SUNIT_InitSeed
    // draws once from the game seed; damage uses that missile's private seed.
    if(request.directOccurrence) {
        if(request.skill.effect!=SkillBehavior::ThunderStorm || !p->rules.skills || !p->rules.skills->formulas || !p->rules.skills->stats)
            return {DomainStatus::NotImplemented,{}};
        const auto &catalog=*p->rules.skills->formulas;
        const auto &skill=catalog.skills.at(request.skill.sourceId);const auto &missile=catalog.missiles.at(m.definition);
        auto input=skills::formulaInput(*p);SkillFormulaValues values(catalog,input);
        const auto physical=values.physicalRange(request.skill.sourceId,request.skill.rank,false);
        if(!physical || physical->first || physical->second || skill.number("srcdam") || missile.number("holy") || skill.elementType!=uint8_t(MissileDamageElement::Lightning))
            return {DomainStatus::NotImplemented,{}};
        m.random=childRandom(random);
        const StatSnapshot initial;
        const MissileDamageInput prepared{.elemental=range,.element=MissileDamageElement::Lightning,
            .owner=p->totals.nativeStats,.missile=initial};
        const auto data=calculateMissileDamageData(prepared,*p->rules.skills->stats);
        const auto hit=rollMissileDamageData(data,{}, {},m.random);
        m.damage=hit.channels[size_t(DamageType::Lightning)];
    } else m.damage=rollMissileDamage(range,random);
    const auto primary=targets.empty()?EntityId{}:targets.front();
    auto hit=impact(m,std::move(targets),{},request.directOccurrence && m.definition>=0);hit.hitClass=uint8_t(request.skill.hitClass);
    if(request.directOccurrence) {hit.occurrence=request.directOccurrence;hit.nativeAura=true;}
    if(request.weapon) {
        hit.type=DamageType::Physical;hit.damage=0;hit.nextDelay=0;
        hit.weapon=request.skill.weapon->smite?rollSmiteDamage(request.weapon->weapon,p->totals.equipment,p->totals.character,request.skill,request.weapon->level,random):rollWeaponSkillDamage(request.weapon->weapon,p->totals.character.combat,request.skill,request.weapon->level,false,random);
        if(hit.weapon->smite || hit.weapon->knockback || request.skill.effect==SkillBehavior::Charge) hit.knockback=true;
        if(request.skill.weapon->conversionFrames>0) hit.conversion=request.skill.weapon;
        hit.coldFrames=uint64_t(std::max(0,hit.weapon->coldFrames));hit.freeze=hit.weapon->freeze;
    }
    if(request.skill.effect==SkillBehavior::StaticField) {
        hit.staticPercent=int(request.skill.staticPercent);hit.staticFloors=p->rules.skills->staticMinimum;
        hit.minimumStaticDamage=request.skill.staticMinDamage.raw();
    }
    if(request.skill.effect==SkillBehavior::Kick) {hit.type=DamageType::Physical;hit.knockback=true;}
    if(request.skill.effect==SkillBehavior::Telekinesis) hit.knockback=int(limitedRandom(random,100))<request.skill.telekinesisKnockbackChance;
    if(request.skill.effect==SkillBehavior::FrozenArmor) {hit.type=DamageType::Cold;hit.freeze=true;}
    auto prepared=ports_.combat.prepareSpells({std::move(hit)});if(!prepared) return {prepared.status,{}};
    std::map<EntityId,Missile> children;
    if(request.weapon && request.skill.weapon->spear && request.skill.weapon->spear->kind==SpearSkillSpec::Kind::Charged) {
        auto burst=request;burst.origin=request.target;burst.target=request.target*2.f-p->position;burst.weapon.reset();burst.cost.reset();
        auto launched=launch(burst,random,&ownerRandom);
        if(launched.size()+1>UINT32_MAX-ports_.ids.cursor()) return {DomainStatus::Capacity,{}};
        auto cursor=ports_.ids.cursor()+1;
        for(auto &child:launched) {if(child.creationRejected || !creationRoom(child) || !townAllowed(child)) continue;child.id=EntityId{cursor++};children.emplace(child.id,std::move(child));}
    }
    if(request.weapon && request.skill.weapon->spear && request.skill.weapon->spear->kind==SpearSkillSpec::Kind::Strike) {
        const auto &program=*request.skill.weapon->spear;
        std::vector<uint64_t> eligible;
        for(const auto id:participants.enemies(p->actor,p->area)) {
            const auto target=participants.find(id);
            const bool delayed=target.player?!ports_.effects.missileHitAllowed(id,request.actor.tick):target.monster->nextHitTick>request.actor.tick;
            if(id!=primary && !delayed && withinSkillArea(request.target,target.position(),float(program.targetRadius),SkillAreaCoordinates::FlooredCells) && a->definition.collision.missileSegment(request.target,target.position(),{4,1})) eligible.push_back(id.value);
        }
        const EntityId successor{missileChainSuccessor(primary.value,eligible)};
        if(successor) {
            if(ports_.ids.cursor()>=UINT32_MAX-1) return {DomainStatus::Capacity,{}};
            auto burst=request;burst.weapon.reset();burst.cost.reset();burst.skill=skills::evaluate(*p,request.skill.sourceId,request.skill.rank);
            const Vec at{std::floor(request.target.x)+.5f,std::floor(request.target.y)+.5f};
            auto child=make(burst,at,participants.find(successor).position()-at,burst.skill.missileId,
                std::max(1,int(std::lround(burst.skill.missileLifetime*25.f))),burst.skill.missileVelocity,Program::Arc,random,false,&ownerRandom);
            child.id=EntityId{ports_.ids.cursor()+1};child.remainingHits=burst.skill.arc->count;child.lastHit=primary;
            child.parameters[0]=burst.skill.arc->count;
            if(!child.creationRejected && creationRoom(child) && townAllowed(child)) children.emplace(child.id,std::move(child));
        }
    }
    auto facts=(request.weapon || request.directOccurrence)?std::vector<DomainFact>{}:visuals({m});
    if(request.skill.hitOverlayId>=0 && !prepared.value->spells.empty()) for(const auto id:prepared.value->spells.front().targets) facts.emplace_back(OverlayFact{id,uint8_t(participants.find(id).player?0:1),m.area,request.skill.hitOverlayId});
    DirectContact contact{m.id,{}};
    if(request.skill.effect==SkillBehavior::ThunderStorm && !prepared.value->spells.empty() && !prepared.value->spells.front().targets.empty()) {
        DomainFact pulse=SkillPulseFact{p->actor,prepared.value->spells.front().targets.front(),0,uint8_t(participants.find(prepared.value->spells.front().targets.front()).player?0:1),m.area,request.skill.sourceId,request.skill.rank,request.target};
        if(notice==DirectNotice::DeferPulse) contact.deferredFacts.push_back(std::move(pulse));else facts.push_back(std::move(pulse));
    }
    auto timers=prepareTimers(children);
    if(!ports_.events.hasCapacity(facts.size()+1,2)) return {DomainStatus::Capacity,{}};
    auto cost=request.cost;
    if(cost) cost->random=transactions::UnitRandomEdit{ownerExpected,ownerRandom};
    else if(!request.free || ownerRandom!=p->combatRandom) {
        transactions::CharacterEdit edit{request.actor,p->inventoryRevision,p->characterRevision,p->persistent.player};
        if(!request.free) {edit.player.mana-=request.skill.manaCost;edit.charge=request.skill.charge;}
        edit.random=transactions::UnitRandomEdit{ownerExpected,ownerRandom};
        auto transaction=ports_.transactions.prepare(std::move(edit));if(!transaction) return {transaction.status,{}};
        cost=std::move(*transaction.value);
    }
    if(cost) {const auto debit=ports_.transactions.commit(std::move(*cost));if(!debit) return {debit.status,{}};}
    if(!facts.empty()) ports_.events.publish({0,request.actor.tick,{}, {AudienceKind::Area,{},m.area},std::move(facts)});
    ports_.combat.commitSpells(std::move(*prepared.value));
    for(size_t i=0;i<children.size()+1;++i) ports_.ids.allocate();
    state_.missiles.merge(children);publishTimers(timers);ports_.random=random;
    return {DomainStatus::Applied,std::move(contact)};
}
}
