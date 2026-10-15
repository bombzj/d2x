#include "system.hpp"
#include "runtime.hpp"
#include "assassin_kick.hpp"
#include "druid_melee.hpp"
#include "gameplay/skills/assassin_summon.hpp"
#include "gameplay/skills/druid_summon.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/companions/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "gameplay/actions/monster_timing.hpp"
#include "gameplay/monsters/projectile_math.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "evaluation.hpp"
#include "server/systems/attributes/monster_sources.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/skills/passive_stats.hpp"
#include <algorithm>
#include <stdexcept>
#include "core/random.hpp"
namespace d2x::server::skills {
DomainResult<> System::requestCast(const CastRequest &request) {
    auto &monsterReleases_=runtime_->monsters;
    const auto *monster = ports_.monsters.find(request.actor);
    const auto *target = std::get_if<UnitTarget>(&request.target);
    if (!monster || !monster->alive() || !target || (request.tick < monster->busyUntil || monsterReleases_.contains(request.actor)) || ports_.monsters.frozen(monster->id,request.tick) || monster->stunnedUntil>request.tick || monster->knockedUntil>request.tick || !monster->standardAttackSource()) return {DomainStatus::InvalidActor, {}};
    auto destination=ports_.monsters.targetPosition(target->id,monster->area);
    const auto *victim=ports_.monsters.find(target->id);
    const bool petAttack=monster->amazonAttacker();
    const auto &area=ports_.areas.at(monster->area);
    if (!ports_.events.hasCapacity(monster->assassinPet?3:2)) return {DomainStatus::Capacity,{}};
    const auto &rule = monster->rule;
    const MonsterAttackRule *prepared=nullptr;
    if(request.skill || monster->hireling) {const auto found=rule.skillActions.find(request.skill);if(found!=rule.skillActions.end()) prepared=&found->second;}
    else {const auto found=rule.attacks.find(request.monsterMode);if(found!=rule.attacks.end()) prepared=&found->second;}
    if((petAttack && request.skill) || !prepared) return {DomainStatus::Unavailable,{}};
    auto slot=*prepared;
    if(monster->assassinPet || (monster->druidPet && request.skill)) {
        const auto sampled=ports_.effects.unitStatValues(monster->id,request.tick);if(!sampled) return {DomainStatus::Unavailable,{}};
        const auto ranks=nativeMonsterSkillRanks(rule.baseSkillRanks,*sampled,*rule.stats,*rule.formulas);
        const auto rank=ranks.find(request.skill);if(rank==ranks.end() || rank->second<=0) return {DomainStatus::Unavailable,{}};
        slot.rank=uint8_t(rank->second);
    }
    if(slot.missile && (slot.missile->levelDamage || slot.missile->skillDamage)) {
        if(!rule.stats || !rule.formulas) return {DomainStatus::Unavailable,{}};
        const auto stats=ports_.effects.unitStatValues(monster->id,request.tick);if(!stats) return {DomainStatus::Unavailable,{}};
        const auto ranks=nativeMonsterSkillRanks(rule.baseSkillRanks,*stats,*rule.stats,*rule.formulas);
        const auto id=slot.missile->skillDamage?slot.missile->skillDamage->rankSkill:slot.missile->levelDamage->skill;
        const auto rank=ranks.find(id);if(rank==ranks.end() || rank->second<=0) return {DomainStatus::Unavailable,{}};
        slot.rank=uint8_t(rank->second);
    }
    const bool resurrection=slot.action==MonsterAttackRule::Action::Resurrect;
    const bool healing=slot.action==MonsterAttackRule::Action::Heal;
    const bool shadowSelf=monster->assassinPet && monster->assassinPet->definition->warp &&
        !target->id && rule.formulas->skills.at(request.skill).number("aitype")==1;
    const bool druidTeleport=monster->druidPet && request.skill && rule.formulas->skills.at(request.skill).number("srvdofunc")==98;
    const bool selfAction=shadowSelf || druidTeleport || slot.action==MonsterAttackRule::Action::BurrowUp || slot.action==MonsterAttackRule::Action::BurrowDown || slot.action==MonsterAttackRule::Action::Hatch || slot.action==MonsterAttackRule::Action::Animation || slot.action==MonsterAttackRule::Action::Lay;
    const bool special=slot.action!=MonsterAttackRule::Action::Damage;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const bool corpseSkill=(monster->assassinPet && rule.formulas->skills.at(request.skill).number("srvdofunc")==55) ||
        (monster->druidPet && request.skill && (rule.formulas->skills.at(request.skill).number("srvstfunc")==63 || rule.formulas->skills.at(request.skill).number("srvdofunc")==137));
    if(!resurrection && !healing && !selfAction && !corpseSkill && !participants.canHarm(monster->id,target->id)) return {DomainStatus::InvalidActor,{}};
    if(corpseSkill && !(monster->druidPet?ports_.monsters.nativeUsableCorpse(target->id,request.tick):ports_.monsters.consumableCorpse(target->id,monster->area,request.tick))) return {DomainStatus::Unavailable,{}};
    if(corpseSkill && victim) destination=std::pair{victim->position,victim->rule.size};
    if(healing && !ports_.monsters.mummySupportTarget(monster->id,target->id,request.tick,false)) return {DomainStatus::InvalidActor,{}};
    if(selfAction && !destination) destination=std::pair{monster->position,0};
    if(request.position) {
        if(!std::isfinite(request.position->x) || !std::isfinite(request.position->y)) return {DomainStatus::InvalidRequest,{}};
        destination=std::pair{*request.position,0};
    }
    if(resurrection && ports_.monsters.resurrectionTarget(monster->id,target->id,request.tick)) destination=std::pair{victim->position,victim->rule.size};
    if(!destination || target->type>1 || (target->type==1 && !victim && !selfAction) || (resurrection && !ports_.monsters.resurrectionTarget(monster->id,target->id,request.tick))) return {DomainStatus::InvalidActor,{}};
    if ((area.definition.town && !(((monster->assassinPet && monster->assassinPet->definition->warp) || monster->druidPet) && rule.formulas->skills.at(request.skill).number("intown"))) || (!special && (slot.missile ? false :
        (request.monsterMode != 9 && meleeDistance(monster->position,monster->rule.size,destination->first,destination->second)>rule.meleeRange) ||
        !area.definition.collision.segment(monster->position,destination->first)))) return {DomainStatus::Unavailable,{}};
    if(slot.action==MonsterAttackRule::Action::Charge && meleeDistance(monster->position,rule.size,destination->first,destination->second)<=rule.meleeRange)
        return requestCast({request.actor,0,request.target,request.tick,5});
    const auto buffs=ports_.effects.unitModifiers(monster->id,request.tick);
    const int speed=slot.casting?100:monster->necroMaterial && monster->petWeapon?std::max(15,effectiveAttackSpeed(100,monster->petWeapon->fasterAttack,monster->petWeapon->baseSpeed,monster->petStats.attributes.combat.attackRate+buffs.combat.attackRate)):std::max(15,100+buffs.combat.attackRate);
    MonsterActionTimingInput timingInput;
    timingInput.duration=slot.duration;timingInput.release=slot.release;timingInput.fallbackRate=speed;
    timingInput.animation=slot.animation;timingInput.actionFrame=slot.animationAction;timingInput.startFrame=slot.animationStartFrame;
    timingInput.casting=slot.casting;timingInput.attackRate=slot.useAttackRate;timingInput.sequence=slot.animationSequence;
    timingInput.animationEvents=slot.animationEvents;timingInput.releaseFrames=slot.releaseFrames;
    if(slot.animation) {
        auto modifiers=buffs.combat;
        if(monster->combatCompanion()) mergeCombatModifiers(modifiers,monster->petStats.attributes.combat);
        const auto *weapon=monster->petWeapon?&*monster->petWeapon:nullptr;
        timingInput.fasterCast=modifiers.fasterCast;timingInput.itemIAS=weapon?weapon->fasterAttack:0;
        timingInput.weaponSpeed=weapon?weapon->baseSpeed:0;timingInput.skillRate=modifiers.attackRate;
        if(monster->assassinPet && monster->assassinPet->definition->warp && rule.formulas->skills.at(request.skill).number("cltstfunc")==9)
            timingInput.skillRate=nativeAdd(timingInput.skillRate,rule.formulas->skills.at(request.skill).number("param4"));
        timingInput.otherRate=buffs.otherAnimationRate+(monster->combatCompanion()?monster->petStats.attributes.otherAnimationRate:0);
    }
    const auto timing=planMonsterActionTiming(timingInput);
    const auto duration=timing.duration,releaseTick=druidTeleport?(monster->druidPet->definition->ai==DruidPetDefinition::Ai::DireWolf?2:4):timing.release;
    auto reserved=monster->combatRandom;
    std::optional<NativeUnitStats> modeState;
    if(monster->nativeStats) {
        const auto &table=rule.modeTables.at(size_t(rule.difficulty));
        if(!table || !rule.stats) throw std::runtime_error("Missing native monster mode calculation rules");
        MonsterModePhysical ownerSkill;
        if(rule.ownerDamageSkill>0) {
            const auto sampleOwner=[&](const SkillFormulaCatalog &catalog,SkillFormulaInput context) {
                SkillFormulaValues values(catalog,context);
                const auto id=rule.ownerDamageSkill;
                const auto level=context.referenceRanks->effective.find(id);
                const int rank=level==context.referenceRanks->effective.end()?0:level->second;
                const auto physical=values.physicalRange(id,rank,true);
                const auto rating=values.parameter(FormulaDomain::Skill,id,rank,"toht");
                if(!physical || !rating) throw std::runtime_error("Missing native owner skill damage context");
                ownerSkill={physical->first>>8,physical->second>>8,*rating};
            };
            const auto ownerId=ports_.monsters.minionOwner(monster->id);
            if(monster->owner) {
                if(const auto *owner=ports_.players.find(*monster->owner);owner && owner->actor==ownerId)
                    sampleOwner(*owner->rules.skills->formulas,formulaInput(*owner));
            } else if(const auto *owner=ports_.monsters.find(ownerId)) {
                const auto weapon=owner->petWeapon?std::optional<EntityId>{owner->petWeapon->item}:std::nullopt;
                auto stats=ports_.effects.unitStatValues(ownerId,request.tick,weapon);
                if(!stats || !owner->rule.formulas) throw std::runtime_error("Missing native monster-owner formula totals");
                sampleOwner(*owner->rule.formulas,formulaInput(*owner,std::move(*stats)));
            }
        }
        slot.modeStats=prepareMonsterModeStats(*table,*rule.stats,
            {request.monsterMode,rule.level,rule.difficulty,rule.statGameType,monster->admittedPlayerCount,
             monster->alignment()==UnitAlignment::Evil,ownerSkill,monster->id,request.tick+1},reserved);
        const auto value=[&](std::string_view name) {
            const auto id=rule.stats->names.at(std::string(name));
            const auto stat=slot.modeStats->base.find({id,0});return stat==slot.modeStats->base.end()?0:stat->second;
        };
        slot.minimum=value("mindamage");slot.maximum=value("maxdamage");slot.rating=value("tohit");
        modeState=attributes::withUnitModeStats(*monster->nativeStats,*rule.stats,slot.modeStats);
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
    std::shared_ptr<Release> shadow;
    if(monster->assassinPet && monster->assassinPet->definition->warp) {
        const auto started=startShadowSkill(request,slot);if(!started) return {started.status,{}};
        shadow=*started.value;reserved=monster->combatRandom;
        shadow->weaponHits=timing.events;shadow->weaponFrames=timing.duration;
    }
    const auto actionRandom=(monster->assassinPet || monster->druidPet)?reserved:childRandom(reserved);
    if(!petAttack) damage.actionRandom=actionRandom;
    if (slot.missile || special || !slot.releaseFrames.empty()) {
        if (monsterReleases_.size() >= 4096) return {DomainStatus::Capacity,{}};
        MonsterRelease release{request,slot,monster->area,area.generation,request.tick+releaseTick,actionRandom,monster->interruption,{}};
        release.started=request.tick;
        release.nativeSkill=std::move(shadow);
        if(monster->assassinPet && rule.formulas->skills.at(request.skill).number("srvstfunc")==53) {
            auto sampled=ports_.effects.unitStatValues(monster->id,request.tick);
            auto input=formulaInput(*monster,*sampled);SkillFormulaValues values(*rule.formulas,input);
            const auto duration=values.calculation(FormulaDomain::Skill,request.skill,slot.rank,"calc2");
            if(!duration) throw std::logic_error("Missing native sentry inferno duration");
            release.infernoUntil=request.tick+uint64_t(std::max(1,*duration));
        }
        release.releaseFrames=timing.events;
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
    if(monster->assassinPet && monster->assassinPet->definition->warp) runtime_->assassinStarts.erase(monster->id);
    if(monster->assassinPet && rule.formulas->skills.at(request.skill).number("srvstfunc")==53)
        ports_.monsters.toggleAiState(monster->id,rule.formulas->infernoState,true,request.tick);
    const bool neutral=request.monsterMode<16 && !(rule.immediateAiModes & (uint16_t(1)<<request.monsterMode));
    const auto begun=ports_.monsters.beginAttack(monster->id, request.tick + duration,neutral,
        request.position || slot.action==MonsterAttackRule::Action::Web ? EntityId{} : target->id,std::move(modeState));
    if(begun && slot.action==MonsterAttackRule::Action::Charge) {
        const auto delta=destination->first-monster->position;
        const float stop=float(rule.meleeRange+3)+.5f;
        const Vec goal=monster->position+delta.unit()*std::max(0.f,delta.length()-stop);
        const auto motion=ports_.monsters.startSkillMotion(monster->id,goal,request.tick+duration,slot.movementPercent,false,request.tick,target->id);
        if(!motion) {monsterReleases_.erase(monster->id);return motion;}
    }
    if(begun && slot.action==MonsterAttackRule::Action::Druid) {
        const auto found=monsterReleases_.find(monster->id);
        if(found!=monsterReleases_.end()) {
            const auto started=startDruidPet(found->second,{request.tick});
            if(!started && started.status!=DomainStatus::Capacity) {monsterReleases_.erase(found);return started;}
        }
    }
    return begun;
}
StepStatus System::releaseMonsters(TickContext tick) {
    auto &monsterReleases_=runtime_->monsters;
    bool blocked=false;
    for (auto it=monsterReleases_.begin();it!=monsterReleases_.end();) {
        auto &pending=it->second;
        if(pending.attack.action==MonsterAttackRule::Action::Druid && (!pending.nativeSkill || !pending.nativeSkill->nativeStartPrepared)) {
            const auto *source=ports_.monsters.find(pending.request.actor);
            if(!source || !source->alive() || pending.cancelled || source->interruption!=pending.interruption || source->area!=pending.area) {
                if(pending.nativeSkill && pending.nativeSkill->druidMelee && pending.nativeSkill->druidMelee->occurrence)
                    ports_.combat.discardNativeMelee(pending.request.actor,*pending.nativeSkill->druidMelee->occurrence);
                it=monsterReleases_.erase(it);continue;
            }
            const auto started=startDruidPet(pending,tick);
            if(started.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
            if(!started) {it=monsterReleases_.erase(it);continue;}
        }
        if (pending.due>tick.tick) {++it;continue;}
        const auto *source=ports_.monsters.find(it->first);const auto *area=ports_.areas.find(pending.area);
        const bool continuing=pending.nativeCallbackStarted;
        if (!source || (source->owner && !source->assassinPet && !source->druidPet && !source->necroSummon && !source->hireling && !source->conversion && !source->hydra()) || (!continuing && (!source->alive() || pending.cancelled || source->interruption!=pending.interruption || ports_.monsters.frozen(source->id,tick.tick) || source->stunnedUntil>tick.tick || source->knockedUntil>tick.tick)) || source->area!=pending.area ||
            !area || area->generation!=pending.generation || (area->definition.town &&
                !(((source->assassinPet && source->assassinPet->definition->warp) || source->druidPet) && source->rule.formulas->skills.at(pending.request.skill).number("intown")))) {
            if(pending.nativeSkill && pending.nativeSkill->assassinKick && pending.nativeSkill->assassinKick->occurrence)
                ports_.combat.discardNativeMelee(pending.request.actor,*pending.nativeSkill->assassinKick->occurrence);
            if(pending.nativeSkill && pending.nativeSkill->druidMelee && pending.nativeSkill->druidMelee->occurrence)
                ports_.combat.discardNativeMelee(pending.request.actor,*pending.nativeSkill->druidMelee->occurrence);
            ports_.companions.cancel(pending.request.actor);it=monsterReleases_.erase(it);continue;
        }
        auto &unit=std::get<UnitTarget>(pending.request.target);
        if(pending.attack.action==MonsterAttackRule::Action::Druid) {
            DomainResult<bool> result;
            {auto execution=state_.executingRelease(source->id);result=releaseDruidPet(pending,tick);}
            if(result.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
            if(result && !*result.value && !pending.cancelled) {++it;continue;}
            if(pending.nativeSkill && pending.nativeSkill->druidMelee && pending.nativeSkill->druidMelee->occurrence)
                ports_.combat.discardNativeMelee(pending.request.actor,*pending.nativeSkill->druidMelee->occurrence);
            ports_.companions.cancel(pending.request.actor);it=monsterReleases_.erase(it);continue;
        }
        if(pending.attack.action==MonsterAttackRule::Action::Assassin) {
            DomainResult<bool> result;
            {auto execution=state_.executingRelease(source->id);result=releaseAssassinPet(pending,tick);}
            if(result.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
            if(result && !*result.value && !pending.cancelled) {++it;continue;}
            if(pending.nativeSkill && pending.nativeSkill->assassinKick && pending.nativeSkill->assassinKick->occurrence)
                ports_.combat.discardNativeMelee(pending.request.actor,*pending.nativeSkill->assassinKick->occurrence);
            if(pending.nativeSkill && pending.nativeSkill->druidMelee && pending.nativeSkill->druidMelee->occurrence)
                ports_.combat.discardNativeMelee(pending.request.actor,*pending.nativeSkill->druidMelee->occurrence);
            ports_.companions.cancel(pending.request.actor);
            it=monsterReleases_.erase(it);continue;
        }
        if(source->skillMotion) {pending.due=tick.tick+1;++it;continue;}
        if(pending.attack.action==MonsterAttackRule::Action::Charge) {
            const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
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
            else if(cast.appliedEffect) specialResult=ports_.effects.unitSkill(source->id,cast,tick.tick);
            else {
                const auto target=ports_.monsters.targetPosition(unit.id,source->area);
                if(!target || !combat::Participants{ports_.players,ports_.monsters,ports_.social}.canHarm(source->id,unit.id)) {specialResult={DomainStatus::InvalidActor,{}};break;}
                missiles::Spawn spawn{actor,cast,{},target->first,true,source->id,1,source->position};
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
                pending.effectTargets=ports_.effects.nativeAuraCandidateRecipients({source->id,source->area,int32_t(curse.radius),3,false,
                    target->first+area->definition.origin,effects::NativeAuraTargetMode::TargetRange});
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
        if(!pending.launch && !combat::Participants{ports_.players,ports_.monsters,ports_.social}.canHarm(source->id,unit.id)) {it=monsterReleases_.erase(it);continue;}
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
        spawn.nativeSkill=pending.request.skill;
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
