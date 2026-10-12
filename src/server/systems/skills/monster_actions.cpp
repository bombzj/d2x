#include "system.hpp"
#include "runtime.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "gameplay/monsters/projectile_math.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include <algorithm>
#include "core/random.hpp"
namespace d2x::server::skills {
DomainResult<> System::requestCast(const CastRequest &request) {
    auto &monsterReleases_=runtime_->monsters;
    const auto *monster = ports_.monsters.find(request.actor);
    const auto *target = std::get_if<UnitTarget>(&request.target);
    if (!monster || monster->life <= 0 || !target || (request.tick < monster->busyUntil || monsterReleases_.contains(request.actor)) || monster->frozenUntil > request.tick || monster->stunnedUntil>request.tick || monster->knockedUntil>request.tick || !monster->standardAttackSource()) return {DomainStatus::InvalidActor, {}};
    auto destination=ports_.monsters.targetPosition(target->id,monster->area);
    const auto *victim=ports_.monsters.find(target->id);
    const bool petAttack=monster->amazonAttacker();
    const bool chaotic=monster->curseControl.ai==CurseAi::Confuse || bool(monster->attractionTarget);
    const auto &area=ports_.areas.at(monster->area);
    if (!ports_.events.hasCapacity(2)) return {DomainStatus::Capacity,{}};
    const auto &rule = monster->rule;
    const MonsterAttackRule *prepared=nullptr;
    if(request.skill || monster->hireling) {const auto found=rule.skillActions.find(request.skill);if(found!=rule.skillActions.end()) prepared=&found->second;}
    else {const auto found=rule.attacks.find(request.monsterMode);if(found!=rule.attacks.end()) prepared=&found->second;}
    if((petAttack && request.skill) || !prepared) return {DomainStatus::Unavailable,{}};
    const auto slot=*prepared;
    const bool resurrection=slot.action==MonsterAttackRule::Action::Resurrect;
    const bool healing=slot.action==MonsterAttackRule::Action::Heal;
    const bool selfAction=slot.action==MonsterAttackRule::Action::BurrowUp || slot.action==MonsterAttackRule::Action::BurrowDown || slot.action==MonsterAttackRule::Action::Hatch || slot.action==MonsterAttackRule::Action::Animation || slot.action==MonsterAttackRule::Action::Lay;
    const bool special=slot.action!=MonsterAttackRule::Action::Damage;
    const combat::Participants participants{ports_.players,ports_.monsters};
    if(!resurrection && !healing && !selfAction && !participants.canHarm(monster->id,target->id)) return {DomainStatus::InvalidActor,{}};
    if(healing && !ports_.monsters.mummySupportTarget(monster->id,target->id,request.tick,false)) return {DomainStatus::InvalidActor,{}};
    if(selfAction && !destination) destination=std::pair{monster->position,0};
    if(request.position) {
        if(!std::isfinite(request.position->x) || !std::isfinite(request.position->y)) return {DomainStatus::InvalidRequest,{}};
        destination=std::pair{*request.position,0};
    }
    if(resurrection && ports_.monsters.resurrectionTarget(monster->id,target->id,request.tick)) destination=std::pair{victim->position,victim->rule.size};
    if(!destination || ((petAttack || monster->necroSummon || monster->hireling || monster->conversion)?target->type!=1 || !victim || victim->owner:!resurrection && !healing && !selfAction && target->type==1 && (!victim || (!victim->combatCompanion() && !chaotic))) || target->type>1 || (resurrection && !ports_.monsters.resurrectionTarget(monster->id,target->id,request.tick))) return {DomainStatus::InvalidActor,{}};
    if (area.definition.town || (!special && (slot.missile ? false :
        (request.monsterMode != 9 && meleeDistance(monster->position,monster->rule.size,destination->first,destination->second)>rule.meleeRange) ||
        !area.definition.collision.segment(monster->position,destination->first)))) return {DomainStatus::Unavailable,{}};
    if(slot.action==MonsterAttackRule::Action::Charge && meleeDistance(monster->position,rule.size,destination->first,destination->second)<=rule.meleeRange)
        return requestCast({request.actor,0,request.target,request.tick,5});
    const auto buffs=ports_.effects.unitModifiers(monster->id,request.tick);
    const int speed=slot.casting?100:monster->necroMaterial && monster->petWeapon?std::max(15,effectiveAttackSpeed(100,monster->petWeapon->fasterAttack,monster->petWeapon->baseSpeed,monster->petStats.attributes.combat.attackRate+buffs.combat.attackRate+(monster->chilledUntil>request.tick?rule.coldEffect:0))):std::max(15,100+buffs.combat.attackRate+(monster->chilledUntil>request.tick?rule.coldEffect:0));
    const auto scaled = [&](int frames) { return uint64_t((int64_t(frames) * 100 + speed - 1) / speed); };
    uint64_t duration=scaled(slot.duration),releaseTick=scaled(slot.release);
    int animationSpeed=0;
    if(slot.animation) {
        auto modifiers=buffs.combat;
        if(monster->combatCompanion()) mergeCombatModifiers(modifiers,monster->petStats.attributes.combat);
        const auto *weapon=monster->petWeapon?&*monster->petWeapon:nullptr;
        const int coldRate=monster->chilledUntil>request.tick?rule.coldEffect:0;
        if(slot.casting) animationSpeed=effectiveCastSpeed(slot.animation->speed,modifiers.fasterCast);
        else if(slot.useAttackRate) animationSpeed=effectiveAttackSpeed(slot.animation->speed,weapon?weapon->fasterAttack:0,weapon?weapon->baseSpeed:0,modifiers.attackRate+coldRate);
        else if(slot.animationSequence) animationSpeed=slot.animation->speed;
        else animationSpeed=effectiveOtherAnimationSpeed(slot.animation->speed,buffs.otherAnimationRate+(monster->combatCompanion()?monster->petStats.attributes.otherAnimationRate:0)+coldRate);
        duration=uint64_t(monsterAnimationTicks({slot.animation->frames,animationSpeed},slot.animationStartFrame));
        releaseTick=uint64_t(std::clamp(animationFrameTicks(std::max(0,slot.animationAction-slot.animationStartFrame),animationSpeed),1,int(duration)));
    }
    DamageType type = DamageType::Physical;
    combat::Damage damage{monster->id, target->id, type, int64_t(slot.minimum) * 256,
        int64_t(slot.maximum) * 256, request.tick + 1, monster->area, request.tick + releaseTick, slot.rating, rule.level, rule.meleeRange, rule.size, (petAttack || monster->necroSummon || monster->hireling)?monster->petWeapon:std::optional<WeaponDamage>{}, 0, 0};
    damage.skill=slot.weaponSkill;
    damage.monsterSkill=slot;damage.rating=int(int64_t(slot.rating)*std::max(0,100+slot.ratingPercent)/100);
    if((petAttack || monster->necroSummon || monster->hireling) && damage.weapon) {
        const auto buffs=ports_.effects.unitModifiers(monster->id,request.tick).combat;
        damage.attackModifiers=monster->petStats.attributes.combat;mergeCombatModifiers(*damage.attackModifiers,buffs);
        damage.weapon->attackRatingPercent+=buffs.attackRatingPercent;damage.weapon->damagePercent+=buffs.damagePercent;
        damage.weapon->projectileDamagePercent+=buffs.damagePercent;
    }
    damage.monsterMode = request.monsterMode;
    damage.sourceInterruption=monster->interruption;
    auto reserved=monster->combatRandom;
    const auto actionRandom=childRandom(reserved);
    if(!petAttack) damage.actionRandom=actionRandom;
    if (slot.missile || special || !slot.releaseFrames.empty()) {
        if (monsterReleases_.size() >= 4096) return {DomainStatus::Capacity,{}};
        MonsterRelease release{request,slot,monster->area,area.generation,request.tick+releaseTick,actionRandom,monster->interruption,{}};
        release.started=request.tick;
        if(animationSpeed && !slot.animationEvents.empty()) {
            for(int frame:slot.animationEvents) release.releaseFrames.push_back(weaponSequenceTick(frame,animationSpeed));
        } else for(int frame:slot.releaseFrames) release.releaseFrames.push_back(int(scaled(frame)));
        if(slot.action==MonsterAttackRule::Action::Spray || request.position || slot.action==MonsterAttackRule::Action::Leap) release.launch=std::pair{monster->position,destination->first};
        monsterReleases_.emplace(request.actor,std::move(release));
    } else if (request.monsterMode != 9) {
        auto queued=ports_.combat.enqueue(damage);
        if (!queued) return queued;
    }
    if(slot.action==MonsterAttackRule::Action::BurrowUp || slot.action==MonsterAttackRule::Action::BurrowDown) {
        const auto changed=ports_.monsters.burrow(monster->id,slot.action==MonsterAttackRule::Action::BurrowDown,0,request.tick);
        if(!changed) {monsterReleases_.erase(monster->id);return changed;}
    }
    auto event = ports_.events.publish({0, request.tick, {}, {AudienceKind::Area, {}, monster->area},
        {AttackFact{monster->id, (request.position || slot.action==MonsterAttackRule::Action::Web)?EntityId{}:target->id, 1, target->type, monster->area, monster->position, slot.action==MonsterAttackRule::Action::Web?monster->position:destination->first, request.tick + 1, request.skill, slot.rank, false, request.monsterMode}}});
    if (!event) { ports_.combat.cancel(monster->id); monsterReleases_.erase(monster->id); return {event.status, {}}; }
    if(!petAttack) ports_.monsters.commitCombatRandom(monster->id,reserved);
    const bool neutral=request.monsterMode<16 && !(rule.immediateAiModes & (uint16_t(1)<<request.monsterMode));
    const auto begun=ports_.monsters.beginAttack(monster->id, request.tick + duration,neutral);
    if(begun && slot.action==MonsterAttackRule::Action::Charge) {
        const auto delta=destination->first-monster->position;
        const float stop=float(rule.meleeRange+3)+.5f;
        const Vec goal=monster->position+delta.unit()*std::max(0.f,delta.length()-stop);
        const auto motion=ports_.monsters.startSkillMotion(monster->id,goal,request.tick+duration,slot.movementPercent,false,request.tick,target->id);
        if(!motion) {monsterReleases_.erase(monster->id);return motion;}
    }
    return begun;
}
StepStatus System::releaseMonsters(TickContext tick) {
    auto &monsterReleases_=runtime_->monsters;
    bool blocked=false;
    for (auto it=monsterReleases_.begin();it!=monsterReleases_.end();) {
        auto &pending=it->second;
        if (pending.due>tick.tick) {++it;continue;}
        const auto *source=ports_.monsters.find(it->first);const auto *area=ports_.areas.find(pending.area);
        if (!source || (source->owner && !source->necroSummon && !source->hireling && !source->conversion) || source->life<=0 || source->area!=pending.area || source->interruption!=pending.interruption || source->frozenUntil>tick.tick || source->stunnedUntil>tick.tick || source->knockedUntil>tick.tick ||
            !area || area->generation!=pending.generation || area->definition.town) {it=monsterReleases_.erase(it);continue;}
        auto &unit=std::get<UnitTarget>(pending.request.target);
        if(source->skillMotion) {pending.due=tick.tick+1;++it;continue;}
        if(pending.attack.action==MonsterAttackRule::Action::Charge) {
            const combat::Participants participants{ports_.players,ports_.monsters};
            const auto target=ports_.monsters.targetPosition(unit.id,pending.area);
            if(!target || !participants.canHarm(source->id,unit.id) || meleeDistance(source->position,source->rule.size,target->first,target->second)>source->rule.meleeRange+3) {
                std::optional<UnitTarget> nearby;float distance=3.f;
                const auto consider=[&](UnitTarget victim,Vec point,int size) {
                    const float candidate=meleeDistance(source->position,source->rule.size,point,size);
                    if(victim.id!=unit.id && candidate<distance && participants.canHarm(source->id,victim.id)) {distance=candidate;nearby=victim;}
                };
                for(const auto &[key,p]:ports_.players.all()) {(void)key;if(p.entered && p.area==pending.area && p.persistent.player.hp>0) consider({p.actor,0,0},p.position,2);}
                for(const auto &[key,m]:ports_.monsters.read().actors) if(m.area==pending.area && m.life>0) consider({key,0,1},m.position,m.rule.size);
                if(!nearby) {it=monsterReleases_.erase(it);continue;}
                unit=*nearby;
            }
        }
        if(pending.attack.action==MonsterAttackRule::Action::Leap && pending.hitReleased) {
            if(!pending.launch) {it=monsterReleases_.erase(it);continue;}
            const auto motion=ports_.monsters.startSkillMotion(source->id,pending.launch->second*2.f-pending.launch->first,tick.tick+uint64_t(pending.attack.duration),pending.attack.movementPercent,true,tick.tick);
            if(motion.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
            it=monsterReleases_.erase(it);continue;
        }
        DomainResult<> specialResult;
        bool special=true;
        switch(pending.attack.action) {
        case MonsterAttackRule::Action::Animation: specialResult={DomainStatus::Applied,std::monostate{}};break;
        case MonsterAttackRule::Action::Skill: {
            if(!source->hireling || !source->owner || !pending.attack.spell) {specialResult={DomainStatus::InvalidActor,{}};break;}
            const auto *owner=ports_.players.find(*source->owner);
            if(!owner || !owner->entered || owner->area!=source->area || owner->persistent.player.hp<=0) {specialResult={DomainStatus::InvalidActor,{}};break;}
            const ActorContext actor{owner->player,owner->actor,source->area,pending.generation,0,tick.tick};
            const auto &cast=*pending.attack.spell;
            if(cast.amazonMagic) specialResult=ports_.effects.amazonMagic(actor,cast,source->id);
            else if(cast.appliedEffect) specialResult=ports_.effects.hirelingSkill(source->id,cast,tick.tick);
            else {
                const auto target=ports_.monsters.targetPosition(unit.id,source->area);
                if(!target || !combat::Participants{ports_.players,ports_.monsters}.canHarm(source->id,unit.id)) {specialResult={DomainStatus::InvalidActor,{}};break;}
                missiles::Spawn spawn{actor,cast,{},target->first,true,source->id,1,source->position};spawn.random=pending.random;
                const auto result=ports_.missiles.spawn(spawn);specialResult={result.status,result?std::optional{std::monostate{}}:std::nullopt};
            }
            break;
        }
        case MonsterAttackRule::Action::Resurrect: specialResult=ports_.monsters.resurrect(source->id,unit.id,tick.tick);break;
        case MonsterAttackRule::Action::Heal: {
            if(!ports_.monsters.mummySupportTarget(source->id,unit.id,tick.tick,false)) {specialResult={DomainStatus::InvalidActor,{}};break;}
            const auto *victim=ports_.monsters.find(unit.id);auto random=pending.random;
            const int maximum=std::clamp(pending.attack.healMaximum,0,100),minimum=std::clamp(pending.attack.healMinimum,0,maximum);
            const int percent=minimum+int(limitedRandom(random,unsigned(maximum-minimum)));
            specialResult=ports_.monsters.heal(unit.id,victim->maximumLife*percent/100);if(specialResult) pending.random=random;break;
        }
        case MonsterAttackRule::Action::BurrowUp: specialResult={DomainStatus::Applied,std::monostate{}};break;
        case MonsterAttackRule::Action::BurrowDown: specialResult=ports_.monsters.heal(source->id,source->life*pending.attack.healMinimum/100);break;
        case MonsterAttackRule::Action::Lay: {
            const auto target=ports_.monsters.targetPosition(unit.id,source->area);
            specialResult=target?ports_.monsters.spawnSkillChildren(source->id,target->first,1,false,tick.tick):DomainResult<>{DomainStatus::InvalidActor,{}};break;
        }
        case MonsterAttackRule::Action::Hatch: specialResult=ports_.monsters.spawnSkillChildren(source->id,source->position,pending.attack.hatchCount,true,tick.tick);break;
        case MonsterAttackRule::Action::SwarmMove: {
            const auto target=ports_.monsters.targetPosition(unit.id,source->area);
            specialResult=target?ports_.monsters.startSwarmMove(source->id,target->first,tick.tick):DomainResult<>{DomainStatus::InvalidActor,{}};break;
        }
        case MonsterAttackRule::Action::Curse: {
            if(!pending.attack.curse) {specialResult={DomainStatus::Unavailable,{}};break;}
            const auto &curse=*pending.attack.curse;
            if(!pending.targetsCaptured) {
                const auto target=ports_.monsters.targetPosition(unit.id,source->area);if(!target) {specialResult={DomainStatus::InvalidActor,{}};break;}
                const auto eligible=[&](EntityId id,Vec point) {return combat::Participants{ports_.players,ports_.monsters}.canHarm(source->id,id) && (point-target->first).length()<=curse.radius && area->definition.activation.nearby(target->first,point);};
                for(const auto &[key,p]:ports_.players.all()) {(void)key;if(p.entered && p.area==source->area && p.persistent.player.hp>0 && eligible(p.actor,p.position)) pending.effectTargets.push_back(p.actor);}
                for(const auto &[key,m]:ports_.monsters.read().actors) if(m.area==source->area && m.life>0 && eligible(key,m.position)) pending.effectTargets.push_back(key);
                pending.targetsCaptured=true;
            }
            specialResult={DomainStatus::Applied,std::monostate{}};
            while(pending.effectTarget<pending.effectTargets.size()) {
                specialResult=ports_.effects.castMonsterCurse(source->id,curse,pending.effectTargets[pending.effectTarget],tick.tick);
                if(specialResult.status==DomainStatus::Capacity) break;
                ++pending.effectTarget;
            }
            break;
        }
        case MonsterAttackRule::Action::Nest: {const auto spawned=pending.request.position?ports_.monsters.spawnNestChild(source->id,tick.tick,*pending.request.position):ports_.monsters.spawnNestChild(source->id,tick.tick);specialResult={spawned.status,spawned?std::optional{std::monostate{}}:std::nullopt};break;}
        case MonsterAttackRule::Action::Web: specialResult=ports_.monsters.activateWeb(source->id,tick.tick);break;
        case MonsterAttackRule::Action::Teleport: specialResult=pending.request.position?ports_.monsters.teleport(source->id,*pending.request.position,tick.tick,ports_.effects.unitStates(source->id,tick.tick).contains(source->rule.preventHealState)?0:pending.request.teleportHeal):DomainResult<>{DomainStatus::InvalidRequest,{}};break;
        case MonsterAttackRule::Action::Spray:
        case MonsterAttackRule::Action::Trap:
        case MonsterAttackRule::Action::Firewall:
        case MonsterAttackRule::Action::Charge:
        case MonsterAttackRule::Action::Leap:
        case MonsterAttackRule::Action::Nova:
        case MonsterAttackRule::Action::Damage: special=false;break;
        }
        if(special) {
            if(specialResult.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
            if(specialResult && ++pending.nextRelease<pending.releaseFrames.size()) {childRandom(pending.random);pending.due=pending.started+uint64_t(pending.releaseFrames[pending.nextRelease]);++it;continue;}
            it=monsterReleases_.erase(it);continue;
        }
        const auto target=ports_.monsters.targetPosition(unit.id,pending.area);
        if(!pending.launch && !combat::Participants{ports_.players,ports_.monsters}.canHarm(source->id,unit.id)) {it=monsterReleases_.erase(it);continue;}
        if (!target && !pending.launch) {it=monsterReleases_.erase(it);continue;}
        if(!pending.attack.missile) {
            combat::Damage damage{source->id,unit.id,DamageType::Physical,int64_t(pending.attack.minimum)*256,int64_t(pending.attack.maximum)*256,
                pending.started+1,source->area,tick.tick,pending.attack.rating,source->rule.level,source->rule.meleeRange,source->rule.size,source->petWeapon};
            damage.skill=pending.attack.weaponSkill;damage.sourceInterruption=pending.interruption;damage.actionRandom=pending.random;
            damage.monsterSkill=pending.attack;damage.rating=int(int64_t(pending.attack.rating)*std::max(0,100+pending.attack.ratingPercent)/100);
            if(pending.attack.action==MonsterAttackRule::Action::Charge) damage.range+=3;
            const auto buffs=ports_.effects.unitModifiers(source->id,tick.tick).combat;
            damage.attackModifiers=source->petStats.attributes.combat;mergeCombatModifiers(*damage.attackModifiers,buffs);
            if(damage.weapon) {damage.weapon->attackRatingPercent+=buffs.attackRatingPercent;damage.weapon->damagePercent+=buffs.damagePercent;}
            const auto result=ports_.combat.enqueue(damage);
            if(result.status==DomainStatus::Capacity || result.status==DomainStatus::Conflict) {blocked=true;++it;continue;}
            if(result && pending.attack.action==MonsterAttackRule::Action::Leap) {
                pending.hitReleased=true;pending.due=std::max(tick.tick+1,pending.started+uint64_t(pending.attack.movementStart));++it;continue;
            }
            if(result && ++pending.nextRelease<pending.releaseFrames.size()) {childRandom(pending.random);pending.due=pending.started+uint64_t(pending.releaseFrames[pending.nextRelease]);++it;continue;}
            it=monsterReleases_.erase(it);continue;
        }
        if(!pending.launch) pending.launch=std::pair{source->position,target->first};
        const int quills=source->rule.ai.kind==MonsterAiKind::QuillRat && pending.request.monsterMode==5?source->rule.ai.params[2]:0;
        auto launch=*pending.launch;
        if(pending.attack.action==MonsterAttackRule::Action::Spray) launch=andarielSprayRay(source->position,pending.launch->second,4+int(pending.nextRelease));
        if(pending.attack.action==MonsterAttackRule::Action::Trap) launch=gargoyleTrapRay(source->position,pending.launch->second,GargoyleRaySide::Server);
        if(pending.attack.action==MonsterAttackRule::Action::Firewall) launch={pending.launch->second,source->position};
        missiles::MonsterSpawn spawn{source->id,source->area,pending.generation,tick.tick,launch.first,launch.second,pending.attack,source->rule.hitStates,source->rule.level,source->rule.criticalChance,quills,pending.random};
        if(pending.attack.action==MonsterAttackRule::Action::Nova) for(const Vec heading:missileRingBurst(1)) spawn.aims.push_back(source->position+heading);
        if(pending.attack.missile->behavior==MonsterMissileRule::Behavior::Meteor) spawn.position=launch.second;
        const auto result=ports_.missiles.spawnMonster(spawn);
        if (result.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
        if(result && pending.attack.action==MonsterAttackRule::Action::Spray && ++pending.nextRelease<pending.releaseFrames.size()) {
            pending.random=result.value->random;pending.due=pending.started+uint64_t(pending.releaseFrames[pending.nextRelease]);++it;continue;
        }
        it=monsterReleases_.erase(it);
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
