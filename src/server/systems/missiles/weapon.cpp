#include "system.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "runtime.hpp"
#include "server/systems/combat/system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/skills/amazon_missile.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x::server::missiles {
void System::weaponImpact(Advance &plan,Vec at) const {
    auto &m=plan.next;
    if(!m.skill.missileImpact) return;
    const auto *owner=ports_.players.find(m.player);if(owner && (!owner->entered || owner->actor!=m.playerActor)) owner=nullptr;
    if(!owner && !ports_.monsters.find(m.owner)) return;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    auto skill=!owner || (m.skill.weapon && m.skill.weapon->commonAttack)?m.skill:skills::evaluate(*owner,m.skill.sourceId,m.skill.rank);
    const bool refresh=owner && m.owner==owner->actor && !(m.skill.weapon && m.skill.weapon->commonAttack);
    Spawn request{{m.player,m.owner,m.area,m.generation,0,m.created},skill,{},at,true};
    if(skill.weapon->spear && skill.weapon->spear->kind==SpearSkillSpec::Kind::Fury) {
        const auto &fury=*skill.weapon->spear;const auto &area=ports_.areas.at(m.area);
        std::vector<MissileBurstTarget> candidates;
        for(const auto id:nearbyUnits(m)) {
            const auto target=participants.find(id);const auto point=unitPosition(m,id);
            if(point && participants.canHarm(m.owner,id) && area.definition.collision.missileSegment(at,*point,{4,1})) candidates.push_back({id,*point});
        }
        for(const auto &target:missileBurstTargets(at,fury.targetRadius,fury.countBase,candidates))
            planChild(plan,request,at,target.position-at,fury.childId,
                std::max(1,int(std::lround(fury.childLifetime*25.f))),fury.childSpeed,fury.childKillOnHit?Program::Projectile:Program::FuryBolt,refresh);
    }
    if(skill.weapon->bow && skill.weapon->bow->immolation) {
        const auto &bow=*skill.weapon->bow;
        const auto &area=ports_.areas.at(m.area);
        const Vec center{std::floor(at.x)+.5f,std::floor(at.y)+.5f};
        request.skill.firewall=bow.fire;
        for(const auto offset:missileDiskOffsets(bow.fireRadius)) {
            const auto position=center+offset;
            if(!area.definition.collision.missileSegment(position,position+offset,{4,1})) continue;
            planChild(plan,request,position,{},bow.fire.fireId,bow.fire.fireFrames,0,Program::Fire,refresh,
                [](Missile &fire,const SkillCastSpec &current){fire.skill.firewall=current.weapon->bow->fire;});
        }
        areaImpact(plan,at,bow.explosionRadius);
    }
    if(m.program==Program::GroundThrow && m.weapon && skill.missileImpact->radius>0)
        areaImpact(plan,at,int(skill.missileImpact->radius));

    if(skill.missileImpact->cloudBurst) {
        const auto &burst=*skill.missileImpact->cloudBurst;const auto &cloud=burst.cloud;
        request.skill.minimumDamage=Fixed8::fromRaw(cloud.minimum);request.skill.maximumDamage=Fixed8::fromRaw(cloud.maximum);
        request.skill.poisonDuration=float(cloud.poisonFrames)/25.f;request.skill.poisonDurationFrames=cloud.poisonFrames;
        for(const auto &heading:poisonCloudDirections(burst.mainStep,burst.subStep))
            planChild(plan,request,at,heading.direction,cloud.missileId,cloud.lifetimeFrames,heading.secondary?burst.subSpeed:burst.mainSpeed,Program::PoisonCloud,refresh,
                [](Missile &child,const SkillCastSpec &current) {
                    const auto &cloud=current.missileImpact->cloudBurst->cloud;
                    child.skill.minimumDamage=Fixed8::fromRaw(cloud.minimum);child.skill.maximumDamage=Fixed8::fromRaw(cloud.maximum);
                    child.skill.poisonDuration=float(cloud.poisonFrames)/25.f;child.skill.poisonDurationFrames=cloud.poisonFrames;
                    child.damageRange=MissileDamageRange{cloud.minimum,cloud.maximum};
                });
    }
    if(!skill.missileImpact->areaMissile) return;
    const auto &area=*skill.missileImpact->areaMissile;
    planChild(plan,request,at,{},area.missileId,area.delayFrames,0,Program::AreaImpact,
        refresh,[this,player=m.player](Missile &child,const SkillCastSpec &current) {
            const auto &area=*current.missileImpact->areaMissile;
            if(child.nativeDamage || !area.addEquipmentElement) return;
            const auto &owner=*ports_.players.find(player);const auto &mods=owner.totals.character.combat;
            const auto ranges=attackElementRanges(mods,owner.totals.equipment.weapons[0].item);
            const auto range=area.element==DamageType::Cold?ranges.cold:ranges.fire;
            child.damageRange=addMissileDamage(*child.damageRange,fixedMissileDamageRange(int64_t(range.minimum)*256,int64_t(range.maximum)*256));
            if(area.element==DamageType::Cold) {
                int cold=mods.coldFrames;
                if(const auto own=mods.weapons.find(owner.totals.equipment.weapons[0].item);own!=mods.weapons.end()) cold+=own->second.coldFrames;
                child.skill.coldDuration+=float(cold)/25.f;child.skill.coldDurationFrames=nativeAdd(child.skill.coldDurationFrames.value_or(0),cold);
            }
        });
}
System::Advance System::advanceGroundThrow(const Missile &original) const {
    Advance plan{original,{},{},false};plan.damageRolls=damageRolls(original);auto &m=plan.next;
    const auto moved=moveMissile(m);const auto next=moved.position;const bool wall=moved.blocked;
    m.position=next;if(!wall) ++m.ageFrames;++m.revision;
    if(wall || m.ageFrames>=m.lifetimeFrames) {weaponImpact(plan,m.position);plan.finished=true;}
    return plan;
}
System::Advance System::advanceWeapon(const Missile &original) const {
    Advance plan{original,{},{},false};plan.damageRolls=damageRolls(original);auto &m=plan.next;const auto &area=ports_.areas.at(m.area);
    const auto *owner=ports_.players.find(m.player);if(owner && (!owner->entered || owner->actor!=m.playerActor)) owner=nullptr;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    if(m.skill.weapon->bow && m.skill.weapon->bow->guided) {
        const auto &bow=*m.skill.weapon->bow;
        if(m.guidance) {
            const auto target=participants.find(m.guidance);
            if(unitPosition(m,m.guidance) && participants.canHarm(m.owner,m.guidance))
                if(const auto heading=missileGuidedDirection(m.position,*unitPosition(m,m.guidance),m.lifetimeFrames-m.ageFrames,bow.retargetPeriod)) m.velocity=*heading*m.velocity.length();
        } else if(!m.guidanceSearched && m.ageFrames+1>=m.lifetimeFrames) {
            m.guidanceSearched=true;
            for(const auto id:nearbyUnits(m)) {
                const auto target=participants.find(id);
                const auto point=unitPosition(m,id);if(!point || !participants.canHarm(m.owner,id)) continue;
                const int distance=missileDistance(m.position,*point);
                if(distance<=bow.searchRadius && area.definition.collision.missileSegment(m.position,*point,{4,1}) && (!m.guidance || id<m.guidance)) m.guidance=id;
            }
            m.ageFrames=0;
            m.lifetimeFrames-=m.rules->definitions.at(m.skill.sourceId).spec.missileRangePerLevel;
            if(m.guidance) m.velocity=(*unitPosition(m,m.guidance)-m.position).unit()*m.velocity.length();
            else m.velocity=m.turnTarget.unit()*m.velocity.length();
        }
    }
    if(m.skill.weapon->spear && m.skill.weapon->spear->poisonTrail) {
        auto skill=owner?skills::evaluate(*owner,m.skill.sourceId,m.skill.rank):m.skill;const auto &cloud=*skill.weapon->spear->poisonTrail;
        skill.minimumDamage=Fixed8::fromRaw(cloud.minimum);skill.maximumDamage=Fixed8::fromRaw(cloud.maximum);
        Spawn request{{m.player,m.owner,m.area,m.generation,0,m.created},skill,{},m.position,true};
        planChild(plan,request,m.position,{},cloud.missileId,cloud.lifetimeFrames,0,Program::PoisonCloud,true,
            [](Missile &child,const SkillCastSpec &current) {
                const auto &cloud=*current.weapon->spear->poisonTrail;
                child.skill.minimumDamage=Fixed8::fromRaw(cloud.minimum);child.skill.maximumDamage=Fixed8::fromRaw(cloud.maximum);
                child.damageRange=MissileDamageRange{cloud.minimum,cloud.maximum};
            });
    }
    const auto moved=moveMissile(m);const auto next=moved.position;const bool wall=moved.blocked;
    if(!wall && !moved.pathFinished) ++m.ageFrames;
    if(m.ageFrames>=m.lifetimeFrames || moved.pathFinished) {m.position=next;weaponImpact(plan,m.position);plan.finished=true;return plan;}
    auto contacts=wall?std::vector<std::pair<float,EntityId>>{}:collisionContacts(m,next);
    if(!plan.damageRolls) throw std::logic_error("Weapon missile lacks native damage ranges");
    plan.collisionFlight=Advance::CollisionFlight{m.position,next,std::move(contacts),0,bool(wall),true};
    continueMissileFlight(plan);
    return plan;
}
void System::continueMissileFlight(Advance &plan) const {
    auto &m=plan.next;auto &flight=*plan.collisionFlight;
    if(!flight.continueFlight) {plan.finished=true;return;}
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    while(flight.next<flight.contacts.size()) {
        const auto [fraction,id]=flight.contacts[flight.next++];const auto target=participants.find(id);
        if(!id) {
            m.position=flight.end;
            if(!nativeHit(plan,{})) {if(flight.weapon) weaponImpact(plan,m.position);else if(m.enemy) monsterCollision(plan,{});else projectileImpact(plan,{});}
            plan.finished=true;return;
        }
        if(!unitPosition(m,id) || !canCollide(m,id)) continue;
        const auto &nativeRule=m.enemy?m.enemy->rule:m.nativeDamage->rule;
        if(nativeRule.nextHit && !ports_.effects.missileHitAllowed(id,m.executionTick)) continue;
        m.position=flight.end;m.lastHit=id;
        // HandleMissileCollision returns after the first selected unit.
        // A piercing missile resumes from its path endpoint next frame.
        flight.next=flight.contacts.size();
        if(flight.weapon) m.weaponContacts.insert(id);
        if(!flight.weapon) {
            auto hit=m.enemy?enemyImpact(plan,{id}):impact(plan,{id});hit.occurrence=(uint64_t(m.ageFrames)<<32)|flight.next;
            plan.addMissileContact(std::move(hit));return;
        }
        combat::SpellImpact hit{m.id,m.owner,unitArea(id),DamageType::Physical,0,{id}};
        hit.binding=m.binding;hit.suppressSourceEvents=m.suppressSourceEvents;hit.weapon=m.weapon;hit.missileDamage=plan.damageRolls;
        const auto &rule=m.nativeDamage->rule;
        hit.allocatedMissile=true;hit.collideFriend=rule.collideFriend;
        hit.monsterToHit=rule.toHit;hit.monsterRating=m.nativeDamage->attackBonus;
        hit.returnFire=rule.returnFire;hit.hitClass=uint8_t(rule.hitClass);hit.nextDelay=uint64_t(std::max(0,rule.nextDelay));
        if(rule.nextHit) hit.nativeNextHit=rule.nextHitState;
        const auto &states=*m.rules;
        const auto *owner=ports_.players.findActor(m.owner);
        hit.monsterStates.poison=states.poisoned;hit.monsterStates.cold=states.chilled;
        const auto modifiers=ports_.effects.combatModifiers(m.owner,m.executionTick);
        hit.coldPierce=modifiers.coldPierce;hit.poisonPierce=modifiers.poisonPierce;
        
        hit.occurrence=(uint64_t(m.ageFrames)<<32)|m.weaponContacts.size();
        plan.addMissileContact(std::move(hit));return;
    }
    m.position=flight.end;
    if(flight.wall) {
        if(!nativeHit(plan,{})) {if(flight.weapon) weaponImpact(plan,m.position);else if(m.enemy) monsterCollision(plan,{});else projectileImpact(plan,{});}
        plan.finished=true;
    }
    ++m.revision;
}

}
