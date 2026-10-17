#include "system.hpp"
#include "runtime.hpp"
#include "assassin_kick.hpp"
#include "druid_melee.hpp"
#include "weapon_melee.hpp"
#include "native_caster.hpp"
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
    if (!monster || !monster->alive() || !target || (request.tick < monster->busyUntil || busy(request.actor,request.tick)) || ports_.monsters.frozen(monster->id,request.tick) || monster->stunnedUntil>request.tick || monster->knockedUntil>request.tick || !monster->standardAttackSource()) return {DomainStatus::InvalidActor, {}};
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
    if(request.skill && slot.action!=MonsterAttackRule::Action::Teleport) {
        const auto sampled=ports_.effects.unitStatValues(monster->id,request.tick);
        if(!sampled || !rule.stats || !rule.formulas) return {DomainStatus::Unavailable,{}};
        const auto ranks=nativeMonsterSkillRanks(rule.baseSkillRanks,*sampled,*rule.stats,*rule.formulas);
        const auto rank=ranks.find(request.skill);
        // RogueMissile is the assigned default attack, with no learned skill slot.
        if(rank!=ranks.end() && rank->second>0 && rank->second<=255) slot.rank=uint8_t(rank->second);
        else if(!monster->hireling || slot.action!=MonsterAttackRule::Action::Damage) return {DomainStatus::Unavailable,{}};
        if(slot.weaponSkill) slot.weaponSkill->rank=slot.rank;
    }

    if(request.skill && slot.weaponSkill && slot.weaponSkill->weapon) {
        const auto &row=rule.formulas->skills.at(request.skill);
        slot.weaponSkill->weapon->startFunction=row.number("srvstfunc");
        slot.weaponSkill->weapon->doFunction=row.number("srvdofunc");
    }
    if(request.skill && !slot.weaponSkill && !slot.missile &&
       (slot.action==MonsterAttackRule::Action::Damage || slot.action==MonsterAttackRule::Action::Charge || slot.action==MonsterAttackRule::Action::Leap)) {
        const auto &row=monster->rule.formulas->skills.at(request.skill);
        SkillCastSpec skill;skill.sourceId=request.skill;skill.rank=slot.rank;skill.weapon=WeaponSkillSpec{};
        skill.weapon->startFunction=row.number("srvstfunc");skill.weapon->doFunction=row.number("srvdofunc");
        if(skill.weapon->nativeMelee()) slot.weaponSkill=std::move(skill);
    }
    auto cachedStart=runtime_->monsterWeaponStarts.find(monster->id);
    const auto heldStart=runtime_->weaponStarts.find(monster->id);
    if(cachedStart!=runtime_->monsterWeaponStarts.end() &&
       (cachedStart->second.tick!=request.tick || cachedStart->second.skill!=request.skill || cachedStart->second.mode!=request.monsterMode ||
        (heldStart!=runtime_->weaponStarts.end() && heldStart->second.unit!=target->id))) {
        cancel({},monster->id);cachedStart=runtime_->monsterWeaponStarts.end();
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
        const auto *weapon=monster->petWeapon?&*monster->petWeapon:nullptr;
        const auto sampled=ports_.effects.unitStatValues(monster->id,request.tick,weapon?std::optional{weapon->item}:std::nullopt);
        if(!sampled || !rule.stats) return {DomainStatus::Unavailable,{}};
        const auto stat=[&](std::string_view name){return logicalStatValue(*sampled,*rule.stats,name);};
        timingInput.fasterCast=stat("item_fastercastrate");timingInput.itemIAS=stat("item_fasterattackrate");
        timingInput.weaponSpeed=weapon?weapon->baseSpeed:0;timingInput.skillRate=nativeSubtract(stat("attackrate"),100);
        if(monster->assassinPet && monster->assassinPet->definition->warp && rule.formulas->skills.at(request.skill).number("cltstfunc")==9)
            timingInput.skillRate=nativeAdd(timingInput.skillRate,rule.formulas->skills.at(request.skill).number("param4"));
        timingInput.otherRate=nativeSubtract(stat("other_animrate"),100);
    }
    const auto timing=planMonsterActionTiming(timingInput);
    if(!slot.animation || slot.animationFlags.size()<size_t(slot.animation->frames)) return {DomainStatus::Unavailable,{}};
    if(slot.animationAction>=0 && size_t(slot.animationAction)<slot.animationFlags.size()) slot.animationEvent=slot.animationFlags[size_t(slot.animationAction)];
    const auto modeIdentity=ports_.frameEvents.reserveIdentity();
    auto modeTimer=ports_.frameEvents.prepareInsertion({{}, {FrameEventUnit::Monster,monster->id},FrameEventType::ModeChange,
        request.tick+1,true,0,0,0,modeIdentity});
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
        if(cachedStart!=runtime_->monsterWeaponStarts.end()) slot.modeStats=cachedStart->second.program.modeStats;
        else slot.modeStats=prepareMonsterModeStats(*table,*rule.stats,
            {request.monsterMode,rule.level,rule.difficulty,rule.statGameType,monster->admittedPlayerCount,
             monster->alignment()==UnitAlignment::Evil,ownerSkill,monster->id,request.tick+1},reserved);
        const auto value=[&](std::string_view name) {
            const auto id=rule.stats->names.at(std::string(name));
            const auto stat=slot.modeStats->base.find({id,0});return stat==slot.modeStats->base.end()?0:stat->second;
        };
        slot.minimum=value("mindamage");slot.maximum=value("maxdamage");slot.rating=value("tohit");
        slot.ownerPhysical=ownerSkill;
        modeState=attributes::withUnitModeStats(*monster->nativeStats,*rule.stats,slot.modeStats);
    }
    std::shared_ptr<Release> shadow;
    if(slot.weaponSkill && slot.weaponSkill->weapon->allocatesMeleeAtStart()) {
        if(cachedStart==runtime_->monsterWeaponStarts.end()) {
            runtime_->monsterWeaponStarts.emplace(monster->id,Runtime::MonsterWeaponStart{request.tick,request.skill,request.monsterMode,slot});
            const auto consumed=ports_.monsters.consumeRandom(monster->id,monster->combatRandom,reserved);
            if(!consumed) {runtime_->monsterWeaponStarts.erase(monster->id);return consumed;}
        }
        const auto caster=nativeCaster(ports_.players,ports_.monsters,monster->id);
        if(!caster) return {DomainStatus::InvalidActor,{}};
        const auto actor=caster->context(ports_.areas,request.tick);
        shadow=std::make_shared<Release>(Release{actor,*slot.weaponSkill,{},
            {monster->area,area.generation,destination->first},target->id,request.tick,target->type});
        shadow->weapon=monster->petWeapon.value_or(WeaponDamage{});
        const auto started=startWeaponMelee(*shadow,actor,&slot);if(!started) return started;
        if(shadow->weaponMelee->contact->monsterProgram) {
            slot.modeStats=shadow->weaponMelee->contact->monsterProgram->modeStats;
            modeState=attributes::withUnitModeStats(*monster->nativeStats,*rule.stats,slot.modeStats);
        }
        reserved=monster->combatRandom;
    }
    if(monster->assassinPet && monster->assassinPet->definition->warp) {
        const auto started=startShadowSkill(request,slot);if(!started) return {started.status,{}};
        shadow=*started.value;reserved=monster->combatRandom;
        shadow->weaponHits=timing.events;shadow->weaponFrames=timing.duration;
    }
    {
        MonsterRelease release{request,slot,monster->area,area.generation,request.tick+releaseTick,monster->interruption,{}};
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
    }
    if(slot.action==MonsterAttackRule::Action::BurrowUp || slot.action==MonsterAttackRule::Action::BurrowDown) {
        const auto changed=ports_.monsters.burrow(monster->id,slot.action==MonsterAttackRule::Action::BurrowDown,request.tick);
        if(!changed) {monsterReleases_.erase(monster->id);return changed;}
    }
    auto event = ports_.events.publish({0, request.tick, {}, {AudienceKind::Area, {}, monster->area},
        {AttackFact{monster->id, (request.position || slot.action==MonsterAttackRule::Action::Web)?EntityId{}:target->id, 1, target->type, monster->area, monster->position, slot.action==MonsterAttackRule::Action::Web?monster->position:destination->first, request.tick + 1, request.skill, slot.rank, false, request.monsterMode}}});
    if (!event) {
        // Output backpressure retains the already allocated start contact.
        if(!isDeferred(event.status)) ports_.combat.cancel(monster->id);
        monsterReleases_.erase(monster->id);return {event.status,{}};
    }
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
        const auto motion=ports_.monsters.startSkillMotion(monster->id,goal,request.tick+duration,request.skill,false,request.tick,target->id);
        if(!motion) {monsterReleases_.erase(monster->id);return motion;}
    }
    if(begun && slot.action==MonsterAttackRule::Action::Druid) {
        const auto found=monsterReleases_.find(monster->id);
        if(found!=monsterReleases_.end()) {
            const auto started=startDruidPet(found->second,{request.tick});
            if(!started && !isDeferred(started.status)) {monsterReleases_.erase(found);return started;}
        }
    }
    if(begun) {
        runtime_->monsterWeaponStarts.erase(monster->id);runtime_->weaponStarts.erase(monster->id);
        Runtime::MonsterMode mode;mode.program=slot;mode.skill=request.skill;mode.identity=modeIdentity;mode.interruption=monster->interruption;
        mode.animation.frame=slot.animationStartFrame*256;mode.animation.remaining=slot.animation->frames*256;
        if(slot.action==MonsterAttackRule::Action::Damage && !slot.missile &&
           (!slot.weaponSkill || !slot.weaponSkill->weapon->allocatesMeleeAtStart())) {
            mode.repeated=monsterReleases_.at(monster->id);
            mode.repeated->releaseFrames.clear();
        }
        runtime_->monsterModes.insert_or_assign(monster->id,std::move(mode));
        ports_.frameEvents.cancelHandler({FrameEventUnit::Monster,monster->id},FrameEventType::ModeChange,0);
        ports_.frameEvents.publish(std::move(modeTimer));
    }
    return begun;
}
StepStatus System::releaseMonsters(TickContext tick,EntityId only) {
    auto &monsterReleases_=runtime_->monsters;
    bool blocked=false;
    for (auto it=monsterReleases_.find(only);it!=monsterReleases_.end() && it->first==only;) {
        auto &pending=it->second;
        if(pending.attack.action==MonsterAttackRule::Action::Druid && (!pending.nativeSkill || !pending.nativeSkill->nativeStartPrepared)) {
            const auto *source=ports_.monsters.find(pending.request.actor);
            if(!source || !source->alive() || pending.cancelled || source->interruption!=pending.interruption || source->area!=pending.area) {
                if(pending.nativeSkill && pending.nativeSkill->druidMelee && pending.nativeSkill->druidMelee->occurrence)
                    ports_.combat.discardNativeMelee(pending.request.actor,*pending.nativeSkill->druidMelee->occurrence);
                it=monsterReleases_.erase(it);continue;
            }
            const auto started=startDruidPet(pending,tick);
            if(isDeferred(started.status)) {blocked=true;++it;continue;}
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
            if(pending.nativeSkill && pending.nativeSkill->weaponMelee && pending.nativeSkill->weaponMelee->occurrence)
                ports_.combat.discardNativeMelee(pending.request.actor,*pending.nativeSkill->weaponMelee->occurrence);
            ports_.companions.cancel(pending.request.actor);it=monsterReleases_.erase(it);continue;
        }
        auto &unit=std::get<UnitTarget>(pending.request.target);
        if(pending.attack.action==MonsterAttackRule::Action::Druid) {
            DomainResult<bool> result;
            {auto execution=state_.executingRelease(source->id);result=releaseDruidPet(pending,tick);}
            if(isDeferred(result.status)) {blocked=true;++it;continue;}
            if(result && !*result.value && !pending.cancelled) {++it;continue;}
            if(pending.nativeSkill && pending.nativeSkill->druidMelee && pending.nativeSkill->druidMelee->occurrence)
                ports_.combat.discardNativeMelee(pending.request.actor,*pending.nativeSkill->druidMelee->occurrence);
            ports_.companions.cancel(pending.request.actor);it=monsterReleases_.erase(it);continue;
        }
        if(pending.attack.action==MonsterAttackRule::Action::Assassin) {
            DomainResult<bool> result;
            {auto execution=state_.executingRelease(source->id);result=releaseAssassinPet(pending,tick);}
            if(isDeferred(result.status)) {blocked=true;++it;continue;}
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
            const auto motion=ports_.monsters.startSkillMotion(source->id,pending.launch->second*2.f-pending.launch->first,tick.tick+uint64_t(pending.attack.duration),pending.request.skill,true,tick.tick);
            if(isDeferred(motion.status)) {blocked=true;++it;continue;}
            it=monsterReleases_.erase(it);continue;
        }
        if(!pending.releaseRankPrepared) {
            const bool heldStart=pending.nativeSkill && pending.nativeSkill->weaponMelee && pending.attack.weaponSkill &&
                pending.attack.weaponSkill->weapon->allocatesMeleeAtStart();
            if(pending.request.skill && !heldStart && pending.attack.action!=MonsterAttackRule::Action::Teleport) {
                const auto sampled=ports_.effects.unitStatValues(source->id,tick.tick);
                if(!sampled) {it=monsterReleases_.erase(it);continue;}
                const auto ranks=nativeMonsterSkillRanks(source->rule.baseSkillRanks,*sampled,*source->rule.stats,*source->rule.formulas);
                const auto rank=ranks.find(pending.request.skill);
                if(rank!=ranks.end() && rank->second>0 && rank->second<=255) pending.attack.rank=uint8_t(rank->second);
                else if(!source->hireling || pending.attack.action!=MonsterAttackRule::Action::Damage) {it=monsterReleases_.erase(it);continue;}
                if(pending.attack.weaponSkill) pending.attack.weaponSkill->rank=pending.attack.rank;
            }
            pending.releaseRankPrepared=true;
        }
        DomainResult<> specialResult;
        bool special=true;
        switch(pending.attack.action) {
        case MonsterAttackRule::Action::Animation: specialResult={DomainStatus::Applied,std::monostate{}};break;
        case MonsterAttackRule::Action::Skill: {
            if(!source->hireling || !source->owner) {specialResult={DomainStatus::InvalidActor,{}};break;}
            const auto *owner=ports_.players.find(*source->owner);
            if(!owner || !owner->entered || owner->area!=source->area || owner->persistent.player.hp<=0) {specialResult={DomainStatus::InvalidActor,{}};break;}
            const ActorContext actor{owner->player,owner->actor,source->area,pending.generation,0,tick.tick};
            const auto caster=nativeCaster(ports_.players,ports_.monsters,source->id);
            if(!caster || !caster->programs() || !caster->programs()->definitions.contains(pending.request.skill)) {specialResult={DomainStatus::Unavailable,{}};break;}
            // The hosting snapshot identifies the program. Values and event
            // bindings belong to the actual caster at the release frame.
            const auto cast=evaluateNativeSkill(*caster,ports_.effects,pending.request.skill,pending.attack.rank,tick.tick);
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
        case MonsterAttackRule::Action::Heal:
        case MonsterAttackRule::Action::BurrowDown: {
            const bool bestow=pending.attack.action==MonsterAttackRule::Action::Heal;
            if(bestow && !ports_.monsters.mummySupportTarget(source->id,unit.id,tick.tick,false)) {specialResult={DomainStatus::InvalidActor,{}};break;}
            const auto *victim=bestow?ports_.monsters.find(unit.id):source;
            if(!pending.formulaPrepared) {
                const auto sampled=ports_.effects.unitStatValues(source->id,tick.tick);if(!sampled) {specialResult={DomainStatus::Unavailable,{}};break;}
                const auto expected=source->combatRandom;auto random=expected;
                const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {return low>=high?low:nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));};
                auto input=formulaInput(*source,*sampled,draw);SkillFormulaValues values(*source->rule.formulas,input);
                const auto formula=[&](std::string_view field) {const auto value=values.calculation(FormulaDomain::Skill,pending.request.skill,pending.attack.rank,field);
                    if(!value) throw std::logic_error("Missing native monster healing formula");return *value;};
                if(bestow) {
                    const auto maximum=std::clamp(formula("calc2"),0,100),minimum=std::clamp(formula("calc1"),0,maximum);
                    pending.formulaValue=nativeAdd(minimum,int32_t(limitedRandom(random,uint32_t(maximum-minimum))));
                } else pending.formulaValue=std::max(formula("calc1"),0);
                pending.formulaRandom={expected,random};pending.formulaPrepared=true;
            }
            const auto amount=nativePercentage(nativeChecked(bestow?victim->maximumLife:victim->life),pending.formulaValue,100);
            specialResult=ports_.monsters.healSkill(source->id,victim->id,std::max(amount,0),tick.tick,pending.formulaRandom);
            break;
        }
        case MonsterAttackRule::Action::BurrowUp: specialResult={DomainStatus::Applied,std::monostate{}};break;
        case MonsterAttackRule::Action::Lay: {
            const auto target=ports_.monsters.targetPosition(unit.id,source->area);
            pending.nativeCallbackStarted=true;
            specialResult=target?ports_.monsters.spawnSkillChildren(source->id,target->first,1,false,tick.tick,pending.children):DomainResult<>{DomainStatus::InvalidActor,{}};
            pending.nativeCallbackStarted=pending.children && pending.children->published && !pending.children->complete;break;
        }
        case MonsterAttackRule::Action::Hatch: {
            if(!pending.formulaPrepared) {
                const auto sampled=ports_.effects.unitStatValues(source->id,tick.tick);if(!sampled) {specialResult={DomainStatus::Unavailable,{}};break;}
                const auto expected=source->combatRandom;auto random=expected;
                const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {return low>=high?low:nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));};
                auto input=formulaInput(*source,*sampled,draw);SkillFormulaValues values(*source->rule.formulas,input);
                const auto count=values.calculation(FormulaDomain::Skill,pending.request.skill,pending.attack.rank,"calc1");
                if(!count) {specialResult={DomainStatus::Unavailable,{}};break;}
                specialResult=ports_.monsters.consumeRandom(source->id,expected,random);if(!specialResult) break;
                pending.formulaValue=*count;pending.formulaPrepared=true;
            }
            pending.nativeCallbackStarted=true;
            specialResult=ports_.monsters.spawnSkillChildren(source->id,source->position,pending.formulaValue,true,tick.tick,pending.children);
            pending.nativeCallbackStarted=pending.children && pending.children->published && !pending.children->complete;
            break;
        }
        case MonsterAttackRule::Action::SwarmMove: {
            const auto target=ports_.monsters.targetPosition(unit.id,source->area);
            specialResult=target?ports_.monsters.startSwarmMove(source->id,target->first,tick.tick):DomainResult<>{DomainStatus::InvalidActor,{}};break;
        }
        case MonsterAttackRule::Action::Curse: {
            const auto target=ports_.monsters.targetPosition(unit.id,source->area);
            if(!target && !pending.curse) {specialResult={DomainStatus::InvalidActor,{}};break;}
            specialResult=ports_.effects.monsterCurse(source->id,pending.request.skill,pending.attack.rank,target?target->first:source->position,tick.tick,pending.curse);
            pending.nativeCallbackStarted=pending.curse && pending.curse->prepared && !pending.curse->complete;
            break;
        }
        case MonsterAttackRule::Action::Nest: {const auto spawned=pending.request.position?ports_.monsters.spawnNestChild(source->id,tick.tick,*pending.request.position):ports_.monsters.spawnNestChild(source->id,tick.tick);specialResult={spawned.status,spawned?std::optional{std::monostate{}}:std::nullopt};break;}
        case MonsterAttackRule::Action::Web: specialResult=ports_.monsters.activateWeb(source->id,pending.request.skill,pending.attack.rank,tick.tick);break;
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
            if(isDeferred(specialResult.status)) {blocked=true;++it;continue;}
            if(specialResult && ++pending.nextRelease<pending.releaseFrames.size()) {pending.formulaPrepared=false;pending.releaseRankPrepared=false;pending.curse.reset();pending.children.reset();pending.due=pending.started+uint64_t(pending.releaseFrames[pending.nextRelease]);++it;continue;}
            it=monsterReleases_.erase(it);continue;
        }
        const auto target=ports_.monsters.targetPosition(unit.id,pending.area);
        if(!pending.launch && !combat::Participants{ports_.players,ports_.monsters,ports_.social}.canHarm(source->id,unit.id)) {it=monsterReleases_.erase(it);continue;}
        if (!target && !pending.launch) {it=monsterReleases_.erase(it);continue;}
        if(!pending.attack.missile) {
            pending.nativeCallbackStarted=true;
            DomainResult<> result;
            if(pending.nativeSkill && pending.nativeSkill->weaponMelee) {
                auto actor=pending.nativeSkill->actor;actor.tick=tick.tick;
                const auto status=drainWeaponMelee(*pending.nativeSkill,actor);
                result={status,status==DomainStatus::Applied?std::optional{std::monostate{}}:std::nullopt};
            } else result=ports_.combat.monsterMelee(source->id,unit.id,pending.attack,pending.started+1,tick);
            if(isDeferred(result.status) || result.status==DomainStatus::Conflict) {blocked=true;++it;continue;}
            pending.nativeCallbackStarted=false;
            if(result && pending.attack.action==MonsterAttackRule::Action::Leap) {
                pending.hitReleased=true;pending.due=std::max(tick.tick+1,pending.started+uint64_t(pending.attack.movementStart));++it;continue;
            }
            if(result && ++pending.nextRelease<pending.releaseFrames.size()) {pending.releaseRankPrepared=false;pending.due=pending.started+uint64_t(pending.releaseFrames[pending.nextRelease]);++it;continue;}
            it=monsterReleases_.erase(it);continue;
        }
        if(!pending.launch) pending.launch=std::pair{source->position,target->first};
        const int quills=source->rule.ai.kind==MonsterAiKind::QuillRat && pending.request.monsterMode==5?source->rule.ai.params[2]:0;
        auto launch=*pending.launch;
        if(pending.attack.action==MonsterAttackRule::Action::Spray) launch=andarielSprayRay(source->position,pending.launch->second,4+int(pending.nextRelease));
        if(pending.attack.action==MonsterAttackRule::Action::Trap) launch=gargoyleTrapRay(source->position,pending.launch->second,GargoyleRaySide::Server);
        if(pending.attack.action==MonsterAttackRule::Action::Firewall) launch={pending.launch->second,source->position};
        missiles::MonsterSpawn spawn{source->id,source->area,pending.generation,tick.tick,launch.first,launch.second,pending.attack,source->rule.hitStates,source->rule.level,source->rule.criticalChance,quills};
        spawn.nativeSkill=pending.request.skill;
        if(pending.attack.action==MonsterAttackRule::Action::Nova) for(const Vec heading:missileRingBurst(1)) spawn.aims.push_back(source->position+heading);
        if(pending.attack.missile->behavior==MonsterMissileRule::Behavior::Meteor) spawn.position=launch.second;
        const auto result=ports_.missiles.spawnMonster(spawn);
        if (isDeferred(result.status)) {blocked=true;++it;continue;}
        if(result && pending.attack.action==MonsterAttackRule::Action::Spray && ++pending.nextRelease<pending.releaseFrames.size()) {
            pending.releaseRankPrepared=false;pending.due=pending.started+uint64_t(pending.releaseFrames[pending.nextRelease]);++it;continue;
        }
        it=monsterReleases_.erase(it);
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
