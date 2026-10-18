#include "system.hpp"
#include "gameplay/skills/targeting.hpp"
#include "runtime.hpp"
#include "shape.hpp"
#include "gameplay/skills/druid_spec.hpp"
#include "assassin_kick.hpp"
#include "druid_melee.hpp"
#include "weapon_melee.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/skills/bone_spec.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include "gameplay/skills/assassin_sequence.hpp"
#include "gameplay/skills/leap_spec.hpp"
#include "gameplay/skills/barbarian_sequence.hpp"
#include "gameplay/skills/amazon_sequence.hpp"
#include "server/systems/transactions/system.hpp"
#include "evaluation.hpp"
#include "server/player_store.hpp"
#include "server/movement.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/companions/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/inventory/system.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/combat/geometry.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace d2x::server::skills {
namespace {
bool inside(const Grid &g, Vec p) {return std::isfinite(p.x) && std::isfinite(p.y) && p.x>=0 && p.y>=0 && p.x<g.width && p.y<g.height;}
bool within(Vec from,Vec to,int r) {const int x=int(to.x)-int(from.x),y=int(to.y)-int(from.y);return x*x+y*y<=r*r;}
}
std::optional<Vec> System::unitPosition(const ActorContext &actor,UnitTarget unit,SkillBehavior skill) const {
    if(unit.type==0) {
        const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
        const auto target=participants.find(unit.id);
        if(target.player && target.aliveIn(actor.area) && (skill==SkillBehavior::Enchant || skill==SkillBehavior::HolyBolt || participants.canHarm(actor.actor,unit.id))) return target.position();
    } else if(unit.type==1) {
        const auto *m=ports_.monsters.find(unit.id);
        const bool consumes=skill==SkillBehavior::RaiseSkeleton || skill==SkillBehavior::CorpseExplosion || skill==SkillBehavior::PoisonExplosion;
        if((consumes?ports_.monsters.consumableCorpse(unit.id,actor.area,actor.tick):
            (skill==SkillBehavior::FindPotion || skill==SkillBehavior::FindItem || skill==SkillBehavior::GrimWard) && ports_.monsters.usableCorpse(unit.id,actor.area,actor.tick)) &&
            ((skill!=SkillBehavior::FindPotion && skill!=SkillBehavior::GrimWard) || m->rule.softCorpse)) return m->position;
        const bool enemy=m && combat::Participants{ports_.players,ports_.monsters,ports_.social}.canHarm(actor.actor,m->id);
        if(m && m->area==actor.area && m->alive() && (skill==SkillBehavior::HolyBolt ? enemy || bool(m->owner) : skill==SkillBehavior::Enchant ? true : skill==SkillBehavior::Unsummon ? m->owner==actor.player : enemy)) return m->position;
    } else if(skill==SkillBehavior::Kick && unit.type==2) {
        const auto found=ports_.objects.read().objects.find(unit.id);
        if(found!=ports_.objects.read().objects.end() && found->second.area==actor.area && found->second.rule.operation==5) return found->second.position;
    } else if(unit.type==4 && skill==SkillBehavior::RaiseSkeleton) return ports_.inventory.groundPosition(unit.id,actor.area);
    else if(skill==SkillBehavior::Telekinesis) {
        if(unit.type==2) {const auto found=ports_.objects.read().objects.find(unit.id);if(found!=ports_.objects.read().objects.end() && found->second.area==actor.area) return found->second.position;return ports_.travel.portalPosition(actor,unit.id);}
        if(unit.type==4) return ports_.inventory.groundPosition(unit.id,actor.area);
    }
    return {};
}
DomainResult<> System::cast(const ActorContext &actor,const Request &request,int selected) {
    auto &releases_=runtime_->releases;
    const auto &p=*ports_.players.find(actor.player);const auto &area=ports_.areas.at(actor.area);
    if(!p.rules.skills) return {DomainStatus::Unavailable,{}};
    const auto &rules=*p.rules.skills;const auto found=rules.definitions.find(selected);if(found==rules.definitions.end()) return {};
    const auto &definition=found->second;
    if(definition.spec.weapon && !definition.spec.leap && !(definition.spec.assassin &&
       (definition.spec.assassin->program==AssassinProgram::DragonTalon || definition.spec.assassin->program==AssassinProgram::DragonTail ||
        definition.spec.assassin->program==AssassinProgram::DragonFlight))) return weaponCast(actor,request,selected);
    if(!request.target) return {DomainStatus::InvalidRequest,{}};
    const bool itemSkill=definition.spec.effect==SkillBehavior::ItemSkill;
    PointTarget target{actor.area,actor.areaGeneration,{}};EntityId unit;uint8_t type=1;
    if(const auto *point=std::get_if<PointTarget>(&*request.target)) {
        if(point->area!=actor.area || point->generation!=actor.areaGeneration) return {DomainStatus::Stale,{}};
        target=*point;
    } else if(itemSkill) {
        target.position=p.position; // SrvDo113 searches inventory; no monster target is required.
    } else {
        const auto &key=std::get<UnitTarget>(*request.target);unit=key.id;type=key.type;
        const auto position=unitPosition(actor,key,definition.spec.effect);if(!position) return {DomainStatus::InvalidRequest,{}};target.position=*position;
    }
    if(!inside(area.definition.collision,target.position) || std::abs(target.position.x-p.position.x)>50 || std::abs(target.position.y-p.position.y)>50)
        return {DomainStatus::InvalidRequest,{}};
    const auto source=selectedSource(p,selected,request.right);
    if(!source) return {DomainStatus::Unavailable,{}};
    const NativeSkillSlot selectedUse{selected,source->charged?source->charged->item.id:EntityId{}};
    if(auto channel=releases_.find(actor.actor);channel!=releases_.end() && channel->second.skill.druid &&
       channel->second.skill.druid->program==DruidProgram::Arctic && selected==channel->second.skill.sourceId && state_.usedSkill(actor.actor)==selectedUse) {
        auto &current=channel->second;
        if(!ports_.events.hasCapacity(3,3)) return {DomainStatus::Capacity,{}};
        const auto started=startArctic(current,actor);if(!started) return {started.status,{}};
        if(*started.value) {const auto result=activateDruid(current,actor,target.position);if(result!=DomainStatus::Applied) return {result,{}};current.tick=std::min(current.tick,actor.tick+1);}
        else {current.tick=actor.tick+10;current.pulses=0;state_.casts.at(actor.actor).until=actor.tick+15;}
        if((current.target.position-target.position).length()>0 || current.unit!=unit || current.unitType!=type) {
            const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},
                {AttackFact{actor.actor,unit,0,type,actor.area,p.position,target.position,actor.sequence,uint16_t(selected),uint8_t(current.skill.rank)}}});
            if(!sent) return {sent.status,{}};
            current.target=target;current.unit=unit;current.unitType=type;
        }
        return {DomainStatus::Applied,std::monostate{}};
    }
    if(auto channel=releases_.find(actor.actor);channel!=releases_.end() && channel->second.skill.assassin &&
       channel->second.skill.assassin->program==AssassinProgram::BladeFury && selected==channel->second.skill.sourceId && state_.usedSkill(actor.actor)==selectedUse) {
        auto &current=channel->second;
        if(!ports_.events.hasCapacity(2,2)) return {DomainStatus::Capacity,{}};
        if(current.pulses) {
            const auto start=startBladeFury(current,actor);if(!start) return {start.status,{}};
            if(*start.value) {
                const auto result=activateAssassin(current,actor,target.position);if(result!=DomainStatus::Applied) return {result,{}};
                current.tick=std::min(current.tick,actor.tick+1);
            } else {current.tick=actor.tick+12;current.pulses=0;state_.casts.at(actor.actor).until=actor.tick+19;}
        }
        if((current.target.position-target.position).length()>0 || current.unit!=unit || current.unitType!=type) {
            const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},
                {AttackFact{actor.actor,unit,0,type,actor.area,p.position,target.position,actor.sequence,uint16_t(selected),uint8_t(current.skill.rank)}}});
            if(!sent) return {sent.status,{}};
            current.target=target;current.unit=unit;current.unitType=type;
        }
        return {DomainStatus::Applied,std::monostate{}};
    }
    if(auto channel=releases_.find(actor.actor);channel!=releases_.end() && channel->second.skill.effect==SkillBehavior::Inferno &&
        selected==channel->second.skill.sourceId && state_.usedSkill(actor.actor)==selectedUse) {
        auto &current=channel->second;
        if((current.target.position-target.position).length()>0 || current.unit!=unit || current.unitType!=type) {
            const auto output=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},
                {AttackFact{actor.actor,unit,0,type,actor.area,p.position,target.position,actor.sequence,uint16_t(selected),uint8_t(current.skill.rank)}}});
            if(!output) return {output.status,{}};
            current.target=target;current.unit=unit;current.unitType=type;
        }
        return {DomainStatus::Applied,std::monostate{}};
    }
    if(busy(actor.actor,actor.tick)) return {DomainStatus::Conflict,{}};
    if(definition.spec.delayFrames>0) {
        if(state_.coolingDown(actor.actor,actor.tick)) return {DomainStatus::Conflict,{}};
    }
    const int effectiveRank=source->rank;
    if(effectiveRank<=0 || effectiveRank>255) return {DomainStatus::InvalidRequest,{}};
    // ObjMode calls PLAYERMODE_Change directly, including barrels in town;
    // it does not apply player-selected spell town eligibility to hidden KK.
    const bool objectKick=definition.spec.effect==SkillBehavior::Kick && type==2;
    if(area.definition.town && !definition.allowedInTown && !objectKick) return {DomainStatus::Unavailable,{}};
    auto skill=evaluate(p,selected,effectiveRank);
    if(skill.leap) {
        Vec landing=target.position;
        const int distance=missileDistance(p.position,landing);
        if(skill.leap->attack) {
            if(unit) {const auto m=combat::Participants{ports_.players,ports_.monsters,ports_.social}.find(unit);if(m.id() && meleeDistance(p.position,2,m.position(),m.size())<=p.totals.equipment.weapons[0].rangeAdder+1) return attack(actor,request,0);}
            if(distance) landing=landing+Vec{float(2*(int(landing.x)-int(p.position.x))/distance),float(2*(int(landing.y)-int(p.position.y))/distance)};
        } else if(distance>skill.leap->range) landing=p.position+(landing-p.position)*(float(skill.leap->range)/float(distance));
        const int dx=std::clamp(int(p.position.x)-int(landing.x),-2,2),dy=std::clamp(int(landing.y)-int(p.position.y),-2,2);
        std::optional<Vec> chosen;
        for(const Vec attempt:{landing,landing+Vec{float(dy),float(dx)},landing-Vec{float(dy),float(dx)}}) {
            const auto candidate=area.definition.collision.nativeSpawn(attempt,1,playerMovement);
            if(!candidate || (!skill.leap->attack && missileDistance(p.position,*candidate)>skill.leap->range) || !area.definition.collision.collisionSegment(p.position,*candidate,0x804)) continue;
            bool occupied=false;
            for(const auto &[id,m]:ports_.monsters.read().actors) { (void)id;if(m.area==actor.area && m.life>0 && meleeDistance(*candidate,2,m.position,m.rule.size)<=0) {occupied=true;break;} }
            if(!occupied) {chosen=candidate;break;}
        }
        if(!chosen || (*chosen-p.position).length()<.001f) return {DomainStatus::Unavailable,{}};
        target.position=*chosen;if(!skill.leap->attack) unit={};
    }
    if(skill.requiresShield && !p.totals.equipment.shield) return {DomainStatus::Unavailable,{}};
    if(skill.effect==SkillBehavior::FistOfTheHeavens && !unit) return {DomainStatus::InvalidRequest,{}};
    if (activationProgram(skill) == ActivationProgram::Unsupported) return {DomainStatus::NotImplemented,{}};
    if(skill.druid && skill.druid->program==DruidProgram::Shape && ports_.effects.unitStates(actor.actor,actor.tick).contains(skill.druid->state.id))
        skill.manaCost=skill.startMana=0;
    if(source->charged) {skill.charge=SkillCharge{source->charged->item,source->charged->layer};skill.manaCost=skill.startMana=0;}
    if(!std::isfinite(skill.manaCost) || skill.manaCost<0 || !std::isfinite(skill.startMana) || skill.startMana<0 ||
        p.persistent.player.mana<std::max(skill.manaCost,skill.startMana)) return {DomainStatus::Unavailable,{}};
    if(skill.effect==SkillBehavior::Teleport && (!area.definition.teleportAllowed || !area.definition.collision.walkable(target.position,playerMovement))) return {DomainStatus::Unavailable,{}};
    if(skill.effect==SkillBehavior::Telekinesis && (!unit || !within(p.position,target.position,skill.telekinesisRange))) return {DomainStatus::InvalidRequest,{}};
    if((skill.effect==SkillBehavior::Blizzard || skill.effect==SkillBehavior::Hydra || skill.effect==SkillBehavior::FireWall || skill.effect==SkillBehavior::Meteor) &&
        !area.definition.collision.missileSegment(target.position,target.position,{5,1})) return {DomainStatus::Unavailable,{}};
    const bool inferno=skill.effect==SkillBehavior::Inferno;
    CastTiming timing;
    std::optional<ActionTimeline> timeline;
    if(skill.druid && skill.druid->program==DruidProgram::Arctic) {
        timing=playerCastSequenceTiming(*playerCastSequence(18),256);
    } else if(skill.assassin && skill.assassin->sequence==23) {
        const auto sequence=assassinSequence(23,p.totals.equipment.animationClass);
        const auto hits=weaponSequenceHitTicks(sequence,256);if(hits.empty()) return {DomainStatus::Unavailable,{}};
        timing={int(sequence.size()),hits.front(),256};
    } else if(skill.assassin && skill.assassin->mode=="s2") {
        const auto animation=rules.weaponAnimations.find("s2"+p.totals.equipment.animationClass);
        if(animation==rules.weaponAnimations.end()) return {DomainStatus::Unavailable,{}};
        const auto &a=animation->second;const auto &weapon=p.totals.equipment.weapons[0];
        const auto speed=skill.assassin->useAttackRate?
            effectiveAttackSpeed(a.speed,weapon.fasterAttack,weapon.baseSpeed,p.totals.character.combat.attackRate):a.speed;
        const WeaponAttackTiming clock{"s2",a.frames,speed,a.actionFrame,0};
        timing={clock.durationTicks(),clock.actionTick(),clock.speed};
        timeline=playerActionTimeline(a.frames,speed,a.startFrame,a.events);
        const auto action=std::find_if(timeline->events.begin(),timeline->events.end(),[](const auto &event){return event.event==1 || event.event==2;});
        timing.duration=timeline->end;timing.impact=action==timeline->events.end()?timeline->end:action->tick;
    } else if(skill.effect!=SkillBehavior::Kick) {
        const auto animation=rules.animations.find(p.totals.equipment.animationClass);
        if(animation==rules.animations.end()) return {DomainStatus::Unavailable,{}};
        timing=sorceressCastTiming(animation->second,p.totals.character.combat.fasterCast,bool(skill.arc),inferno);
        const bool ordinary=(!skill.assassin || skill.assassin->mode=="sc") &&
            (!skill.druid || (skill.druid->mode=="sc" && skill.druid->program!=DruidProgram::Shape));
        if(ordinary && !inferno && !skill.leap) {
            if(skill.arc) {
                const auto sequence=*playerCastSequence(12);
                std::vector<uint8_t> flags(sequence.frames.size());flags[size_t(sequence.releaseFrame)]=1;
                timeline=playerActionTimeline(int(sequence.frames.size()),timing.speed,0,flags);
            } else timeline=playerActionTimeline(animation->second.frames,timing.speed,0,animation->second.events);
            const auto action=std::find_if(timeline->events.begin(),timeline->events.end(),[](const auto &event){return event.event==1 || event.event==2;});
            timing.duration=timeline->end;timing.impact=action==timeline->events.end()?timeline->end:action->tick;
        }
    }
    if(const auto *shape=activeShape(rules,ports_.effects.unitStates(actor.actor,actor.tick))) {
        const bool scheduled=timeline.has_value();timeline.reset();
        const auto mode=skill.druid?skill.druid->mode:std::string{"sc"};
        const auto clock=shapeActionTiming(p,*shape,mode,rules.formulas->skills.at(skill.sourceId).number("useattackrate")!=0);if(!clock) return {DomainStatus::Unavailable,{}};
        timing={clock->durationTicks(),clock->actionTick(),clock->speed};
        if(scheduled) {
            timeline=playerActionTimeline(clock->frames,clock->speed,clock->startFrame,clock->events);
            const auto action=std::find_if(timeline->events.begin(),timeline->events.end(),[](const auto &event){return event.event==1 || event.event==2;});
            timing.duration=timeline->end;timing.impact=action==timeline->events.end()?timeline->end:action->tick;
        }
    }
    if(skill.effect==SkillBehavior::Kick) {
        const auto victim=combat::Participants{ports_.players,ports_.monsters,ports_.social}.find(unit);
        if(!unit || (type!=2 && (type>1 || !victim.id()))) return {DomainStatus::InvalidRequest,{}};
        // Object operation has already checked the original interaction geometry.
        if(type!=2 && !playerMeleeReach(area.definition.collision,p.position,target.position,victim.size(),0)) {
            if(request.stationary) return {DomainStatus::Unavailable,{}};
            const auto movement=ports_.movement.execute(actor,{MovementAction::Move,target.position,true,{},MotionTarget{unit,type}});
            return {movement==CommandStatus::Applied?DomainStatus::Conflict:DomainStatus::Unavailable,{}};
        }
        const auto kick=rules.weaponAnimations.find("kk"+p.totals.equipment.animationClass);
        if(kick==rules.weaponAnimations.end()) return {DomainStatus::Unavailable,{}};
        const auto &a=kick->second;
        const auto &weapon=p.totals.equipment.weapons[0];
        WeaponAttackTiming clock{"kk",a.frames,effectiveAttackSpeed(a.speed,weapon.fasterAttack,weapon.baseSpeed,p.totals.character.combat.attackRate),a.actionFrame,0};
        timing={clock.durationTicks(),clock.actionTick(),clock.speed};
        timeline=playerActionTimeline(a.frames,clock.speed,a.startFrame,a.events);
        const auto action=std::find_if(timeline->events.begin(),timeline->events.end(),[](const auto &event){return event.event==1 || event.event==2;});
        timing.duration=timeline->end;timing.impact=action==timeline->events.end()?timeline->end:action->tick;
    }
    if(skill.effect==SkillBehavior::Unsummon && (!unit || !ports_.companions.canDismiss(actor,unit))) return {DomainStatus::InvalidRequest,{}};
    if(skill.leap) timing=playerCastSequenceTiming(*playerCastSequence(13),256);
    if(!ports_.events.hasCapacity(inferno?2:1,inferno?2:1)) return {DomainStatus::Capacity,{}};
    Release release{actor,std::move(skill),definition.collision,target,unit,actor.tick+uint64_t(timing.impact),type};
    if(release.skill.effect==SkillBehavior::Kick && type!=2) {
        auto start=release;
        start.skill.weapon=WeaponSkillSpec{};start.skill.weapon->startFunction=2;start.skill.weapon->doFunction=2;
        start.weapon=p.totals.equipment.weapons[0];
        const auto prepared=startWeaponMelee(start,actor);if(!prepared) return prepared;
        release.weaponMelee=std::move(start.weaponMelee);
    }
    if(release.skill.assassin && (release.skill.assassin->program==AssassinProgram::DragonTalon ||
       release.skill.assassin->program==AssassinProgram::DragonTail || release.skill.assassin->program==AssassinProgram::DragonFlight)) {
        const auto start=startAssassinKick(release,actor);if(!start) return start;
        const auto kick=rules.weaponAnimations.find("kk"+p.totals.equipment.animationClass);
        if(kick==rules.weaponAnimations.end()) return {DomainStatus::Unavailable,{}};
        const auto &a=kick->second;const auto &weapon=p.totals.equipment.weapons[0];
        const auto speed=effectiveAttackSpeed(a.speed,weapon.fasterAttack,weapon.baseSpeed,p.totals.character.combat.attackRate);
        WeaponAttackTiming clock{"kk",a.frames,speed,a.actionFrame,0};
        timing={clock.durationTicks(),clock.actionTick(),clock.speed};
        release.weaponSpeed=clock.speed;release.weaponFrames=a.frames;release.animationEvents=a.events;
        timeline=playerActionTimeline(a.frames,clock.speed,a.startFrame,a.events);
        timing.duration=timeline->end;
        const auto action=std::find_if(timeline->events.begin(),timeline->events.end(),[](const auto &event){return event.event==1 || event.event==2;});
        timing.impact=action==timeline->events.end()?timeline->end:action->tick;
        release.weaponHits={timing.impact};
        if(release.skill.assassin->program==AssassinProgram::DragonFlight) {
            const auto sequence=assassinSequence(21,p.totals.equipment.animationClass);
            release.weaponHits=weaponSequenceHitTicks(sequence,256);
            release.weaponSpeed=256;release.weaponFrames=int(sequence.size());release.animationEvents.clear();
            for(const auto &frame:sequence) release.animationEvents.push_back(frame.hit?1:0);
            timeline=playerActionTimeline(release.weaponFrames,256,0,release.animationEvents);
            timing={timeline->end,release.weaponHits.front(),256};
        }
        release.tick=actor.tick+uint64_t(timing.impact);
    }
    if(release.skill.leap && release.skill.leap->attack) {
        const auto end=p.totals.equipment.weapons.begin()+p.totals.equipment.weaponCount;
        const auto weapon=std::find_if(p.totals.equipment.weapons.begin(),end,[&](const auto &w){return !w.leftHand && std::find(w.types.begin(),w.types.end(),release.skill.weapon->requiredType)!=w.types.end();});
        if(weapon==end) return {DomainStatus::Unavailable,{}};
        release.weapon=*weapon;release.weaponSpeed=effectiveAttackSpeed(256,weapon->fasterAttack,weapon->baseSpeed,p.totals.character.combat.attackRate-30);
    }
    const auto previous=state_.casts.find(actor.actor);const auto saved=previous==state_.casts.end()?std::optional<Cast>{}:previous->second;
    auto preparedUse=state_.prepareUsedSkill(actor.actor,selectedUse);
    auto cast=Cast{actor.actor,uint16_t(selected),actor.tick,actor.sequence,actor.tick+uint64_t(timing.duration),actor.area,unit};
    cast.usesSkillPointer=true;
    std::optional<State::PreparedTimeline> preparedTimeline;
    if(timeline) {
        preparedTimeline=state_.prepareTimeline(actor.actor,actor.tick,*timeline);
        cast.timeline=preparedTimeline->generation;release.eventDriven=true;
    } else {
        preparedTimeline=state_.prepareProgramTimeline(actor.actor,actor.tick);
        cast.timeline=preparedTimeline->generation;release.eventDriven=true;
    }
    if(saved) cast.cooldownUntil=saved->cooldownUntil;
    std::map<EntityId,Release> preparedRelease;preparedRelease.emplace(actor.actor,std::move(release));
    std::map<EntityId,Cast> preparedCast;preparedCast.emplace(actor.actor,cast);
    auto &staged=preparedRelease.at(actor.actor);
    const auto action=AttackFact{actor.actor,unit,0,type,actor.area,p.position,target.position,actor.sequence,
        uint16_t(selected),uint8_t(effectiveRank),bool(staged.skill.leap)};
    if(staged.assassinKick || (staged.skill.assassin && staged.skill.assassin->program==AssassinProgram::PsychicHammer)) {
        const auto debit=ports_.transactions.release(actor,p.characterRevision,staged.skill.manaCost,{},staged.skill.charge,{action},{},staged.skill.sourceId);
        if(!debit) return debit;
        staged.manaPaid=true;runtime_->assassinStarts.erase(actor.actor);
    } else if(inferno) {
        const auto debit=ports_.transactions.release(actor,p.characterRevision,staged.skill.manaCost,{},staged.skill.charge,{action},{},staged.skill.sourceId);
        if(!debit) return debit;
        if(staged.skill.charge) {
            staged.skill.charge->item=p.persistent.inventory.items.at(staged.skill.charge->item.id).handle();
            // Only the charged path recomputed attributes in this transaction.
            // Resource-only release must not replay an older removal ledger.
            removeSkillUse(preparedUse.mapped(),p.totals.clearedSkillUses);
        }
        staged.manaPaid=true;
    } else if(staged.skill.druid && staged.skill.druid->program==DruidProgram::Arctic) {
        const std::array<DomainFact,1> facts{action};const auto started=startArctic(staged,actor,facts);if(!started) return {started.status,{}};
    } else if(staged.skill.assassin && staged.skill.assassin->program==AssassinProgram::BladeFury) {
        const std::array<DomainFact,1> facts{action};const auto started=startBladeFury(staged,actor,facts);if(!started) return {started.status,{}};
    } else if(staged.skill.assassin && staged.skill.assassin->program==AssassinProgram::BladeShield) {
        if(!staged.skill.appliedEffect || !staged.skill.appliedEffect->duration || !*staged.skill.appliedEffect->duration || staged.skill.missileId<=0)
            return {DomainStatus::Unavailable,{}};
        const std::array<DomainFact,1> facts{action};
        const auto installed=ports_.effects.skill(actor,staged.skill,{},facts);if(!installed) return installed;
        staged.manaPaid=true;
    } else {
        const auto output=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},{action}});
        if(!output) return {output.status,{}};
    }
    if(const auto start=runtime_->weaponStarts.find(actor.actor);start!=runtime_->weaponStarts.end()) {
        if(start->second.weaponMelee!=staged.weaponMelee && start->second.weaponMelee && start->second.weaponMelee->occurrence)
            ports_.combat.discardNativeMelee(actor.actor,*start->second.weaponMelee->occurrence);
        runtime_->weaponStarts.erase(start);
    }
    if(releases_.contains(actor.actor)) ports_.companions.cancel(actor.actor);
    releases_.erase(actor.actor);releases_.merge(preparedRelease);
    state_.casts.erase(actor.actor);state_.casts.merge(preparedCast);
    state_.commitUsedSkill(std::move(preparedUse));
    if(preparedTimeline) state_.commitTimeline(std::move(*preparedTimeline));
    else state_.cancelTimeline(actor.actor);
    ports_.movement.execute(actor,{MovementAction::Stop,{},false});return {DomainStatus::Applied,std::monostate{}};
}
System::ActivationProgram System::activationProgram(const SkillCastSpec &skill) {
    return skillActivationProgram(skill);
}
DomainStatus System::activate(Release &pending,const ActorContext &actor,Vec target) {
    using Handler = DomainStatus (System::*)(Release &, const ActorContext &, Vec);
    struct Entry { ActivationProgram program; Handler handler; };
    static constexpr std::array entries{
        Entry{ActivationProgram::Weapon, &System::weaponRelease},
        Entry{ActivationProgram::Item, &System::activateItem},
        Entry{ActivationProgram::Unsummon, &System::activateUnsummon},
        Entry{ActivationProgram::Kick, &System::activateKick},
        Entry{ActivationProgram::AmazonSummon, &System::activateAmazonSummon},
        Entry{ActivationProgram::AmazonMagic, &System::activateAmazonMagic},
        Entry{ActivationProgram::Teleport, &System::activateTeleport},
        Entry{ActivationProgram::Leap, &System::activateLeap},
        Entry{ActivationProgram::Hydra, &System::activateHydra},
        Entry{ActivationProgram::Effect, &System::activateEffect},
        Entry{ActivationProgram::Warcry, &System::activateWarcry},
        Entry{ActivationProgram::Curse, &System::activateCurse},
        Entry{ActivationProgram::Corpse, &System::activateCorpse},
        Entry{ActivationProgram::CorpseSearch, &System::activateCorpseSearch},
        Entry{ActivationProgram::GrimWard, &System::activateGrimWard},
        Entry{ActivationProgram::StaticField, &System::activateStaticField},
        Entry{ActivationProgram::Telekinesis, &System::activateTelekinesis},
        Entry{ActivationProgram::Missile, &System::activateMissile},
        Entry{ActivationProgram::Assassin, &System::activateAssassin},
        Entry{ActivationProgram::Druid, &System::activateDruid},
        Entry{ActivationProgram::Unsupported, &System::activateUnsupported},
    };
    static_assert([] {
        std::array<bool, size_t(ActivationProgram::Count)> present{};
        for (const auto &entry : entries) {
            const auto index = size_t(entry.program);
            if (index >= present.size() || present[index] || !entry.handler) return false;
            present[index] = true;
        }
        for (const bool registered : present) if (!registered) return false;
        return true;
    }());
    static constexpr auto handlers = [] {
        std::array<Handler, size_t(ActivationProgram::Count)> result{};
        for (const auto &entry : entries) result[size_t(entry.program)] = entry.handler;
        return result;
    }();
    if(pending.whirlwind) return activateWhirlwind(pending,actor,target);
    const auto program = pending.skill.leap ? ActivationProgram::Leap : pending.weapon ? ActivationProgram::Weapon : activationProgram(pending.skill);
    return (this->*handlers[size_t(program)])(pending, actor, target);
}
StepStatus System::release(TickContext tick,EntityId only) {
    auto &releases_=runtime_->releases;
    bool blocked=false;
    for(auto it=releases_.find(only);it!=releases_.end() && it->first==only;) {
        auto &pending=it->second;if(tick.tick<pending.tick) {++it;continue;}
        auto actor=pending.actor;actor.tick=tick.tick;const auto *p=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
        const bool continuing=pending.nativeCallbackStarted;
        bool valid=p && p->entered && p->actor==actor.actor && p->area==actor.area && (p->persistent.player.hp>0 || continuing) && area && area->generation==actor.areaGeneration;
        const auto used=state_.usedSkill(actor.actor);
        if(valid && !continuing) valid=used && used->skill==pending.skill.sourceId &&
            nativeSkillSlotLevel(*used,p->totals.skillOrder,p->totals.skillRanks,p->totals.chargedSkills,
                p->rules.character->formulas->maximumSkillRank)>0;
        if(valid && !continuing && pending.skill.charge) valid=std::any_of(p->totals.chargedSkills.begin(),p->totals.chargedSkills.end(),[&](const auto &c){return c.item.id==pending.skill.charge->item.id && c.layer==pending.skill.charge->layer && c.charges>0;});
        Vec target=pending.target.position;
        if(valid && !continuing && pending.skill.requiresShield) valid=bool(p->totals.equipment.shield);
        if(valid && !continuing && pending.unit && !pending.whirlwind && !pending.skill.leap && !(pending.skill.weapon && (pending.skill.weapon->dualWield || pending.skill.effect==SkillBehavior::Zeal || (pending.skill.druid && pending.skill.druid->program==DruidProgram::Fury) || (pending.skill.weapon->bow && pending.skill.weapon->bow->strafe) || (pending.skill.weapon->spear && pending.skill.weapon->spear->kind==SpearSkillSpec::Kind::Fend)))) {
            const auto position=unitPosition(actor,{pending.unit,0,pending.unitType},pending.skill.effect);valid=position.has_value();if(valid) target=*position;
        }
        const bool channel=pending.skill.effect==SkillBehavior::Inferno;
        if(!valid) {
            if(pending.assassinKick && pending.assassinKick->occurrence) ports_.combat.discardNativeMelee(actor.actor,*pending.assassinKick->occurrence);
            if(pending.druidMelee && pending.druidMelee->occurrence) ports_.combat.discardNativeMelee(actor.actor,*pending.druidMelee->occurrence);
            if(pending.weaponMelee && pending.weaponMelee->occurrence) ports_.combat.discardNativeMelee(actor.actor,*pending.weaponMelee->occurrence);
            ports_.companions.cancel(actor.actor);it=releases_.erase(it);continue;
        }
        if(pending.charging) {
            if(!ports_.events.hasCapacity(1)) {blocked=true;++it;continue;}
            const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
            const auto victim=participants.find(pending.unit);
            const bool inMelee=victim.id() && playerMeleeReach(area->definition.collision,p->position,victim.position(),victim.size(),pending.weapon->rangeAdder);
            const auto step=inMelee?DomainResult<bool>{DomainStatus::Applied,true}:
                ports_.movement.chargeStep(actor,target,pending.chargeSpeed,0);
            if(!step && isDeferred(step.status)) {blocked=true;++it;continue;}
            if(!step) {state_.casts.at(actor.actor).until=tick.tick;it=releases_.erase(it);continue;}
            if(!*step.value) {pending.tick=tick.tick+1;state_.casts.at(actor.actor).until=tick.tick+1;++it;continue;}
            if(!pending.unit) {
                const auto targets=ports_.effects.nativeAuraCandidateRecipients({actor.actor,actor.area,3,0xa783,false,{},effects::NativeAuraTargetMode::OwnerRange,tick.tick});
                pending.unit=nativeSkillSuccessor(targets,UINT32_MAX);
                if(pending.unit) {const auto found=participants.find(pending.unit);pending.unitType=found.player?0:1;target=found.position();}
                if(!pending.unit) {state_.casts.at(actor.actor).until=tick.tick+1;it=releases_.erase(it);continue;}
            }
            pending.charging=false;pending.animationAnchor=int64_t(tick.tick)-8;
            const auto timeline=rewoundPlayerActionTimeline(pending.weaponFrames,pending.weaponSpeed,8,pending.animationEvents);
            auto prepared=state_.prepareTimeline(actor.actor,tick.tick,timeline);
            auto &cast=state_.casts.at(actor.actor);cast.timeline=prepared.generation;cast.until=tick.tick+uint64_t(timeline.end);
            pending.tick=tick.tick+1;
            pending.weaponHits.clear();for(const auto &event:timeline.events) if(event.event==1 || event.event==2) {pending.weaponHits.push_back(event.tick);break;}
            state_.commitTimeline(std::move(prepared));
            ports_.movement.execute(actor,{MovementAction::Stop,{},false});
            ++it;continue;
        }
        DomainStatus status;
        {
        auto execution=state_.executingRelease(actor.actor);
        if(channel) {
            auto skill=evaluate(*p,pending.skill.sourceId,pending.skill.rank);
            if(pending.skill.charge) {skill.charge=pending.skill.charge;skill.manaCost=skill.startMana=0;}
            const bool consume=pending.pulses%2==0;
            if(consume && p->persistent.player.mana<skill.manaCost) {it=releases_.erase(it);continue;}
            status=ports_.missiles.spawn({actor,skill,pending.collision,target,!consume}).status;
        } else status=activate(pending,actor,target);
        }
        if(pending.cancelled && (!isDeferred(status) || !pending.nativeCallbackStarted)) {
            if(pending.weaponMelee && pending.weaponMelee->occurrence) ports_.combat.discardNativeMelee(actor.actor,*pending.weaponMelee->occurrence);
            if(pending.assassinKick && pending.assassinKick->occurrence) ports_.combat.discardNativeMelee(actor.actor,*pending.assassinKick->occurrence);
            if(pending.druidMelee && pending.druidMelee->occurrence) ports_.combat.discardNativeMelee(actor.actor,*pending.druidMelee->occurrence);
            ports_.companions.cancel(actor.actor);it=releases_.erase(it);continue;
        }
        if(isDeferred(status)) {blocked=true;++it;continue;}
        if(status==DomainStatus::Applied) {
            auto &cast=state_.casts.at(actor.actor);
            if(pending.assassinKick && pending.assassinKick->phase!=AssassinKickExecution::Phase::Complete) {
                cast.until=std::max(cast.until,tick.tick+uint64_t(std::max(1,pending.weaponFrames)));
                ++it;continue;
            }
            if(pending.skill.druid && pending.skill.druid->program==DruidProgram::Arctic) {
                ++pending.pulses;
                if(ports_.effects.unitStates(actor.actor,tick.tick).contains(p->rules.skills->formulas->infernoState)) {
                    pending.tick=tick.tick+1;cast.until=std::max(cast.until,tick.tick+5);++it;continue;
                }
                cast.until=tick.tick+4;
            }
            if(pending.skill.assassin && pending.skill.assassin->program==AssassinProgram::BladeFury) {
                ++pending.pulses;
                if(ports_.effects.unitStates(actor.actor,tick.tick).contains(p->rules.skills->formulas->infernoState)) {
                    pending.tick=tick.tick+1;cast.until=std::max(cast.until,tick.tick+7);++it;continue;
                }
                cast.until=tick.tick+7;
            }
            if(pending.whirlwind) {pending.tick=tick.tick+1;cast.until=tick.tick+1;++it;continue;}
            if(pending.leaping && !pending.landed) {pending.tick=tick.tick+1;cast.until=tick.tick+1;++it;continue;}
            if(pending.leaping && pending.landed && pending.tick>tick.tick) {++it;continue;}
            if(pending.skill.delayFrames>0) cast.cooldownUntil=tick.tick+uint64_t(pending.skill.delayFrames);
            if(channel) {if(pending.skill.charge) pending.skill.charge->item=p->persistent.inventory.items.at(pending.skill.charge->item.id).handle();++pending.pulses;pending.tick=tick.tick+1;cast.until=tick.tick+1;++it;continue;}
            if(pending.weapon) {
                pending.manaPaid=true;
                if(pending.rewindRemaining>0 && --pending.rewindRemaining>0) {
                    const auto elapsed=int((int64_t(tick.tick)-pending.animationAnchor)*(100-pending.weaponRollback)/100);
                    const auto timeline=rewoundPlayerActionTimeline(pending.weaponFrames,pending.weaponSpeed,elapsed,pending.animationEvents);
                    auto prepared=state_.prepareTimeline(actor.actor,tick.tick,timeline);
                    pending.animationAnchor=int64_t(tick.tick)-elapsed;
                    cast.timeline=prepared.generation;cast.until=tick.tick+uint64_t(timeline.end);
                    pending.tick=tick.tick+1;
                    ++pending.nextWeaponHit;
                    state_.commitTimeline(std::move(prepared));
                    ++it;continue;
                }
                if(++pending.nextWeaponHit<pending.weaponHits.size()) {
                    pending.tick=pending.eventDriven?cast.started+uint64_t(pending.weaponHits[pending.nextWeaponHit]):
                        tick.tick+uint64_t(std::max(1,pending.weaponHits[pending.nextWeaponHit]-pending.weaponHits[pending.nextWeaponHit-1]));
                    ++it;continue;
                }
            }
        }
        ports_.companions.cancel(actor.actor);
        if(pending.weaponMelee && pending.weaponMelee->occurrence) ports_.combat.discardNativeMelee(actor.actor,*pending.weaponMelee->occurrence);
        if(pending.assassinKick && pending.assassinKick->occurrence) ports_.combat.discardNativeMelee(actor.actor,*pending.assassinKick->occurrence);
        it=releases_.erase(it);
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
DomainResult<> System::objectKick(const ActorContext &actor, UnitTarget target) {
    const auto *player=ports_.players.find(actor.player);
    if(!player || !player->rules.skills || target.type!=2) return {DomainStatus::InvalidActor,{}};
    // PlrModes may refuse a mode change while another action is in progress;
    // ObjMode still operates the barrel in that case.
    if(busy(actor.actor,actor.tick)) return {DomainStatus::Applied,std::monostate{}};
    for(const auto &[id,definition]:player->rules.skills->definitions)
        if(definition.spec.effect==SkillBehavior::Kick) {
            Request request{};request.action=Action::Cast;request.target=target;request.stationary=true;
            return cast(actor,request,id);
        }
    return {DomainStatus::Unavailable,{}};
}
DomainResult<> System::itemTrigger(const ActorContext &actor,SkillCastSpec skill,MissileCollisionRule collision,EntityId target,Vec position,bool dead,bool itemTargetDo,std::shared_ptr<actions::SkillRelease> *continuation) {
    const auto *p=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    const bool continuing=continuation && *continuation && (*continuation)->nativeCallbackStarted;
    if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || !area || area->generation!=actor.areaGeneration || (p->persistent.player.hp<=0 && !dead && !continuing)) return {DomainStatus::InvalidActor,{}};
    if(itemTargetDo || skill.weapon || skill.effect==SkillBehavior::ItemSkill || skill.effect==SkillBehavior::Kick || skill.effect==SkillBehavior::Unsummon) return {DomainStatus::NotImplemented,{}};
    skill.manaCost=skill.startMana=0;skill.delayFrames=0;skill.charge.reset();
    if(!dead && !skill.druid && !skill.warcry && skill.appliedEffect && !(skill.assassin && skill.assassin->program==AssassinProgram::BladeShield)) {
        if(skill.effect==SkillBehavior::Enchant && target) {
            for(const auto &[id,recipient]:ports_.players.all()) if(recipient.actor==target && recipient.entered && recipient.area==actor.area) return ports_.effects.skill(actor,skill,id);
            return ports_.effects.skillUnit(actor,skill,target);
        }
        return ports_.effects.skill(actor,skill);
    }
    if(dead && (skill.appliedEffect || skill.summon || skill.effect==SkillBehavior::Teleport || skill.effect==SkillBehavior::Hydra)) return {DomainStatus::Unavailable,{}};
    const auto program = activationProgram(skill);
    switch (program) {
    case ActivationProgram::Druid:
    case ActivationProgram::Assassin:
    case ActivationProgram::Warcry:
    case ActivationProgram::StaticField:
    case ActivationProgram::Telekinesis:
    case ActivationProgram::Corpse: {
        std::shared_ptr<Release> local;auto &pending=continuation?*continuation:local;
        if(!pending) pending=std::make_shared<Release>(Release{actor,skill,collision,{actor.area,actor.areaGeneration,position},target,actor.tick,uint8_t(ports_.players.findActor(target)?0:1)});
        const auto result=activate(*pending,actor,position);
        if(result!=DomainStatus::Capacity) pending.reset();
        return {result,result==DomainStatus::Applied?std::optional{std::monostate{}}:std::nullopt};
    }
    case ActivationProgram::AmazonSummon: return skill.summon && skill.summon->amazon?ports_.companions.amazon(actor,skill,position):ports_.companions.necro(actor,skill,position,target);
    case ActivationProgram::Curse: return ports_.effects.curse(actor,skill,position,target);
    case ActivationProgram::AmazonMagic: return ports_.effects.amazonMagic(actor,skill);
    case ActivationProgram::Teleport: return ports_.travel.teleport(actor,{actor.area,actor.areaGeneration,position},0);
    case ActivationProgram::Hydra: return ports_.companions.hydra(actor,skill,position);
    case ActivationProgram::Missile: {
        missiles::Spawn spawn{actor,std::move(skill),collision,position,true};spawn.deathTrigger=dead;
        const auto result=ports_.missiles.spawn(spawn);return {result.status,result?std::optional{std::monostate{}}:std::nullopt};
    }
    default: return {DomainStatus::NotImplemented,{}};
    }
}

}
