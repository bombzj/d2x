#include "system.hpp"
#include "evaluation.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/movement.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/skills/weapon_damage.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/amazon_sequence.hpp"
#include "gameplay/skills/weapon_volley.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/skills/common_actions.hpp"
#include <algorithm>
namespace d2x::server::skills {
namespace {
template<class T> bool unsupportedWeaponEffects(const T &mods) {
    return mods.lifeLeech || mods.manaLeech || mods.crushingBlow || mods.openWounds;
}
}
DomainResult<> System::avoidance(const ActorContext &actor,WeaponAvoidance result,EntityId attacker) {
    const auto &p=*ports_.players.find(actor.player);
    if(result==WeaponAvoidance::Evade) {
        const auto output=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},{SoundFact{actor.actor,0,actor.area,12}}});
        return {output.status,output?std::optional{std::monostate{}}:std::nullopt};
    }
    const int id=result==WeaponAvoidance::Dodge?13:18;
    const auto rank=p.totals.skillRanks.find(id);if(rank==p.totals.skillRanks.end()) return {DomainStatus::Unavailable,{}};
    const auto animation=p.rules.skills->weaponAnimations.find("s1"+p.totals.equipment.animationClass);
    if(animation==p.rules.skills->weaponAnimations.end()) return {DomainStatus::Unavailable,{}};
    const auto &a=animation->second;
    std::map<EntityId,Cast> candidate;candidate.emplace(actor.actor,Cast{actor.actor,uint16_t(id),actor.tick,actor.tick,actor.tick+uint64_t(std::max(1,(a.frames*256+a.speed-1)/a.speed)),actor.area,attacker});
    const auto output=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},{AttackFact{actor.actor,attacker,0,1,actor.area,p.position,p.position,actor.tick,uint16_t(id),uint8_t(rank->second),true}}});
    if(!output) return {output.status,{}};
    cancel(actor.player,actor.actor);state_.casts.erase(actor.actor);state_.casts.merge(candidate);ports_.movement.execute(actor,{MovementAction::Stop,{},false});
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::weaponCast(const ActorContext &actor,const Request &request,int id) {
    const auto &p=*ports_.players.find(actor.player);const auto &area=ports_.areas.at(actor.area);
    const auto &rules=*p.rules.skills;
    if(area.definition.town || !request.target) return {DomainStatus::Unavailable,{}};
    if(busy(actor.actor,actor.tick)) return {DomainStatus::Conflict,{}};
    const auto rank=p.totals.skillRanks.find(id);
    const auto charge=chargedSource(p,id,request.right);
    const auto owner=p.persistent.player.selectedSkillOwners.at(p.persistent.player.weaponSet*2+(request.right?1:0));
    if(owner!=UINT32_MAX && !charge) return {DomainStatus::Unavailable,{}};
    const int effectiveRank=charge?charge->rank:p.rules.character->innateSkills.contains(id)?1:(rank==p.totals.skillRanks.end()?0:rank->second);
    if(effectiveRank<=0 || effectiveRank>255) return {DomainStatus::Unavailable,{}};
    auto skill=evaluate(p,id,effectiveRank);
    if(charge) {skill.charge=SkillCharge{charge->item,charge->layer};skill.manaCost=skill.startMana=0;}
    const auto &program=*skill.weapon;
    if(program.smite && !p.totals.equipment.shield) return {DomainStatus::Unavailable,{}};
    if(p.persistent.player.mana<skill.manaCost && program.spear && program.spear->attackWithoutMana) return attack(actor,request,0);
    const auto *weapon=program.commonAttack?commonAttackWeapon(p.totals.equipment,program.thrown,program.leftHand):std::find_if(p.totals.equipment.weapons.begin(),p.totals.equipment.weapons.begin()+p.totals.equipment.weaponCount,
        [&](const auto &w){return !w.leftHand && (program.smite || std::find(w.types.begin(),w.types.end(),program.requiredType)!=w.types.end());});
    if(!weapon || weapon==p.totals.equipment.weapons.begin()+p.totals.equipment.weaponCount || weapon->fasterAttack<=-120 ||
        (program.thrown && !weapon->throwable) || p.persistent.player.mana<skill.manaCost) return {DomainStatus::Unavailable,{}};
    if(program.commonAttack && (weapon->ranged || program.thrown)) {
        if(!weapon->projectile) return {DomainStatus::Unavailable,{}};
        skill=commonProjectileSkill(skill,*weapon->projectile);
    }
    const auto &mods=p.totals.character.combat;
    if(!weapon->potion && unsupportedWeaponEffects(mods)) return {DomainStatus::NotImplemented,{}};
    if(const auto own=mods.weapons.find(weapon->item);own!=mods.weapons.end() && !weapon->potion && unsupportedWeaponEffects(own->second)) return {DomainStatus::NotImplemented,{}};
    const bool fend=program.spear && program.spear->kind==SpearSkillSpec::Kind::Fend;
    const bool zeal=skill.effect==SkillBehavior::Zeal;
    Vec destination;EntityId target;
    if(const auto *unit=std::get_if<UnitTarget>(&*request.target)) {
        if(unit->type!=1) return {DomainStatus::InvalidRequest,{}};
        const auto *monster=ports_.monsters.find(unit->id);
        if(!monster || monster->owner || monster->life<=0 || monster->area!=actor.area) return {DomainStatus::InvalidRequest,{}};
        destination=monster->position;target=monster->id;
        if(program.chargeVelocity>0 && meleeDistance(p.position,2,destination,monster->rule.size)<=weapon->rangeAdder+1) return attack(actor,request,0);
        if(program.chargeVelocity==0 && (!weapon->ranged || program.smite) && !program.thrown && (meleeDistance(p.position,2,destination,monster->rule.size)>weapon->rangeAdder+1 ||
            !area.definition.collision.segment(p.position,destination))) {
            if(request.stationary) return {DomainStatus::Unavailable,{}};
            ports_.movement.execute(actor,{MovementAction::Move,destination,false});return {DomainStatus::Conflict,{}};
        }
    } else {
        if(!weapon->ranged && !program.thrown && !fend && !zeal && !program.commonAttack && !program.chargeVelocity) return {DomainStatus::InvalidRequest,{}};
        const auto &point=std::get<PointTarget>(*request.target);
        if(point.area!=actor.area || point.generation!=actor.areaGeneration) return {DomainStatus::Stale,{}};
        destination=point.position;
    }
    if(!std::isfinite(destination.x) || !std::isfinite(destination.y) || std::abs(destination.x-p.position.x)>50 || std::abs(destination.y-p.position.y)>50 ||
        destination.x<0 || destination.y<0 || destination.x>=area.definition.collision.width || destination.y>=area.definition.collision.height) return {DomainStatus::InvalidRequest,{}};
    const auto mode=program.mode.empty()?(program.thrown?"th":"a1"):program.mode.c_str();
    const auto animation=rules.weaponAnimations.find(std::string(mode)+p.totals.equipment.animationClass);
    if(animation==rules.weaponAnimations.end()) return {DomainStatus::Unavailable,{}};
    const auto &a=animation->second;
    WeaponAttackTiming timing{mode,a.frames,effectiveAttackSpeed(a.speed,weapon->fasterAttack,weapon->baseSpeed,p.totals.character.combat.attackRate),a.actionFrame,a.startFrame};
    std::vector<int> hits{timing.actionTick()};
    if(program.spear && (program.spear->kind==SpearSkillSpec::Kind::Jab || program.spear->kind==SpearSkillSpec::Kind::Impale)) {
        const auto sequence=amazonWeaponSequence(program.spear->kind==SpearSkillSpec::Kind::Jab?1:8,p.totals.equipment.animationClass);
        if(sequence.frames.empty()) return {DomainStatus::Unavailable,{}};
        timing.speed=effectiveAttackSpeed(256,weapon->fasterAttack,weapon->baseSpeed,p.totals.character.combat.attackRate-30);
        timing.frames=int(sequence.frames.size());timing.startFrame=0;hits.clear();
        for(size_t i=0;i<sequence.frames.size();++i) if(sequence.frames[i].hit) hits.push_back(weaponSequenceTick(int(i),timing.speed));
    }
    const bool strafe=program.bow && program.bow->strafe;
    int volleyDuration=timing.durationTicks();
    if(strafe || fend || zeal) {
        std::vector<uint64_t> targets;
        for(const auto &[id,m]:ports_.monsters.read().actors) if(!m.owner && m.life>0 && m.area==actor.area && (strafe?missileDistance(p.position,m.position)<=program.bow->targetRadius && area.definition.collision.missileSegment(p.position,m.position,{4,1}):meleeDistance(p.position,2,m.position,m.rule.size)<=weapon->rangeAdder+1 && area.definition.collision.segment(p.position,m.position))) targets.push_back(id.value);
        const int count=targets.empty()?0:zeal?program.attacks:strafe?strafeShotCount(int(targets.size()),program.attacks,program.bow->minimumShots):std::min(int(targets.size()),program.attackLimit);
        if(!count) return {DomainStatus::Unavailable,{}};
        if(!target || std::find(targets.begin(),targets.end(),target.value)==targets.end()) target=EntityId{missileChainSuccessor(0,targets)};
        destination=ports_.monsters.find(target)->position;
        const auto volley=weaponVolley(timing,count,program.rollbackPercent);
        hits=volley.hits;volleyDuration=int(volley.frames.size());
        if(hits.empty()) return {DomainStatus::Unavailable,{}};
    }
    auto prior=state_.casts.find(actor.actor);
    if(prior!=state_.casts.end() && skill.delayFrames>0 && prior->second.cooldownUntil>actor.tick) return {DomainStatus::Conflict,{}};
    auto cost=ports_.inventory.weaponCost(actor,*weapon,skill,!program.manaOnRelease,strafe);if(!cost) return {cost.status,{}};
    const auto fact=AttackFact{actor.actor,target,0,1,actor.area,p.position,destination,actor.sequence,uint16_t(id),uint8_t(effectiveRank)};
    if(auto *edit=std::get_if<transactions::CharacterEdit>(&cost.value->change)) edit->publicFacts.emplace_back(fact);
    else std::get<transactions::InventoryEdit>(cost.value->change).publicFacts.emplace_back(fact);
    Release release{actor,skill,{}, {actor.area,actor.areaGeneration,destination},target,actor.tick+uint64_t(hits.front())};
    release.weapon=*weapon;release.manaPaid=!program.manaOnRelease;release.weaponHits=std::move(hits);
    release.weaponSpeed=timing.speed;release.weaponFrames=timing.frames;release.weaponRollback=program.rollbackPercent;
    if(program.smite) release.shield=p.totals.equipment.shield;
    if(program.chargeVelocity>0) {
        release.charging=true;release.tick=actor.tick+1;
        release.chargeSpeed=float(p.definition.runVelocity)*25.f/16.f*float(program.chargeVelocity+std::max(50,p.totals.character.velocityPercent))/100.f;
    }
    auto cast=Cast{actor.actor,uint16_t(id),actor.tick,actor.sequence,actor.tick+uint64_t(volleyDuration),actor.area,target};
    if(program.chargeVelocity>0) cast.until=actor.tick+250;
    if(prior!=state_.casts.end()) cast.cooldownUntil=prior->second.cooldownUntil;
    // Allocate candidate nodes before publishing inventory/mana/action as one transaction.
    std::map<EntityId,Release> preparedRelease;preparedRelease.emplace(actor.actor,std::move(release));
    std::map<EntityId,Cast> preparedCast;preparedCast.emplace(actor.actor,cast);
    const auto committed=ports_.transactions.commit(std::move(*cost.value));if(!committed) return committed;
    releases_.erase(actor.actor);releases_.merge(preparedRelease);state_.casts.erase(actor.actor);state_.casts.merge(preparedCast);
    ports_.movement.execute(actor,{MovementAction::Stop,{},false});return {DomainStatus::Applied,std::monostate{}};
}
DomainStatus System::weaponRelease(Release &pending,const ActorContext &actor,Vec destination) {
    const auto &p=*ports_.players.find(actor.player);
    const auto found=std::find_if(p.totals.equipment.weapons.begin(),p.totals.equipment.weapons.begin()+p.totals.equipment.weaponCount,
        [&](const auto &w){return w.item==pending.weapon->item && w.weaponClass==pending.weapon->weaponClass && w.leftHand==pending.weapon->leftHand;});
    if(found==p.totals.equipment.weapons.begin()+p.totals.equipment.weaponCount) return DomainStatus::Stale;
    if(pending.skill.weapon->smite && (!pending.shield || pending.shield!=p.totals.equipment.shield)) return DomainStatus::Stale;
    if(pending.skill.weapon->commonAttack && commonAttackWeapon(p.totals.equipment,pending.skill.weapon->thrown,pending.skill.weapon->leftHand)!=&*found) return DomainStatus::Stale;
    const bool strafe=pending.skill.weapon->bow && pending.skill.weapon->bow->strafe;
    const bool fend=pending.skill.weapon->spear && pending.skill.weapon->spear->kind==SpearSkillSpec::Kind::Fend;
    const bool zeal=pending.skill.effect==SkillBehavior::Zeal;
    EntityId selectedTarget=pending.unit;
    if(strafe || fend || zeal) {
        std::vector<uint64_t> targets;
        const auto &area=ports_.areas.at(actor.area);
        for(const auto &[id,m]:ports_.monsters.read().actors) if(!m.owner && m.life>0 && m.area==actor.area && (strafe?missileDistance(p.position,m.position)<=pending.skill.weapon->bow->targetRadius && area.definition.collision.missileSegment(p.position,m.position,{4,1}):meleeDistance(p.position,2,m.position,m.rule.size)<=found->rangeAdder+1 && area.definition.collision.segment(p.position,m.position))) targets.push_back(id.value);
        auto successor=missileChainSuccessor(pending.unit.value,targets);
        if(!successor && targets.size()==1) successor=targets.front();
        const auto target=EntityId{!pending.nextWeaponHit && std::find(targets.begin(),targets.end(),pending.unit.value)!=targets.end()?pending.unit.value:successor};
        if(!target) return DomainStatus::Unavailable;
        selectedTarget=target;destination=ports_.monsters.find(target)->position;
    }
    const bool projectile=(found->ranged && !pending.skill.weapon->smite) || pending.skill.weapon->thrown;
    auto event=ports_.effects.prepareItemEvents(actor,{ItemSkillEvent::Attack},selectedTarget,destination);if(!event) return event.status;
    if(pending.skill.charge) {
        const auto source=p.persistent.inventory.items.find(pending.skill.charge->item.id);
        if(source==p.persistent.inventory.items.end() || !p.totals.activeEquipment.contains(source->first)) return DomainStatus::Stale;
        pending.skill.charge->item=source->second.handle();
    }
    std::optional<transactions::Plan> cost;
    if(!pending.manaPaid || (projectile && !strafe && !pending.skill.weapon->noAmmo)) {
        auto prepared=ports_.inventory.weaponCost(actor,*found,pending.skill,!pending.manaPaid,projectile && !strafe);
        if(!prepared) return prepared.status;
        cost=std::move(*prepared.value);
    }
    if(projectile) {
        WeaponSkillDamage snapshot;snapshot.weapon=*found;snapshot.level=p.persistent.player.level;
        missiles::Spawn request{actor,pending.skill,{},destination,true};request.weapon=snapshot;request.cost=std::move(cost);
        request.guidedTarget=selectedTarget;
        const auto result=ports_.missiles.spawn(request).status;
        if(result==DomainStatus::Applied) {pending.unit=selectedTarget;ports_.effects.commitItemEvents(std::move(*event.value));}
        return result;
    }
    if(pending.skill.weapon->commonAttack && !selectedTarget) {
        const auto result=cost?ports_.transactions.commit(std::move(*cost)).status:DomainStatus::Applied;
        if(result==DomainStatus::Applied) ports_.effects.commitItemEvents(std::move(*event.value));
        return result;
    }
    const auto *target=ports_.monsters.find(selectedTarget);
    const auto &area=ports_.areas.at(actor.area);
    if(!target || target->owner || target->life<=0 || target->area!=actor.area ||
        meleeDistance(p.position,2,target->position,target->rule.size)>found->rangeAdder+1 || !area.definition.collision.segment(p.position,target->position)) return DomainStatus::Unavailable;
    WeaponSkillDamage snapshot;snapshot.weapon=*found;snapshot.level=p.persistent.player.level;
    missiles::Spawn request{actor,pending.skill,{},destination,true};request.weapon=snapshot;request.cost=std::move(cost);
    const auto result=ports_.missiles.direct(request,{selectedTarget}).status;
    if(result==DomainStatus::Applied) {pending.unit=selectedTarget;ports_.effects.commitItemEvents(std::move(*event.value));}
    return result;
}
}
