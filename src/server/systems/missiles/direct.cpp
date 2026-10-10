#include "system.hpp"
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
namespace d2x::server::missiles {
DomainResult<> System::direct(const Spawn &request, std::vector<EntityId> targets) {
    const auto *p=ports_.players.find(request.actor.player);const auto *a=ports_.areas.find(request.actor.area);
    if(!p || !p->entered || p->actor!=request.actor.actor || p->area!=request.actor.area || (p->persistent.player.hp<=0 && !request.deathTrigger) ||
        !a || a->generation!=request.actor.areaGeneration || a->definition.town) return {DomainStatus::InvalidActor,{}};
    if(ports_.ids.cursor()>=UINT32_MAX) return {DomainStatus::Capacity,{}};
    auto random=ports_.random;
    Missile m; m.id=EntityId{ports_.ids.cursor()};m.player=p->player;m.owner=p->actor;m.area=p->area;m.skill=request.skill;
    m.emitter=request.emitter ? request.emitter : p->actor;m.emitterType=request.emitterType;m.definition=request.skill.missileId;m.position=request.target;m.lifetimeFrames=std::max(1,int(std::lround(request.skill.missileLifetime*25.f)));
    m.damage=int64_t(request.skill.minimumDamage*256.f)+limitedRandom(random,uint32_t((request.skill.maximumDamage-request.skill.minimumDamage)*256.f));
    const auto primary=targets.empty()?EntityId{}:targets.front();
    auto hit=impact(m,std::move(targets));hit.hitClass=uint8_t(request.skill.hitClass);
    if(request.weapon) {
        hit.type=DamageType::Physical;hit.damage=0;hit.nextDelay=0;
        hit.weapon=request.skill.weapon->smite?rollSmiteDamage(request.weapon->weapon,p->totals.equipment,p->totals.character,request.skill,request.weapon->level,random):rollWeaponSkillDamage(request.weapon->weapon,p->totals.character.combat,request.skill,request.weapon->level,false,random);
        if(hit.weapon->smite || request.skill.effect==SkillBehavior::Charge) hit.knockback=true;
        if(request.skill.weapon->conversionFrames>0) hit.conversion=request.skill.weapon;
        hit.coldFrames=uint64_t(std::max(0,hit.weapon->coldFrames));hit.freeze=hit.weapon->freeze;
    }
    if(request.skill.effect==SkillBehavior::StaticField) {
        hit.staticPercent=int(request.skill.staticPercent);hit.staticFloors=p->rules.skills->staticMinimum;
        hit.minimumStaticDamage=int64_t(request.skill.staticMinDamage*256.f);
    }
    if(request.skill.effect==SkillBehavior::Kick) {hit.type=DamageType::Physical;hit.knockback=true;}
    if(request.skill.effect==SkillBehavior::Telekinesis) hit.knockback=int(limitedRandom(random,100))<request.skill.telekinesisKnockbackChance;
    if(request.skill.effect==SkillBehavior::FrozenArmor) {hit.type=DamageType::Cold;hit.freeze=true;}
    auto prepared=ports_.combat.prepareSpells({std::move(hit)});if(!prepared) return {prepared.status,{}};
    std::map<EntityId,Missile> children;
    if(request.weapon && request.skill.weapon->spear && request.skill.weapon->spear->kind==SpearSkillSpec::Kind::Charged) {
        auto burst=request;burst.origin=request.target;burst.target=request.target*2.f-p->position;burst.weapon.reset();burst.cost.reset();
        auto launched=launch(burst,random);
        if(launched.size()>4096-state_.missiles.size() || launched.size()+1>UINT32_MAX-ports_.ids.cursor()) return {DomainStatus::Capacity,{}};
        auto cursor=ports_.ids.cursor()+1;
        for(auto &child:launched) {child.id=EntityId{cursor++};children.emplace(child.id,std::move(child));}
    }
    if(request.weapon && request.skill.weapon->spear && request.skill.weapon->spear->kind==SpearSkillSpec::Kind::Strike) {
        const auto &program=*request.skill.weapon->spear;
        std::vector<uint64_t> eligible;
        for(const auto &[id,target]:ports_.monsters.read().actors) {
            const Vec d{std::floor(target.position.x)-std::floor(request.target.x),std::floor(target.position.y)-std::floor(request.target.y)};
            if(target.enemyTarget() && target.life>0 && target.area==p->area && id!=primary && target.nextHitTick<=request.actor.tick &&
                d.x*d.x+d.y*d.y<=float(program.targetRadius*program.targetRadius) && a->definition.collision.missileSegment(request.target,target.position,{4,1})) eligible.push_back(id.value);
        }
        const EntityId successor{missileChainSuccessor(primary.value,eligible)};
        if(successor) {
            if(state_.missiles.size()>=4096 || ports_.ids.cursor()>=UINT32_MAX-1) return {DomainStatus::Capacity,{}};
            auto burst=request;burst.weapon.reset();burst.cost.reset();burst.skill=skills::evaluate(*p,request.skill.sourceId,request.skill.rank);
            const Vec at{std::floor(request.target.x)+.5f,std::floor(request.target.y)+.5f};
            auto child=make(burst,at,ports_.monsters.find(successor)->position-at,burst.skill.missileId,
                std::max(1,int(std::lround(burst.skill.missileLifetime*25.f))),burst.skill.missileVelocity,Program::Arc,random);
            child.id=EntityId{ports_.ids.cursor()+1};child.remainingHits=burst.skill.arc->count;child.lastHit=primary;
            children.emplace(child.id,std::move(child));
        }
    }
    auto facts=request.weapon?std::vector<DomainFact>{}:visuals({m});
    if(request.skill.hitOverlayId>=0 && !prepared.value->spells.empty()) for(const auto id:prepared.value->spells.front().targets) facts.emplace_back(OverlayFact{id,1,p->area,request.skill.hitOverlayId});
    if(request.skill.effect==SkillBehavior::ThunderStorm && !prepared.value->spells.empty() && !prepared.value->spells.front().targets.empty())
        facts.emplace_back(SkillPulseFact{p->actor,prepared.value->spells.front().targets.front(),0,1,p->area,request.skill.sourceId,request.skill.rank,request.target});
    if(!ports_.events.hasCapacity(facts.size()+1,2)) return {DomainStatus::Capacity,{}};
    if(request.cost) {
        const auto debit=ports_.transactions.commit(*request.cost);if(!debit) return debit;
    } else if(!request.free) {
        const auto debit=ports_.transactions.release(request.actor,p->characterRevision,request.skill.manaCost,{},request.skill.charge);if(!debit) return debit;
    }
    if(!facts.empty()) ports_.events.publish({0,request.actor.tick,{}, {AudienceKind::Area,{},p->area},std::move(facts)});
    ports_.combat.commitSpells(std::move(*prepared.value));
    for(size_t i=0;i<children.size()+1;++i) ports_.ids.allocate();
    state_.missiles.merge(children);ports_.random=random;
    return {DomainStatus::Applied,std::monostate{}};
}
}
