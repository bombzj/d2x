#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/skills/amazon_missile.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x::server::missiles {
void System::weaponImpact(Advance &plan,Vec at) const {
    auto &m=plan.next;
    if(!m.skill.missileImpact) return;
    const auto &owner=*ports_.players.find(m.player);
    auto skill=m.skill.weapon && m.skill.weapon->commonAttack?m.skill:skills::evaluate(owner,m.skill.sourceId,m.skill.rank);
    Spawn request{{m.player,m.owner,m.area,m.generation,0,m.created},skill,{},at,true};
    if(skill.weapon->spear && skill.weapon->spear->kind==SpearSkillSpec::Kind::Fury) {
        const auto &fury=*skill.weapon->spear;const auto &area=ports_.areas.at(m.area);
        std::vector<MissileBurstTarget> candidates;
        for(const auto &[id,target]:ports_.monsters.read().actors) if(target.enemyTarget() && target.life>0 && target.area==m.area &&
            area.definition.collision.missileSegment(at,target.position,{4,1})) candidates.push_back({id,target.position});
        for(const auto &target:missileBurstTargets(at,fury.targetRadius,fury.countBase,candidates))
            plan.children.push_back(make(request,at,target.position-at,fury.childId,
                std::max(1,int(std::lround(fury.childLifetime*25.f))),fury.childSpeed,fury.childKillOnHit?Program::Projectile:Program::FuryBolt,m.random));
    }
    if(skill.weapon->bow && skill.weapon->bow->immolation) {
        const auto &bow=*skill.weapon->bow;
        const auto &area=ports_.areas.at(m.area);
        const Vec center{std::floor(at.x)+.5f,std::floor(at.y)+.5f};
        request.skill.firewall=bow.fire;
        for(const auto offset:missileDiskOffsets(bow.fireRadius)) {
            const auto position=center+offset;
            if(!area.definition.collision.missileSegment(position,position+offset,{4,1})) continue;
            plan.children.push_back(make(request,position,{},bow.fire.fireId,bow.fire.fireFrames,0,Program::Fire,m.random));
        }
        std::vector<EntityId> targets;
        for(const auto &[id,target]:ports_.monsters.read().actors)
            if(target.enemyTarget() && target.life>0 && target.area==m.area &&
                (Vec{std::floor(target.position.x)-std::floor(at.x),std::floor(target.position.y)-std::floor(at.y)}).length()<=bow.explosionRadius)
                targets.push_back(id);
        auto hit=impact(m,std::move(targets),m.weapon->channels[size_t(DamageType::Fire)]);
        hit.type=DamageType::Fire;hit.coldFrames=0;hit.freeze=false;// Range damage and the primary weapon contact are distinct native callbacks.
        hit.occurrence=(uint64_t(m.ageFrames)<<32)|(uint64_t{1}<<31)|m.weaponContacts.size();
        plan.impacts.push_back(std::move(hit));
    }
    if(m.program==Program::GroundThrow && m.weapon && skill.missileImpact->radius>0) {
        std::vector<EntityId> targets;
        for(const auto &[id,target]:ports_.monsters.read().actors) {
            const Vec d{std::floor(target.position.x)-std::floor(at.x),std::floor(target.position.y)-std::floor(at.y)};
            if(target.enemyTarget() && target.life>0 && target.area==m.area && d.x*d.x+d.y*d.y<=skill.missileImpact->radius*skill.missileImpact->radius) targets.push_back(id);
        }
        auto hit=impact(m,std::move(targets),0);hit.type=DamageType::Physical;hit.weapon=m.weapon;
        hit.occurrence=uint64_t(m.ageFrames);plan.impacts.push_back(std::move(hit));
    }
    if(skill.missileImpact->cloudBurst) {
        const auto &burst=*skill.missileImpact->cloudBurst;const auto &cloud=burst.cloud;
        request.skill.minimumDamage=float(cloud.minimum)/256.f;request.skill.maximumDamage=float(cloud.maximum)/256.f;
        request.skill.poisonDuration=float(cloud.poisonFrames)/25.f;
        for(const auto &heading:poisonCloudDirections(burst.mainStep,burst.subStep))
            plan.children.push_back(make(request,at,heading.direction,cloud.missileId,cloud.lifetimeFrames,heading.secondary?burst.subSpeed:burst.mainSpeed,Program::PoisonCloud,m.random));
    }
    if(!skill.missileImpact->areaMissile) return;
    const auto &area=*skill.missileImpact->areaMissile;
    auto child=make(request,at,{},area.missileId,area.delayFrames,0,Program::AreaImpact,m.random);
    if(area.addEquipmentElement) {
        const auto &mods=owner.totals.character.combat;
        const auto ranges=attackElementRanges(mods,owner.totals.equipment.weapons[0].item);
        const auto range=area.element==DamageType::Cold?ranges.cold:ranges.fire;
        child.damage+=int64_t(range.minimum)*256+limitedRandom(m.random,uint32_t(std::max(0,range.maximum-range.minimum))*256);
        if(area.element==DamageType::Cold) {
            int cold=mods.coldFrames;
            if(const auto own=mods.weapons.find(owner.totals.equipment.weapons[0].item);own!=mods.weapons.end()) cold+=own->second.coldFrames;
            child.skill.coldDuration+=float(cold)/25.f;
        }
    }
    plan.children.push_back(std::move(child));
}
System::Advance System::advanceGroundThrow(const Missile &original) const {
    Advance plan{original,{},{},false};auto &m=plan.next;
    const auto &area=ports_.areas.at(m.area);Vec next=m.position+m.velocity*TickContext::seconds;
    const auto wall=missileTerrainContact(area.definition.collision,m.position,next,m.collision);
    if(wall) next=m.position+(next-m.position)* *wall;
    m.position=next;++m.ageFrames;++m.revision;
    if(wall || m.ageFrames>=m.lifetimeFrames) {weaponImpact(plan,m.position);plan.finished=true;}
    return plan;
}
System::Advance System::advanceWeapon(const Missile &original) const {
    Advance plan{original,{},{},false};auto &m=plan.next;const auto &area=ports_.areas.at(m.area);
    const auto &owner=*ports_.players.find(m.player);
    if(m.skill.weapon->bow && m.skill.weapon->bow->guided) {
        const auto &bow=*m.skill.weapon->bow;
        if(m.guidance) {
            const auto *target=ports_.monsters.find(m.guidance);
            if(target && target->enemyTarget() && target->life>0 && target->area==m.area)
                if(const auto heading=missileGuidedDirection(m.position,target->position,m.lifetimeFrames-m.ageFrames,bow.retargetPeriod)) m.velocity=*heading*m.velocity.length();
        } else if(!m.guidanceSearched && m.ageFrames+1>=m.lifetimeFrames) {
            m.guidanceSearched=true;
            for(const auto &[id,target]:ports_.monsters.read().actors) if(target.enemyTarget() && target.life>0 && target.area==m.area) {
                const int distance=missileDistance(m.position,target.position);
                if(distance<=bow.searchRadius && area.definition.collision.missileSegment(m.position,target.position,{4,1}) && (!m.guidance || id<m.guidance)) m.guidance=id;
            }
            m.ageFrames=0;
            m.lifetimeFrames-=owner.rules.skills->definitions.at(m.skill.sourceId).spec.missileRangePerLevel;
            if(m.guidance) m.velocity=(ports_.monsters.find(m.guidance)->position-m.position).unit()*m.velocity.length();
            else m.velocity=m.turnTarget.unit()*m.velocity.length();
        }
    }
    if(m.skill.weapon->spear && m.skill.weapon->spear->poisonTrail) {
        auto skill=skills::evaluate(owner,m.skill.sourceId,m.skill.rank);const auto &cloud=*skill.weapon->spear->poisonTrail;
        skill.minimumDamage=float(cloud.minimum)/256.f;skill.maximumDamage=float(cloud.maximum)/256.f;
        Spawn request{{m.player,m.owner,m.area,m.generation,0,m.created},skill,{},m.position,true};
        auto child=make(request,m.position,{},cloud.missileId,cloud.lifetimeFrames,0,Program::PoisonCloud,m.random);
        plan.children.push_back(std::move(child));
    }
    ++m.ageFrames;Vec next=m.position+m.velocity*TickContext::seconds;
    const auto wall=missileTerrainContact(area.definition.collision,m.position,next,m.collision);
    if(wall) next=m.position+(next-m.position)* *wall;
    if(m.ageFrames>=m.lifetimeFrames) {m.position=next;weaponImpact(plan,m.position);plan.finished=true;return plan;}
    std::vector<std::pair<float,EntityId>> contacts;
    const bool active=!m.skill.weapon->bow || m.ageFrames>=m.skill.weapon->bow->activateFrames;
    for(const auto candidate:collisionCandidates(m,next)) {
        const auto *found=ports_.monsters.find(candidate);if(!found) continue;
        const auto &target=*found;const auto id=target.id;
        if(active && target.enemyTarget() && target.life>0 && target.area==m.area && !m.weaponContacts.contains(id))
        if(!m.skill.weapon->bow || !m.skill.weapon->bow->guided || (m.guidance && id==m.guidance) || (!m.guidance && m.guidanceSearched))
        if(const auto fraction=missileUnitIntersection(m.position,next,m.collision.size,target.position,target.rule.size)) contacts.emplace_back(*fraction,id);
    }
    std::sort(contacts.begin(),contacts.end());
    const Vec start=m.position;
    for(const auto &[fraction,id]:contacts) {
        m.position=start+(next-start)*fraction;m.lastHit=id;m.weaponContacts.insert(id);
        combat::SpellImpact hit{m.id,m.owner,m.area,DamageType::Physical,0,{id}};
        hit.binding=m.binding;
        hit.weapon=m.weapon;hit.weaponHit=ports_.combat.weaponContact(*m.weapon,id,m.created+uint64_t(m.ageFrames),m.random);hit.coldFrames=uint64_t(std::max(0,m.weapon->coldFrames));hit.freeze=m.weapon->freeze;
        hit.occurrence=(uint64_t(m.ageFrames)<<32)|m.weaponContacts.size();
        hit.coldDivisor=owner.rules.skills->coldDivisor;hit.freezeDivisor=owner.rules.skills->freezeDivisor;
        const bool hitSuccessful=*hit.weaponHit;
        plan.impacts.push_back(std::move(hit));
        if(hitSuccessful || owner.rules.skills->alwaysExplodingMissiles.contains(m.definition)) weaponImpact(plan,m.position);
        if(!hitSuccessful || m.pierces<=0) {plan.finished=true;break;}
        --m.pierces;
    }
    if(!plan.finished) m.position=next;
    ++m.revision;if(wall && !plan.finished) {weaponImpact(plan,m.position);plan.finished=true;}
    return plan;
}
}
