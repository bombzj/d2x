#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::missiles {
namespace {
Vec cell(Vec p) { return {std::floor(p.x)+.5f,std::floor(p.y)+.5f}; }
bool radius(Vec a, Vec b, int r) { const auto d=Vec{std::floor(a.x)-std::floor(b.x),std::floor(a.y)-std::floor(b.y)}; return d.x*d.x+d.y*d.y<=float(r*r); }
DamageType element(SkillBehavior s) {
    switch(s) {
    case SkillBehavior::IceBolt: case SkillBehavior::IceBlast: case SkillBehavior::FrostNova: case SkillBehavior::GlacialSpike:
    case SkillBehavior::FrozenArmor: case SkillBehavior::FrozenOrb: case SkillBehavior::Blizzard: case SkillBehavior::ShiverArmor: case SkillBehavior::ChillingArmor: return DamageType::Cold;
    case SkillBehavior::ChargedBolt: case SkillBehavior::Nova: case SkillBehavior::Lightning: case SkillBehavior::ChainLightning:
    case SkillBehavior::ThunderStorm: case SkillBehavior::Telekinesis: case SkillBehavior::StaticField: return DamageType::Lightning;
    default: return DamageType::Fire;
    }
}
}
combat::SpellImpact System::impact(Missile &m, std::vector<EntityId> targets, std::optional<int64_t> damage) const {
    const auto &owner=*ports_.players.find(m.player); const auto &rules=*owner.rules.skills;
    combat::SpellImpact result{m.id,m.owner,m.area,element(m.skill.effect),damage.value_or(m.damage),std::move(targets)};
    result.occurrence=uint64_t(m.ageFrames);
    if(m.skill.weapon && m.skill.weapon->spear && (m.skill.weapon->spear->kind==SpearSkillSpec::Kind::Charged ||
        m.skill.weapon->spear->kind==SpearSkillSpec::Kind::Strike || m.skill.weapon->spear->kind==SpearSkillSpec::Kind::Fury)) result.type=DamageType::Lightning;
    if(m.program==Program::AreaImpact && m.skill.missileImpact && m.skill.missileImpact->areaMissile) {
        result.type=m.skill.missileImpact->areaMissile->element;
        result.freeze=result.type==DamageType::Cold;
    }
    if(m.program==Program::PoisonCloud) {result.type=DamageType::Poison;result.poisonFrames=uint64_t(std::max(0,int(std::lround(m.skill.poisonDuration*25.f))));}
    result.coldFrames=uint64_t(std::max(0,int(std::lround(m.skill.coldDuration*25.f))));
    result.freeze=result.freeze || m.skill.effect==SkillBehavior::IceBlast || m.skill.effect==SkillBehavior::GlacialSpike;
    if (m.skill.freezingArea) result.coldFrames=uint64_t(m.skill.freezingArea->freezeFrames);
    result.coldPierce=skills::coldPierce(owner); result.coldDivisor=rules.coldDivisor; result.freezeDivisor=rules.freezeDivisor;
    result.nextDelay=uint64_t(m.skill.arc ? m.skill.arc->nextDelay : std::max(0,int(std::lround(m.skill.missileNextDelay*25.f))));
    return result;
}
System::Advance System::advance(const Missile &original) const {
    if(original.weapon) return advanceWeapon(original);
    Advance plan{original,{},{},false}; auto &m=plan.next;
    const auto &owner=*ports_.players.find(m.player); const auto &area=ports_.areas.at(m.area);
    Spawn source{{m.player,m.owner,m.area,m.generation,0,m.created},m.skill,m.collision,{},true,m.emitter,m.emitterType,{}};
    const auto enemy = [&](const auto &target) { return target.life>0 && !target.owner && target.area==m.area; };
    const auto child = [&](Vec at, Vec direction, int id, int frames, float speed, Program program, bool fresh=false) -> Missile & {
        if(fresh) source.skill=skills::evaluate(owner,m.skill.sourceId,m.skill.rank);
        auto c=make(source,at,direction,id,frames,speed,program,m.random);
        c.created=m.created+uint64_t(m.ageFrames)+1; c.expires=c.created+uint64_t(frames);
        plan.children.push_back(std::move(c)); return plan.children.back();
    };
    const auto areaHit = [&](Vec at,int range,int64_t damage) {
        std::vector<EntityId> targets;
        for(const auto &[id,t]:ports_.monsters.read().actors) if(enemy(t) && radius(at,t.position,range)) targets.push_back(id);
        plan.impacts.push_back(impact(m,std::move(targets),damage));
    };
    if(m.program==Program::Orb) {
        const auto &p=*m.skill.frozenOrb;
        if(const auto e=missileRingEmission(m.lifetimeFrames-m.ageFrames,p.emissionPeriod,m.directionIndex,p.directionStep)) {
            child(cell(m.position),e->direction,p.bolt.missileId,p.bolt.lifetimeFrames,p.bolt.speed,Program::OrbBolt,true);
            m.directionIndex=e->nextIndex;
        }
    }
    if(m.program==Program::OrbNova) {
        const auto &p=*m.skill.frozenOrb;
        if(const auto turn=missileOrbTurn(m.turnTarget,m.lifetimeFrames-m.ageFrames,p.novaTurnFrames,p.novaTurnPeriod)) {
            m.turnTarget=*turn; m.velocity=m.turnTarget.unit()*p.nova.speed;
        }
    }
    if(m.program==Program::Blizzard) {
        const auto &p=*m.skill.blizzard; const int remaining=m.lifetimeFrames-m.ageFrames;
        if(missileEmissionDue(remaining,p.emissionPeriod)) {
            const Vec at=cell(m.position)+blizzardOffset(uint32_t(std::floor(m.position.x+area.definition.origin.x)),remaining,p.radius,false);
            if(area.definition.collision.missileSegment(at,at,{5,1})) child(at,{},p.shardId,p.shardFrames,0,Program::Shard,true);
        }
        ++m.ageFrames; plan.finished=m.ageFrames>=m.lifetimeFrames; return plan;
    }
    ++m.ageFrames;
    const bool expires=m.ageFrames>=m.lifetimeFrames;
    if(m.program==Program::AreaImpact) {
        if(expires) {areaHit(m.position,int(m.skill.missileImpact->areaMissile->radius),m.damage);plan.finished=true;}
        return plan;
    }
    if(m.program==Program::Meteor) {
        if(!expires) return plan;
        const auto &p=*m.skill.meteor; areaHit(m.position,p.radius,m.damage);
        constexpr Vec offsets[]{{2,-2},{-2,-2},{0,2},{0,5},{-3,3},{0,3},{3,3},{-1,2},{1,1},{-1,-1},{2,-1},{-4,-2},{-3,-2},{-1,-3},{0,-4},{1,-3},{3,-3},{4,-2}};
        source.skill=skills::evaluate(owner,m.skill.sourceId,m.skill.rank);
        for(int i=0;i<18;i+=p.fireStep) {
            const Vec at=m.position+offsets[i]; if(!area.definition.collision.missileSegment(at,at,{5,1})) continue;
            auto &c=child(at,{},p.fire.fireId,p.fire.fireFrames,0,Program::Fire); c.skill.firewall=source.skill.meteor->fire;
        }
        plan.finished=true; return plan;
    }
    if(m.program==Program::Fire || m.program==Program::Shard || m.program==Program::PoisonCloud) {
        if(expires) { plan.finished=true; return plan; }
        const int size=m.program==Program::Fire ? m.skill.firewall->size : m.collision.size;
        Vec next=m.position+m.velocity*TickContext::seconds;
        const auto wall=missileTerrainContact(area.definition.collision,m.position,next,m.collision);
        if(wall) next=m.position+(next-m.position)* *wall;
        std::vector<EntityId> targets;std::vector<int64_t> amounts;
        for(const auto &[id,t]:ports_.monsters.read().actors) if(enemy(t) &&
            ((m.program!=Program::Shard && m.program!=Program::PoisonCloud) || id!=m.lastHit) && missileUnitIntersection(m.position,next,size,t.position,t.rule.size)) {
            int64_t amount;
            if(m.program==Program::Fire) {
                const auto &p=*m.skill.firewall;
                amount=(int64_t(p.minimumDamage)+limitedRandom(m.random,uint32_t(p.maximumDamage-p.minimumDamage+1)))<<p.hitShift;
            } else amount=int64_t(m.skill.minimumDamage*256.f)+limitedRandom(m.random,uint32_t((m.skill.maximumDamage-m.skill.minimumDamage)*256.f));
            targets.push_back(id);amounts.push_back(amount);
            if(m.program==Program::Shard || m.program==Program::PoisonCloud) { m.lastHit=id;break; }
        }
        auto hit=impact(m,std::move(targets));hit.targetDamage=std::move(amounts);plan.impacts.push_back(std::move(hit));
        m.position=next;if(wall) plan.finished=true;return plan;
    }
    if(const auto step=advanceMissileVelocity(m.velocity.length(),m.acceleration,m.maximumVelocity,m.ageFrames)) {
        m.acceleration=step->acceleration;
        m.velocity=m.velocity.unit()*step->speed;
    }
    Vec next=m.position+m.velocity*TickContext::seconds;
    if(m.program==Program::Charged) {
        float distance=m.velocity.length()*TickContext::seconds; next=m.position;
        while(distance>0 && !m.path.empty()) {
            const Vec delta=m.path.front()-next; const float span=delta.length();
            if(span<=distance) {next=m.path.front();m.path.pop_front();distance-=span;}
            else {next=next+delta.unit()*distance;distance=0;}
        }
        if(m.path.empty()) plan.finished=true;
    }
    const auto wall=missileTerrainContact(area.definition.collision,m.position,next,m.collision);
    if(wall) next=m.position+(next-m.position)* *wall;
    if(m.program==Program::FirewallMaker) {
        if(expires || wall) plan.finished=true;
        else if(missileChangedCell(m.position,next)) {
            const auto &p=*m.skill.firewall;child(cell(next),{},p.fireId,p.fireFrames,0,Program::Fire);
        }
        m.position=next;return plan;
    }
    if(m.program==Program::Orb) {
        m.position=next;
        if(wall) plan.finished=true;
        else if(expires) {
            const auto &p=*m.skill.frozenOrb;
            for(const auto heading:missileRingBurst(p.burstStep)) child(cell(next),heading,p.nova.missileId,p.nova.lifetimeFrames,p.nova.speed,Program::OrbNova,true);
            plan.finished=true;
        }
        return plan;
    }
    if(expires) {plan.finished=true;m.position=next;return plan;}
    std::vector<std::pair<float,EntityId>> contacts;
    for(const auto &[id,t]:ports_.monsters.read().actors) {
        if(m.skill.weapon && m.skill.weapon->spear && m.ageFrames<m.skill.weapon->spear->activateFrames) continue;
        if(!enemy(t) || id==m.lastHit || (m.skill.arc && m.skill.arc->nextDelay && t.nextHitTick>m.created+uint64_t(m.ageFrames))) continue;
        if(const auto contact=missileUnitIntersection(m.position,next,m.collision.size,t.position,t.rule.size)) contacts.emplace_back(*contact,id);
    }
    std::sort(contacts.begin(),contacts.end());
    const bool piercing=m.program==Program::FuryBolt || m.program==Program::Arc || m.program==Program::Ring || m.skill.effect==SkillBehavior::Inferno;
    if(!contacts.empty()) {
        if(m.skill.effect==SkillBehavior::GlacialSpike && m.skill.freezingArea) {
            m.skill.freezingArea=skills::evaluate(owner,m.skill.sourceId,m.skill.rank).freezingArea;
            areaHit(next,m.skill.freezingArea->radius,m.damage); plan.finished=true;
        } else if(m.radius>0) {
            m.position=m.position+(next-m.position)*contacts.front().first;
            areaHit(m.position,int(m.radius),m.damage);plan.finished=true;
        } else {
            std::vector<EntityId> targets;
            for(const auto &[fraction,id]:contacts) {
                targets.push_back(id);m.lastHit=id;
                if(m.program==Program::Arc && (m.skill.effect==SkillBehavior::ChainLightning || (m.skill.weapon && m.skill.weapon->spear && m.skill.weapon->spear->kind==SpearSkillSpec::Kind::Strike))) {
                    const Vec at=m.position+(next-m.position)*fraction;std::vector<uint64_t> eligible;
                    if(m.remainingHits>1) for(const auto &[candidate,t]:ports_.monsters.read().actors) {
                        if(!enemy(t) || candidate==id || t.nextHitTick>m.created+uint64_t(m.ageFrames) ||
                            !radius(at,t.position,m.skill.arc->range) || !area.definition.collision.missileSegment(at,t.position,{4,1})) continue;
                        eligible.push_back(candidate.value);
                    }
                    const EntityId successor{missileChainSuccessor(id.value,eligible)};
                    if(successor) {
                        auto &c=child(cell(at),ports_.monsters.find(successor)->position-cell(at),m.definition,m.lifetimeFrames,m.skill.missileVelocity,Program::Arc,true);
                        c.remainingHits=m.remainingHits-1;c.lastHit=id;
                    }
                    next=at;plan.finished=true;break;
                }
                if(!piercing) {next=m.position+(next-m.position)*fraction;plan.finished=true;break;}
            }
            plan.impacts.push_back(impact(m,std::move(targets)));
        }
    } else if(wall && m.skill.effect==SkillBehavior::GlacialSpike && m.skill.freezingArea) {
        m.skill.freezingArea=skills::evaluate(owner,m.skill.sourceId,m.skill.rank).freezingArea;
        areaHit(next,m.skill.freezingArea->radius,m.damage);
    }
    else if(wall && m.radius>0) areaHit(next,int(m.radius),m.damage);
    m.position=next; ++m.revision; if(wall) plan.finished=true; return plan;
}
}
