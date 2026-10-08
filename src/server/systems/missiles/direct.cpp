#include "system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "gameplay/skills/behavior.hpp"
#include "core/random.hpp"
#include <cmath>
#include <algorithm>
namespace d2x::server::missiles {
DomainResult<> System::direct(const Spawn &request, std::vector<EntityId> targets) {
    const auto *p=ports_.players.find(request.actor.player);const auto *a=ports_.areas.find(request.actor.area);
    if(!p || !p->entered || p->actor!=request.actor.actor || p->area!=request.actor.area || p->persistent.player.hp<=0 ||
        !a || a->generation!=request.actor.areaGeneration || a->definition.town) return {DomainStatus::InvalidActor,{}};
    if(ports_.ids.cursor()>=UINT32_MAX) return {DomainStatus::Capacity,{}};
    auto random=ports_.random;
    Missile m; m.id=EntityId{ports_.ids.cursor()};m.player=p->player;m.owner=p->actor;m.area=p->area;m.skill=request.skill;
    m.emitter=request.emitter ? request.emitter : p->actor;m.emitterType=request.emitterType;m.definition=request.skill.missileId;m.position=request.target;m.lifetimeFrames=std::max(1,int(std::lround(request.skill.missileLifetime*25.f)));
    m.damage=int64_t(request.skill.minimumDamage*256.f)+limitedRandom(random,uint32_t((request.skill.maximumDamage-request.skill.minimumDamage)*256.f));
    auto hit=impact(m,std::move(targets));hit.hitClass=uint8_t(request.skill.hitClass);
    if(request.skill.effect==SkillBehavior::StaticField) {
        hit.staticPercent=int(request.skill.staticPercent);hit.staticFloors=p->rules.skills->staticMinimum;
        hit.minimumStaticDamage=int64_t(request.skill.staticMinDamage*256.f);
    }
    if(request.skill.effect==SkillBehavior::Telekinesis) hit.knockback=int(limitedRandom(random,100))<request.skill.telekinesisKnockbackChance;
    if(request.skill.effect==SkillBehavior::FrozenArmor) {hit.type=DamageType::Cold;hit.freeze=true;}
    auto prepared=ports_.combat.prepareSpells({std::move(hit)});if(!prepared) return {prepared.status,{}};
    auto facts=visuals({m});
    if(request.skill.hitOverlayId>=0 && !prepared.value->spells.empty()) for(const auto id:prepared.value->spells.front().targets) facts.emplace_back(OverlayFact{id,1,p->area,request.skill.hitOverlayId});
    if(request.skill.effect==SkillBehavior::ThunderStorm && !prepared.value->spells.empty() && !prepared.value->spells.front().targets.empty())
        facts.emplace_back(SkillPulseFact{p->actor,prepared.value->spells.front().targets.front(),0,1,p->area,request.skill.sourceId,request.skill.rank,request.target});
    if(!ports_.events.hasCapacity(facts.size()+1,2)) return {DomainStatus::Capacity,{}};
    if(!request.free) {
        const auto debit=ports_.transactions.release(request.actor,p->characterRevision,request.skill.manaCost);if(!debit) return debit;
    }
    if(!facts.empty()) ports_.events.publish({0,request.actor.tick,{}, {AudienceKind::Area,{},p->area},std::move(facts)});
    ports_.combat.commitSpells(std::move(*prepared.value));ports_.ids.allocate();ports_.random=random;
    return {DomainStatus::Applied,std::monostate{}};
}
}
