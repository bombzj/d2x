#include "system.hpp"
#include "server/systems/transactions/system.hpp"
#include "evaluation.hpp"
#include "server/player_store.hpp"
#include "server/movement.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/companions/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/inventory/system.hpp"
#include "gameplay/skills/behavior.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::skills {
namespace {
bool inside(const Grid &g, Vec p) {return std::isfinite(p.x) && std::isfinite(p.y) && p.x>=0 && p.y>=0 && p.x<g.width && p.y<g.height;}
bool within(Vec from,Vec to,int r) {const int x=int(to.x)-int(from.x),y=int(to.y)-int(from.y);return x*x+y*y<=r*r;}
}
std::optional<Vec> System::unitPosition(const ActorContext &actor,UnitTarget unit,SkillBehavior skill) const {
    if(unit.type==0 && skill==SkillBehavior::Enchant) {
        for(const auto &[id,p]:ports_.players.all()) {(void)id;if(p.actor==unit.id && p.entered && p.area==actor.area && p.persistent.player.hp>0) return p.position;}
    } else if(unit.type==1) {
        const auto *m=ports_.monsters.find(unit.id);
        if(m && m->area==actor.area && m->life>0 && (skill==SkillBehavior::Enchant ? bool(m->owner) : !m->owner)) return m->position;
        if(skill==SkillBehavior::Enchant) for(const auto &npc:ports_.areas.at(actor.area).definition.npcs) if(npc.id==unit.id) return npc.position;
    } else if(skill==SkillBehavior::Telekinesis) {
        if(unit.type==2) {const auto found=ports_.objects.read().objects.find(unit.id);if(found!=ports_.objects.read().objects.end() && found->second.area==actor.area) return found->second.position;return ports_.travel.portalPosition(actor,unit.id);}
        if(unit.type==4) return ports_.inventory.groundPosition(unit.id,actor.area);
    }
    return {};
}
DomainResult<> System::cast(const ActorContext &actor,const Request &request,int selected) {
    const auto &p=*ports_.players.find(actor.player);const auto &area=ports_.areas.at(actor.area);
    if(!p.rules.skills) return {DomainStatus::Unavailable,{}};
    const auto &rules=*p.rules.skills;const auto found=rules.definitions.find(selected);if(found==rules.definitions.end()) return {};
    const auto &definition=found->second;
    if(!request.target) return {DomainStatus::InvalidRequest,{}};
    PointTarget target{actor.area,actor.areaGeneration,{}};EntityId unit;uint8_t type=1;
    if(const auto *point=std::get_if<PointTarget>(&*request.target)) {
        if(point->area!=actor.area || point->generation!=actor.areaGeneration) return {DomainStatus::Stale,{}};
        target=*point;
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
    const auto rank=p.totals.skillRanks.find(selected);if(rank==p.totals.skillRanks.end() || rank->second<=0 || rank->second>255) return {DomainStatus::InvalidRequest,{}};
    if(area.definition.town && !definition.allowedInTown) return {DomainStatus::Unavailable,{}};
    const auto animation=rules.animations.find(p.totals.equipment.animationClass);if(animation==rules.animations.end()) return {DomainStatus::Unavailable,{}};
    auto skill=evaluate(p,selected,rank->second);
    if(!std::isfinite(skill.manaCost) || skill.manaCost<0 || !std::isfinite(skill.startMana) || skill.startMana<0 ||
        p.persistent.player.mana<std::max(skill.manaCost,skill.startMana)) return {DomainStatus::Unavailable,{}};
    if(skill.effect==SkillBehavior::Teleport && (!area.definition.teleportAllowed || !area.definition.collision.walkable(target.position,playerMovement))) return {DomainStatus::Unavailable,{}};
    if(skill.effect==SkillBehavior::Telekinesis && (!unit || !within(p.position,target.position,skill.telekinesisRange))) return {DomainStatus::InvalidRequest,{}};
    if((skill.effect==SkillBehavior::Blizzard || skill.effect==SkillBehavior::Hydra || skill.effect==SkillBehavior::FireWall || skill.effect==SkillBehavior::Meteor) &&
        !area.definition.collision.missileSegment(target.position,target.position,{5,1})) return {DomainStatus::Unavailable,{}};
    const bool inferno=skill.effect==SkillBehavior::Inferno;
    const auto timing=sorceressCastTiming(animation->second,p.totals.character.combat.fasterCast,bool(skill.arc),inferno);
    if(!ports_.events.hasCapacity(inferno?2:1,inferno?2:1)) return {DomainStatus::Capacity,{}};
    Release release{actor,std::move(skill),definition.collision,target,unit,actor.tick+uint64_t(timing.impact),type};
    const auto previous=state_.casts.find(actor.actor);const auto saved=previous==state_.casts.end()?std::optional<Cast>{}:previous->second;
    releases_.emplace(actor.actor,std::move(release));
    auto rollback=[&] {releases_.erase(actor.actor);if(saved) state_.casts.at(actor.actor)=*saved;else state_.casts.erase(actor.actor);};
    try {
        state_.casts[actor.actor]={actor.actor,uint16_t(selected),actor.tick,actor.sequence,actor.tick+uint64_t(timing.duration),actor.area,unit};
        if(saved) state_.casts.at(actor.actor).cooldownUntil=saved->cooldownUntil;
        if(inferno) {
            const auto debit=ports_.transactions.release(actor,p.characterRevision,releases_.at(actor.actor).skill.manaCost);
            if(!debit) {rollback();return debit;}releases_.at(actor.actor).manaPaid=true;
        }
        const auto output=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},
            {AttackFact{actor.actor,unit,0,type,actor.area,p.position,target.position,actor.sequence,uint16_t(selected),uint8_t(rank->second)}}});
        if(!output) {rollback();return {output.status,{}};}
    } catch(...) {rollback();throw;}
    ports_.movement.execute(actor,{MovementAction::Stop,{},false});return {DomainStatus::Applied,std::monostate{}};
}
DomainStatus System::activate(Release &pending,const ActorContext &actor,Vec target) {
    const auto &p=*ports_.players.find(actor.player);const auto &skill=pending.skill;
    if(skill.effect==SkillBehavior::Teleport) return ports_.travel.teleport(actor,{actor.area,actor.areaGeneration,target},skill.manaCost).status;
    if(skill.effect==SkillBehavior::Hydra) return ports_.companions.hydra(actor,skill,target).status;
    if(skill.appliedEffect) {
        if(skill.effect==SkillBehavior::Enchant && pending.unit && pending.unitType==1) return ports_.effects.skillUnit(actor,skill,pending.unit).status;
        std::optional<PlayerId> recipient;
        if(skill.effect==SkillBehavior::Enchant && pending.unit) for(const auto &[id,other]:ports_.players.all()) if(other.actor==pending.unit) {recipient=id;break;}
        return ports_.effects.skill(actor,skill,recipient).status;
    }
    if(skill.effect==SkillBehavior::StaticField) {
        std::vector<EntityId> targets;
        for(const auto &[id,m]:ports_.monsters.read().actors) if(!m.owner && m.life>0 && m.area==actor.area &&
            within(p.position,m.position,int(skill.staticRadius))) targets.push_back(id);
        return ports_.missiles.direct({actor,skill,{},p.position},std::move(targets)).status;
    }
    if(skill.effect==SkillBehavior::Telekinesis) {
        if(!within(p.position,target,skill.telekinesisRange)) return DomainStatus::InvalidRequest;
        if(pending.unitType==1) return ports_.missiles.direct({actor,skill,{},target},{pending.unit}).status;
        if(pending.unitType==4) {
            const auto result=ports_.inventory.telekinesis(actor,pending.unit,skill);
            return result.status;
        }
        if(pending.unitType==2) {
            if(!pending.manaPaid) {
                const auto debit=ports_.transactions.release(actor,p.characterRevision,skill.manaCost);if(!debit) return debit.status;pending.manaPaid=true;
            }
            if(ports_.travel.portalPosition(actor,pending.unit)) return ports_.travel.useSpecial(actor,{travel::Kind::Portal,{pending.unit,0},{}},skill.telekinesisRange).status;
            return ports_.objects.execute(actor,{{pending.unit,0}},skill.telekinesisRange).status;
        }
        return DomainStatus::InvalidRequest;
    }
    return ports_.missiles.spawn({actor,skill,pending.collision,target}).status;
}
StepStatus System::release(TickContext tick) {
    bool blocked=false;
    for(auto it=releases_.begin();it!=releases_.end();) {
        auto &pending=it->second;if(tick.tick<pending.tick) {++it;continue;}
        auto actor=pending.actor;actor.tick=tick.tick;const auto *p=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
        bool valid=p && p->entered && p->actor==actor.actor && p->area==actor.area && p->persistent.player.hp>0 && area && area->generation==actor.areaGeneration;
        Vec target=pending.target.position;
        if(valid && pending.unit) {
            const auto position=unitPosition(actor,{pending.unit,0,pending.unitType},pending.skill.effect);valid=position.has_value();if(valid) target=*position;
        }
        const bool channel=pending.skill.effect==SkillBehavior::Inferno;
        if(!valid) {it=releases_.erase(it);continue;}
        DomainStatus status;
        if(channel) {
            auto skill=evaluate(*p,pending.skill.sourceId,pending.skill.rank);
            const bool consume=pending.pulses%2==0;
            if(consume && p->persistent.player.mana<skill.manaCost) {it=releases_.erase(it);continue;}
            status=ports_.missiles.spawn({actor,skill,pending.collision,target,!consume}).status;
        } else status=activate(pending,actor,target);
        if(status==DomainStatus::Capacity) {blocked=true;++it;continue;}
        if(status==DomainStatus::Applied) {
            auto &cast=state_.casts.at(actor.actor);
            if(pending.skill.delayFrames>0) cast.cooldownUntil=tick.tick+uint64_t(pending.skill.delayFrames);
            if(channel) {++pending.pulses;pending.tick=tick.tick+1;cast.until=tick.tick+1;++it;continue;}
        }
        it=releases_.erase(it);
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
