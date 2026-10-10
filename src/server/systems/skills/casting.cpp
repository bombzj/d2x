#include "system.hpp"
#include "runtime.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "server/systems/transactions/system.hpp"
#include "evaluation.hpp"
#include "server/player_store.hpp"
#include "server/movement.hpp"
#include "server/systems/monsters/system.hpp"
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
    if(unit.type==0 && (skill==SkillBehavior::Enchant || skill==SkillBehavior::HolyBolt)) {
        for(const auto &[id,p]:ports_.players.all()) {(void)id;if(p.actor==unit.id && p.entered && p.area==actor.area && p.persistent.player.hp>0) return p.position;}
    } else if(unit.type==1) {
        const auto *m=ports_.monsters.find(unit.id);
        const bool enemy=m && combat::Participants{ports_.players,ports_.monsters}.canHarm(actor.actor,m->id);
        if(m && m->area==actor.area && m->life>0 && (skill==SkillBehavior::HolyBolt ? enemy || m->owner==actor.player : skill==SkillBehavior::Enchant ? bool(m->owner) : skill==SkillBehavior::Unsummon ? m->owner==actor.player : enemy)) return m->position;
        if(skill==SkillBehavior::Enchant) for(const auto &npc:ports_.areas.at(actor.area).definition.npcs) if(npc.id==unit.id) return npc.position;
    } else if(skill==SkillBehavior::Kick && unit.type==2) {
        const auto found=ports_.objects.read().objects.find(unit.id);
        if(found!=ports_.objects.read().objects.end() && found->second.area==actor.area && found->second.rule.operation==5) return found->second.position;
    } else if(skill==SkillBehavior::Telekinesis) {
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
    if(definition.spec.weapon) return weaponCast(actor,request,selected);
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
    if(auto channel=releases_.find(actor.actor);channel!=releases_.end() && channel->second.skill.effect==SkillBehavior::Inferno && selected==channel->second.skill.sourceId) {
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
        const auto cast=state_.casts.find(actor.actor);if(cast!=state_.casts.end() && cast->second.cooldownUntil>actor.tick) return {DomainStatus::Conflict,{}};
    }
    const auto rank=p.totals.skillRanks.find(selected);
    const auto owner=p.persistent.player.selectedSkillOwners.at(p.persistent.player.weaponSet*2+(request.right?1:0));
    const auto charged=std::find_if(p.totals.chargedSkills.begin(),p.totals.chargedSkills.end(),[&](const auto &c){return c.item.id.value==owner && c.skill==selected && c.charges>0;});
    if(owner!=UINT32_MAX && charged==p.totals.chargedSkills.end()) return {DomainStatus::Unavailable,{}};
    const int effectiveRank=owner!=UINT32_MAX?charged->rank:p.rules.character->innateSkills.contains(selected)?1:(rank==p.totals.skillRanks.end()?0:rank->second);
    if(effectiveRank<=0 || effectiveRank>255) return {DomainStatus::InvalidRequest,{}};
    // ObjMode calls PLAYERMODE_Change directly, including barrels in town;
    // it does not apply player-selected spell town eligibility to hidden KK.
    const bool objectKick=definition.spec.effect==SkillBehavior::Kick && type==2;
    if(area.definition.town && !definition.allowedInTown && !objectKick) return {DomainStatus::Unavailable,{}};
    auto skill=evaluate(p,selected,effectiveRank);
    if(skill.requiresShield && !p.totals.equipment.shield) return {DomainStatus::Unavailable,{}};
    if(skill.effect==SkillBehavior::FistOfTheHeavens && !unit) return {DomainStatus::InvalidRequest,{}};
    if (activationProgram(skill) == ActivationProgram::Unsupported) return {DomainStatus::NotImplemented,{}};
    if(owner!=UINT32_MAX) {skill.charge=SkillCharge{charged->item,charged->layer};skill.manaCost=skill.startMana=0;}
    if(!std::isfinite(skill.manaCost) || skill.manaCost<0 || !std::isfinite(skill.startMana) || skill.startMana<0 ||
        p.persistent.player.mana<std::max(skill.manaCost,skill.startMana)) return {DomainStatus::Unavailable,{}};
    if(skill.effect==SkillBehavior::Teleport && (!area.definition.teleportAllowed || !area.definition.collision.walkable(target.position,playerMovement))) return {DomainStatus::Unavailable,{}};
    if(skill.effect==SkillBehavior::Telekinesis && (!unit || !within(p.position,target.position,skill.telekinesisRange))) return {DomainStatus::InvalidRequest,{}};
    if((skill.effect==SkillBehavior::Blizzard || skill.effect==SkillBehavior::Hydra || skill.effect==SkillBehavior::FireWall || skill.effect==SkillBehavior::Meteor) &&
        !area.definition.collision.missileSegment(target.position,target.position,{5,1})) return {DomainStatus::Unavailable,{}};
    const bool inferno=skill.effect==SkillBehavior::Inferno;
    CastTiming timing;
    if(skill.effect!=SkillBehavior::Kick) {
        const auto animation=rules.animations.find(p.totals.equipment.animationClass);
        if(animation==rules.animations.end()) return {DomainStatus::Unavailable,{}};
        timing=sorceressCastTiming(animation->second,p.totals.character.combat.fasterCast,bool(skill.arc),inferno);
    }
    if(skill.effect==SkillBehavior::Kick) {
        const auto *victim=ports_.monsters.find(unit);
        if(!unit || (type!=2 && (type!=1 || !victim))) return {DomainStatus::InvalidRequest,{}};
        // Object operation has already checked the original interaction geometry.
        if(type!=2 && (meleeDistance(p.position,2,target.position,victim->rule.size)>1 || !area.definition.collision.segment(p.position,target.position))) {
            if(request.stationary) return {DomainStatus::Unavailable,{}};
            ports_.movement.execute(actor,{MovementAction::Move,target.position,false});return {DomainStatus::Conflict,{}};
        }
        const auto kick=rules.weaponAnimations.find("kk"+p.totals.equipment.animationClass);
        if(kick==rules.weaponAnimations.end()) return {DomainStatus::Unavailable,{}};
        const auto &a=kick->second;
        WeaponAttackTiming clock{"kk",a.frames,effectiveAttackSpeed(a.speed,0,0,p.totals.character.combat.attackRate),a.actionFrame,0};
        timing={clock.durationTicks(),clock.actionTick(),clock.speed};
    }
    if(skill.effect==SkillBehavior::Unsummon && (!unit || !ports_.companions.canDismiss(actor,unit))) return {DomainStatus::InvalidRequest,{}};
    if(!ports_.events.hasCapacity(inferno?2:1,inferno?2:1)) return {DomainStatus::Capacity,{}};
    Release release{actor,std::move(skill),definition.collision,target,unit,actor.tick+uint64_t(timing.impact),type};
    const auto previous=state_.casts.find(actor.actor);const auto saved=previous==state_.casts.end()?std::optional<Cast>{}:previous->second;
    releases_.emplace(actor.actor,std::move(release));
    auto rollback=[&] {releases_.erase(actor.actor);if(saved) state_.casts.at(actor.actor)=*saved;else state_.casts.erase(actor.actor);};
    try {
        state_.casts[actor.actor]={actor.actor,uint16_t(selected),actor.tick,actor.sequence,actor.tick+uint64_t(timing.duration),actor.area,unit};
        if(saved) state_.casts.at(actor.actor).cooldownUntil=saved->cooldownUntil;
        if(inferno) {
            const auto &castSkill=releases_.at(actor.actor).skill;
            const auto debit=ports_.transactions.release(actor,p.characterRevision,castSkill.manaCost,{},castSkill.charge);
            if(!debit) {rollback();return debit;}if(releases_.at(actor.actor).skill.charge) releases_.at(actor.actor).skill.charge->item=p.persistent.inventory.items.at(castSkill.charge->item.id).handle();releases_.at(actor.actor).manaPaid=true;
        }
        const auto output=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},
            {AttackFact{actor.actor,unit,0,type,actor.area,p.position,target.position,actor.sequence,uint16_t(selected),uint8_t(effectiveRank)}}});
        if(!output) {rollback();return {output.status,{}};}
    } catch(...) {rollback();throw;}
    ports_.movement.execute(actor,{MovementAction::Stop,{},false});return {DomainStatus::Applied,std::monostate{}};
}
System::ActivationProgram System::activationProgram(const SkillCastSpec &skill) {
    switch (skill.effect) {
    case SkillBehavior::ItemSkill: return ActivationProgram::Item;
    case SkillBehavior::Unsummon: return ActivationProgram::Unsummon;
    case SkillBehavior::Kick: return ActivationProgram::Kick;
    default: break;
    }
    if (skill.summon && skill.summon->amazon) return ActivationProgram::AmazonSummon;
    if (skill.amazonMagic) return ActivationProgram::AmazonMagic;
    switch (skill.effect) {
    case SkillBehavior::Teleport: return ActivationProgram::Teleport;
    case SkillBehavior::Hydra: return ActivationProgram::Hydra;
    default: break;
    }
    if (skill.appliedEffect) return ActivationProgram::Effect;
    switch (skill.effect) {
    case SkillBehavior::StaticField: return ActivationProgram::StaticField;
    case SkillBehavior::Telekinesis: return ActivationProgram::Telekinesis;
    case SkillBehavior::WeaponProjectile:
    case SkillBehavior::HolyBolt:
    case SkillBehavior::BlessedHammer: case SkillBehavior::FistOfTheHeavens:
    case SkillBehavior::FireBolt: case SkillBehavior::Fireball:
    case SkillBehavior::IceBolt: case SkillBehavior::IceBlast:
    case SkillBehavior::Inferno: case SkillBehavior::ChillingArmor:
    case SkillBehavior::ChargedBolt: case SkillBehavior::FrostNova:
    case SkillBehavior::Nova: case SkillBehavior::GlacialSpike:
    case SkillBehavior::FrozenOrb: case SkillBehavior::Blizzard:
    case SkillBehavior::Lightning: case SkillBehavior::ChainLightning:
    case SkillBehavior::FireWall: case SkillBehavior::Blaze:
    case SkillBehavior::Meteor: return ActivationProgram::Missile;
    default: return ActivationProgram::Unsupported;
    }
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
        Entry{ActivationProgram::Hydra, &System::activateHydra},
        Entry{ActivationProgram::Effect, &System::activateEffect},
        Entry{ActivationProgram::StaticField, &System::activateStaticField},
        Entry{ActivationProgram::Telekinesis, &System::activateTelekinesis},
        Entry{ActivationProgram::Missile, &System::activateMissile},
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
    const auto program = pending.weapon ? ActivationProgram::Weapon : activationProgram(pending.skill);
    return (this->*handlers[size_t(program)])(pending, actor, target);
}
StepStatus System::release(TickContext tick) {
    auto &releases_=runtime_->releases;
    bool blocked=false;
    for(auto it=releases_.begin();it!=releases_.end();) {
        auto &pending=it->second;if(tick.tick<pending.tick) {++it;continue;}
        auto actor=pending.actor;actor.tick=tick.tick;const auto *p=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
        bool valid=p && p->entered && p->actor==actor.actor && p->area==actor.area && p->persistent.player.hp>0 && area && area->generation==actor.areaGeneration;
        if(valid && pending.skill.charge) valid=std::any_of(p->totals.chargedSkills.begin(),p->totals.chargedSkills.end(),[&](const auto &c){return c.item.id==pending.skill.charge->item.id && c.layer==pending.skill.charge->layer && c.charges>0;});
        if(valid && !pending.skill.charge && pending.skill.sourceId>=0 && !p->rules.character->innateSkills.contains(pending.skill.sourceId)) valid=p->totals.skillRanks.contains(pending.skill.sourceId);
        Vec target=pending.target.position;
        if(valid && pending.skill.requiresShield) valid=bool(p->totals.equipment.shield);
        if(valid && pending.unit && !(pending.skill.weapon && (pending.skill.effect==SkillBehavior::Zeal || (pending.skill.weapon->bow && pending.skill.weapon->bow->strafe) || (pending.skill.weapon->spear && pending.skill.weapon->spear->kind==SpearSkillSpec::Kind::Fend)))) {
            const auto position=unitPosition(actor,{pending.unit,0,pending.unitType},pending.skill.effect);valid=position.has_value();if(valid) target=*position;
        }
        const bool channel=pending.skill.effect==SkillBehavior::Inferno;
        if(!valid) {ports_.companions.cancel(actor.actor);it=releases_.erase(it);continue;}
        if(pending.charging) {
            if(!ports_.events.hasCapacity(1)) {blocked=true;++it;continue;}
            const auto *victim=ports_.monsters.find(pending.unit);
            const int stop=victim?pending.weapon->rangeAdder+1+(victim->rule.size+2)/2:0;
            const auto step=ports_.movement.chargeStep(actor,target,pending.chargeSpeed,stop);
            if(!step || tick.tick>state_.casts.at(actor.actor).started+250) {state_.casts.at(actor.actor).until=tick.tick;it=releases_.erase(it);continue;}
            if(!*step.value) {pending.tick=tick.tick+1;++it;continue;}
            if(!pending.unit) {
                for(const auto &[id,m]:ports_.monsters.read().actors) if(m.enemyTarget() && m.life>0 && m.area==actor.area && meleeDistance(p->position,2,m.position,m.rule.size)<=pending.weapon->rangeAdder+1) {pending.unit=id;target=m.position;break;}
                if(!pending.unit) {state_.casts.at(actor.actor).until=tick.tick;it=releases_.erase(it);continue;}
            }
            pending.charging=false;pending.tick=tick.tick+uint64_t(std::max(1,(3*256+pending.weaponSpeed-1)/pending.weaponSpeed));
            ports_.movement.execute(actor,{MovementAction::Stop,{},false});
            ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},actor.area},
                {AttackFact{actor.actor,pending.unit,0,1,actor.area,p->position,target,tick.tick,uint16_t(pending.skill.sourceId),uint8_t(pending.skill.rank),true}}});
            pending.weaponHits={int(pending.tick-tick.tick)};
            state_.casts.at(actor.actor).until=tick.tick+uint64_t(std::max(1,(7*256+pending.weaponSpeed-1)/pending.weaponSpeed));
            ++it;continue;
        }
        DomainStatus status;
        if(channel) {
            auto skill=evaluate(*p,pending.skill.sourceId,pending.skill.rank);
            if(pending.skill.charge) {skill.charge=pending.skill.charge;skill.manaCost=skill.startMana=0;}
            const bool consume=pending.pulses%2==0;
            if(consume && p->persistent.player.mana<skill.manaCost) {it=releases_.erase(it);continue;}
            status=ports_.missiles.spawn({actor,skill,pending.collision,target,!consume}).status;
        } else status=activate(pending,actor,target);
        if(status==DomainStatus::Capacity) {blocked=true;++it;continue;}
        if(status==DomainStatus::Applied) {
            auto &cast=state_.casts.at(actor.actor);
            if(pending.skill.delayFrames>0) cast.cooldownUntil=tick.tick+uint64_t(pending.skill.delayFrames);
            if(channel) {if(pending.skill.charge) pending.skill.charge->item=p->persistent.inventory.items.at(pending.skill.charge->item.id).handle();++pending.pulses;pending.tick=tick.tick+1;cast.until=tick.tick+1;++it;continue;}
            if(pending.weapon) {
                pending.manaPaid=true;
                if(++pending.nextWeaponHit<pending.weaponHits.size()) {
                    pending.tick=tick.tick+uint64_t(std::max(1,pending.weaponHits[pending.nextWeaponHit]-pending.weaponHits[pending.nextWeaponHit-1]));
                    ++it;continue;
                }
            }
        }
        ports_.companions.cancel(actor.actor);
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
DomainResult<> System::itemTrigger(const ActorContext &actor,SkillCastSpec skill,MissileCollisionRule collision,EntityId target,Vec position,bool dead,bool itemTargetDo) {
    const auto *p=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || !area || area->generation!=actor.areaGeneration || (p->persistent.player.hp<=0 && !dead)) return {DomainStatus::InvalidActor,{}};
    if(itemTargetDo || skill.weapon || skill.effect==SkillBehavior::ItemSkill || skill.effect==SkillBehavior::Kick || skill.effect==SkillBehavior::Unsummon) return {DomainStatus::NotImplemented,{}};
    skill.manaCost=skill.startMana=0;skill.delayFrames=0;skill.charge.reset();
    if(!dead && skill.appliedEffect) {
        if(skill.effect==SkillBehavior::Enchant && target) {
            for(const auto &[id,recipient]:ports_.players.all()) if(recipient.actor==target && recipient.entered && recipient.area==actor.area) return ports_.effects.skill(actor,skill,id);
            return ports_.effects.skillUnit(actor,skill,target);
        }
        return ports_.effects.skill(actor,skill);
    }
    if(dead && (skill.appliedEffect || skill.summon || skill.effect==SkillBehavior::Teleport || skill.effect==SkillBehavior::Hydra)) return {DomainStatus::Unavailable,{}};
    const auto program = activationProgram(skill);
    switch (program) {
    case ActivationProgram::AmazonSummon: return ports_.companions.amazon(actor,skill,position);
    case ActivationProgram::AmazonMagic: return ports_.effects.amazonMagic(actor,skill);
    case ActivationProgram::Teleport: return ports_.travel.teleport(actor,{actor.area,actor.areaGeneration,position},0);
    case ActivationProgram::Hydra: return ports_.companions.hydra(actor,skill,position);
    case ActivationProgram::StaticField:
    case ActivationProgram::Telekinesis:
    case ActivationProgram::Missile: {
        missiles::Spawn spawn{actor,std::move(skill),collision,position,true};spawn.deathTrigger=dead;
        if(program==ActivationProgram::StaticField) return ports_.missiles.direct(spawn,staticFieldTargets(actor,spawn.skill));
        if(program==ActivationProgram::Telekinesis) return target?ports_.missiles.direct(spawn,{target}):DomainResult<>{DomainStatus::Unavailable,{}};
        const auto result=ports_.missiles.spawn(spawn);return {result.status,result?std::optional{std::monostate{}}:std::nullopt};
    }
    default: return {DomainStatus::NotImplemented,{}};
    }
}

}
