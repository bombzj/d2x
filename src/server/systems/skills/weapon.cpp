#include "system.hpp"
#include "runtime.hpp"
#include "evaluation.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/movement.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/skills/weapon_damage.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/amazon_sequence.hpp"
#include "gameplay/skills/barbarian_sequence.hpp"
#include "gameplay/skills/weapon_volley.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/skills/common_actions.hpp"
#include <algorithm>
namespace d2x::server::skills {
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
    const auto output=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},{AttackFact{actor.actor,attacker,0,uint8_t(ports_.players.findActor(attacker)?0:1),actor.area,p.position,p.position,actor.tick,uint16_t(id),uint8_t(rank->second),true}}});
    if(!output) return {output.status,{}};
    cancel(actor.player,actor.actor);state_.casts.erase(actor.actor);state_.casts.merge(candidate);ports_.movement.execute(actor,{MovementAction::Stop,{},false});
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::weaponCast(const ActorContext &actor,const Request &request,int id) {
    auto &releases_=runtime_->releases;
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
    if(p.persistent.player.mana<skill.manaCost && (program.frenzy || (program.spear && program.spear->attackWithoutMana) || skill.effect==SkillBehavior::PoisonDagger)) return attack(actor,request,0);
    const auto *weapon=program.commonAttack?commonAttackWeapon(p.totals.equipment,program.thrown,program.leftHand):std::find_if(p.totals.equipment.weapons.begin(),p.totals.equipment.weapons.begin()+p.totals.equipment.weaponCount,
        [&](const auto &w){return !w.leftHand && (program.smite || std::find(w.types.begin(),w.types.end(),program.requiredType)!=w.types.end());});
    if(!weapon || weapon==p.totals.equipment.weapons.begin()+p.totals.equipment.weaponCount || weapon->fasterAttack<=-120 ||
        (program.thrown && !weapon->throwable) || p.persistent.player.mana<skill.manaCost) return {DomainStatus::Unavailable,{}};
    if((program.commonAttack || program.dualWield) && (weapon->ranged || program.thrown)) {
        if(!weapon->projectile) return {DomainStatus::Unavailable,{}};
        skill=commonProjectileSkill(skill,*weapon->projectile);
    }
    const WeaponDamage *offhand=nullptr;
    if(program.dualWield) {
        const auto last=p.totals.equipment.weapons.begin()+p.totals.equipment.weaponCount;
        const auto second=std::find_if(p.totals.equipment.weapons.begin(),last,[&](const auto &w){return w.leftHand && w.item &&
            std::find(w.types.begin(),w.types.end(),program.requiredType)!=w.types.end() && (!program.thrown || w.throwable);});
        if(second==last || !weapon->item) return {DomainStatus::Unavailable,{}};
        offhand=&*second;
    }
    if(program.whirlwind) for(const auto &other:p.totals.equipment.weapons) if(other.item && other.leftHand && std::find(other.types.begin(),other.types.end(),program.requiredType)!=other.types.end()) offhand=&other;
    const bool fend=program.spear && program.spear->kind==SpearSkillSpec::Kind::Fend;
    const bool zeal=skill.effect==SkillBehavior::Zeal;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    Vec destination;EntityId target;uint8_t targetType=1;
    if(const auto *unit=std::get_if<UnitTarget>(&*request.target)) {
        const auto victim=participants.find(unit->id);
        if(unit->type>1 || bool(victim.player)!=(unit->type==0) || !victim.aliveIn(actor.area) || !participants.canHarm(actor.actor,unit->id)) return {DomainStatus::InvalidRequest,{}};
        destination=victim.position();target=unit->id;targetType=unit->type;
        if((program.chargeVelocity>0 || program.whirlwind) && meleeDistance(p.position,2,destination,victim.size())<=weapon->rangeAdder+1) return attack(actor,request,0);
        if(!program.whirlwind && program.chargeVelocity==0 && (!weapon->ranged || program.smite) && !program.thrown &&
            !playerMeleeReach(area.definition.collision,p.position,destination,victim.size(),weapon->rangeAdder+int(victim.moving()))) {
            if(request.stationary) return {DomainStatus::Unavailable,{}};
            const auto movement=ports_.movement.execute(actor,{MovementAction::Move,destination,true,{},MotionTarget{unit->id,unit->type}});
            return {movement==CommandStatus::Applied?DomainStatus::Conflict:DomainStatus::Unavailable,{}};
        }
    } else {
        if(!weapon->ranged && !program.thrown && !fend && !zeal && !program.commonAttack && !program.chargeVelocity && !program.whirlwind) return {DomainStatus::InvalidRequest,{}};
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
    if(program.sequence==11 || program.sequence==15) {
        timing.sequence=true;
        const auto sequence=barbarianSequence(program.sequence,p.totals.equipment.animationClass);
        timing.speed=effectiveAttackSpeed(256,weapon->fasterAttack,weapon->baseSpeed,p.totals.character.combat.attackRate+program.attackRateBonus-30);
        timing.frames=int(sequence.size());timing.startFrame=0;hits=weaponSequenceHitTicks(sequence,timing.speed);
        if(hits.empty()) return {DomainStatus::Unavailable,{}};
    }
    if(program.spear && (program.spear->kind==SpearSkillSpec::Kind::Jab || program.spear->kind==SpearSkillSpec::Kind::Impale)) {
        timing.sequence=true;
        const auto sequence=amazonWeaponSequence(program.spear->kind==SpearSkillSpec::Kind::Jab?1:8,p.totals.equipment.animationClass);
        if(sequence.frames.empty()) return {DomainStatus::Unavailable,{}};
        timing.speed=effectiveAttackSpeed(256,weapon->fasterAttack,weapon->baseSpeed,p.totals.character.combat.attackRate-30);
        timing.frames=int(sequence.frames.size());timing.startFrame=0;hits=weaponSequenceHitTicks(sequence.frames,timing.speed);
    }
    const bool strafe=program.bow && program.bow->strafe;
    int volleyDuration=timing.durationTicks();
    if(program.whirlwind) {
        const float speed=float(p.definition.runVelocity)*25.f/16.f*float(std::max(50,p.totals.character.velocityPercent))/100.f;
        if(speed<=0 || (destination-p.position).length()<.001f || !area.definition.collision.nativeMovementSegment(p.position,destination,{0x804,2})) return {DomainStatus::Unavailable,{}};
        volleyDuration=int(std::ceil((destination-p.position).length()*25.f/speed))+1;hits={4};
    }
    if(strafe || fend || zeal) {
        std::vector<uint64_t> targets;
        for(const auto id:participants.enemies(actor.actor,actor.area)) {
            const auto m=participants.find(id);
            if(strafe?missileDistance(p.position,m.position())<=program.bow->targetRadius && area.definition.collision.missileSegment(p.position,m.position(),{4,1}):playerMeleeReach(area.definition.collision,p.position,m.position(),m.size(),weapon->rangeAdder)) targets.push_back(id.value);
        }
        const int count=targets.empty()?0:zeal?program.attacks:strafe?strafeShotCount(int(targets.size()),program.attacks,program.bow->minimumShots):std::min(int(targets.size()),program.attackLimit);
        if(!count) return {DomainStatus::Unavailable,{}};
        if(!target || std::find(targets.begin(),targets.end(),target.value)==targets.end()) target=EntityId{missileChainSuccessor(0,targets)};
        destination=participants.find(target).position();targetType=participants.find(target).player?0:1;
        const auto volley=weaponVolley(timing,count,program.rollbackPercent);
        hits=volley.hits;volleyDuration=int(volley.frames.size());
        if(hits.empty()) return {DomainStatus::Unavailable,{}};
    }
    auto prior=state_.casts.find(actor.actor);
    if(prior!=state_.casts.end() && skill.delayFrames>0 && prior->second.cooldownUntil>actor.tick) return {DomainStatus::Conflict,{}};
    std::optional<effects::PotionPlan> attackState;
    if(program.attackState.id>=0) {
        CombatEffectSpec effect;effect.state=program.attackState;effect.duration=uint64_t(std::max(1,program.attackDuration>0?program.attackDuration:volleyDuration));
        effect.source={CombatEffectSource::Skill,actor.actor,id,effectiveRank};effect.stacking=EffectStacking::ReplaceState;
        effect.modifiers.combat.defensePercent=program.attackDefense;
        effect.modifiers.combat.armorOverridePercent=program.armorOverridePercent;effect.modifiers.combat.physicalResist=program.attackPhysicalResist;
        auto prepared=ports_.effects.skillState(actor,std::move(effect));if(!prepared) return {prepared.status,{}};
        attackState=std::move(*prepared.value);
        if(program.whirlwind) {
            CombatEffectSpec moving;moving.state=program.movingState;moving.duration=uint64_t(std::max(1,volleyDuration));
            moving.source={CombatEffectSource::Skill,actor.actor,id,effectiveRank};
            auto &states=attackState->next.players[actor.actor].states;
            if(!states.apply(std::move(moving),actor.tick).accepted) return {DomainStatus::Conflict,{}};
            attackState->transient.states.insert(program.movingState.id);
        }
    }
    auto cost=ports_.inventory.weaponCost(actor,*weapon,skill,!program.manaOnRelease,strafe,0,attackState?std::optional{attackState->transient}:std::nullopt);if(!cost) return {cost.status,{}};
    const auto fact=AttackFact{actor.actor,target,0,targetType,actor.area,p.position,destination,actor.sequence,uint16_t(id),uint8_t(effectiveRank),program.whirlwind};
    if(auto *edit=std::get_if<transactions::CharacterEdit>(&cost.value->change)) edit->publicFacts.emplace_back(fact);
    else std::get<transactions::InventoryEdit>(cost.value->change).publicFacts.emplace_back(fact);
    Release release{actor,skill,{}, {actor.area,actor.areaGeneration,destination},target,actor.tick+uint64_t(hits.front())};
    release.unitType=targetType;release.weapon=*weapon;release.manaPaid=!program.manaOnRelease;release.weaponHits=std::move(hits);
    if(offhand) release.offhand=*offhand;
    release.weaponSpeed=timing.speed;release.weaponFrames=timing.frames;release.weaponRollback=program.rollbackPercent;
    if(program.whirlwind) {
        release.whirlwind=true;release.tick=actor.tick+1;release.nextWhirlHit=actor.tick+4;
        release.chargeSpeed=float(p.definition.runVelocity)*25.f/16.f*float(std::max(50,p.totals.character.velocityPercent))/100.f;
        const int speed=std::max(1,a.speed*std::max(1,100-weapon->itemBaseSpeed+weapon->itemAttackSpeed)/100);
        const int frames=a.frames*256/speed;
        release.whirlPeriod=frames<12?4:frames<15?6:frames<18?8:frames<20?10:frames<23?12:frames<26?14:16;
    }
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
    if(attackState) ports_.effects.commit(std::move(*attackState));
    releases_.erase(actor.actor);releases_.merge(preparedRelease);state_.casts.erase(actor.actor);state_.casts.merge(preparedCast);
    ports_.movement.execute(actor,{MovementAction::Stop,{},false});return {DomainStatus::Applied,std::monostate{}};
}
DomainStatus System::weaponRelease(Release &pending,const ActorContext &actor,Vec destination) {
    const auto &p=*ports_.players.find(actor.player);
    const auto &hand=pending.offhand && pending.nextWeaponHit%2?*pending.offhand:*pending.weapon;
    const auto found=std::find_if(p.totals.equipment.weapons.begin(),p.totals.equipment.weapons.begin()+p.totals.equipment.weaponCount,
        [&](const auto &w){return w.item==hand.item && w.weaponClass==hand.weaponClass && w.leftHand==hand.leftHand;});
    if(found==p.totals.equipment.weapons.begin()+p.totals.equipment.weaponCount) return DomainStatus::Stale;
    if(pending.skill.weapon->smite && (!pending.shield || pending.shield!=p.totals.equipment.shield)) return DomainStatus::Stale;
    if(pending.skill.weapon->commonAttack && commonAttackWeapon(p.totals.equipment,pending.skill.weapon->thrown,pending.skill.weapon->leftHand)!=&*found) return DomainStatus::Stale;
    const bool strafe=pending.skill.weapon->bow && pending.skill.weapon->bow->strafe;
    const bool fend=pending.skill.weapon->spear && pending.skill.weapon->spear->kind==SpearSkillSpec::Kind::Fend;
    const bool zeal=pending.skill.effect==SkillBehavior::Zeal;
    const bool secondSwing=pending.offhand && !pending.skill.weapon->thrown && pending.nextWeaponHit>0;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    EntityId selectedTarget=pending.unit;
    if(strafe || fend || zeal || secondSwing) {
        std::vector<uint64_t> targets;
        const auto &area=ports_.areas.at(actor.area);
        for(const auto id:participants.enemies(actor.actor,actor.area)) {
            const auto victim=participants.find(id);
            if(strafe?missileDistance(p.position,victim.position())<=pending.skill.weapon->bow->targetRadius && area.definition.collision.missileSegment(p.position,victim.position(),{4,1}):playerMeleeReach(area.definition.collision,p.position,victim.position(),victim.size(),found->rangeAdder)) targets.push_back(id.value);
        }
        auto successor=missileChainSuccessor(pending.unit.value,targets);
        if(!successor && targets.size()==1) successor=targets.front();
        const auto target=EntityId{!pending.nextWeaponHit && std::find(targets.begin(),targets.end(),pending.unit.value)!=targets.end()?pending.unit.value:successor};
        if(!target) return DomainStatus::Unavailable;
        selectedTarget=target;destination=participants.find(target).position();
    }
    const bool projectile=(found->ranged && !pending.skill.weapon->smite) || pending.skill.weapon->thrown;
    if(projectile && pending.skill.weapon->dualWield) {
        if(!found->projectile) return DomainStatus::Unavailable;
        pending.skill=commonProjectileSkill(pending.skill,*found->projectile);
    }
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
    const auto target=participants.find(selectedTarget);
    const auto &area=ports_.areas.at(actor.area);
    if(!target.aliveIn(actor.area) || !participants.canHarm(actor.actor,selectedTarget) ||
        !playerMeleeReach(area.definition.collision,p.position,target.position(),target.size(),found->rangeAdder)) return DomainStatus::Unavailable;
    WeaponSkillDamage snapshot;snapshot.weapon=*found;snapshot.level=p.persistent.player.level;
    missiles::Spawn request{actor,pending.skill,{},destination,true};request.weapon=snapshot;request.cost=std::move(cost);
    const auto result=ports_.missiles.direct(request,{selectedTarget}).status;
    if(result==DomainStatus::Applied) {pending.unit=selectedTarget;ports_.effects.commitItemEvents(std::move(*event.value));}
    return result;
}
}
