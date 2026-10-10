#include "system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/amazon_missile.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/skills/common_actions.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::missiles {
namespace { Vec cell(Vec p) { return {std::floor(p.x) + .5f, std::floor(p.y) + .5f}; } }
Missile System::make(const Spawn &r, Vec origin, Vec direction, int id, int frames, float speed, Program program, uint64_t &random) const {
    const auto &rules = *ports_.players.find(r.actor.player)->rules.skills;
    Missile m; m.owner = r.actor.actor; m.player = r.actor.player; m.area = r.actor.area; m.generation = r.actor.areaGeneration;
    m.binding=combat::Participants{ports_.players,ports_.monsters}.bind({ports_.players.find(r.actor.player),nullptr});
    m.definition = id; m.created = r.actor.tick; m.expires = r.actor.tick + uint64_t(frames);
    if(speed>0 && !direction.length()) direction={1,1};
    m.position = origin; m.velocity = direction.unit() * speed; m.turnTarget = direction;
    m.collision = rules.collisions.at(id); m.skill = r.skill; m.program = program; m.lifetimeFrames = frames;
    m.random = childRandom(random); m.emitter = r.emitter ? r.emitter : r.actor.actor; m.emitterType = r.emitterType;
    if(rules.slowableMissiles.contains(id) && speed>0) {
        const int slow=m.emitterType==1?ports_.effects.unitModifiers(m.emitter,r.actor.tick).combat.slowMissiles:ports_.players.find(r.actor.player)->totals.character.combat.slowMissiles;
        if(slow) {
            const auto &motion=rules.missileVelocities.at(id);
            const auto fixed=missileVelocityFixed(motion.first,motion.second,r.skill.rank,slow);
            if(!fixed) throw std::runtime_error("Invalid prepared slowed missile velocity");
            m.velocity=m.velocity.unit()*(float(*fixed)*25.f/4096.f);
        }
    }
    m.acceleration = r.skill.missileAcceleration; m.maximumVelocity = r.skill.missileMaxVelocity;
    if(r.weapon && r.weapon->weapon.potion) m.weapon=rollPotionDamage(r.weapon->weapon,r.weapon->level,m.random);
    else if(r.weapon) m.weapon=rollWeaponSkillDamage(r.weapon->weapon,ports_.players.find(r.actor.player)->totals.character.combat,r.skill,r.weapon->level,true,m.random);
    if(m.weapon && rules.pierceableMissiles.contains(id)) m.pierces=missilePierceCount(m.weapon->pierceChance,0);
    m.guidance=r.guidedTarget;
    m.radius = r.skill.missileImpact ? r.skill.missileImpact->radius : 0;
    const float fraction = float(rollRandom(m.random)) / 4294967295.f;
    m.damage = int64_t((r.skill.minimumDamage + (r.skill.maximumDamage - r.skill.minimumDamage) * fraction) * 256.f);
    return m;
}
std::vector<Missile> System::launch(const Spawn &r, uint64_t &random) const {
    const auto &p = *ports_.players.find(r.actor.player); const auto &s = r.skill;
    Vec origin = r.origin.value_or(p.position), direction = r.target - origin;
    if (!direction.length()) direction = p.look.length() ? p.look * 10.f : Vec{1,1};
    const int frames = std::max(1, int(std::lround(s.missileLifetime * 25.f)));
    std::vector<Missile> result;
    const auto add = [&](Vec at, Vec heading, int id, int life, float speed, Program program) -> Missile & {
        result.push_back(make(r, at, heading, id, life, speed, program, random)); return result.back();
    };
    switch (s.effect) {
    case SkillBehavior::WeaponProjectile:
        if(r.weapon && r.weapon->weapon.potion) {
            const auto &projectile=*r.weapon->weapon.projectile;
            const int lifetime=groundThrowFrames(origin,r.target,projectile.velocityUnits);
            if(lifetime<=0) break;
            add(cell(origin),direction,s.missileId,lifetime,s.missileVelocity,Program::GroundThrow);break;
        }
        if(s.weapon->spear && s.weapon->spear->kind==SpearSkillSpec::Kind::Charged && !r.weapon) {
            for(int i=0;i<s.missileCount;++i) {
                auto &m=add(cell(origin),direction,s.missileId,std::min(frames,77),s.missileVelocity,Program::Charged);
                const auto path=chargedBoltPath(origin,r.target,i,m.lifetimeFrames);m.path.assign(path.begin(),path.end());
            }
            break;
        }
        if(s.weapon->bow && s.weapon->bow->multiple) {
            const auto targets=missileFanTargets(origin,r.target,s.missileCount,direction);
            const int id=p.totals.equipment.animationClass=="xbw"?s.weapon->bow->boltId:s.missileId;
            for(const auto target:targets) add(cell(origin),target-origin,id,frames,s.missileVelocity,Program::Projectile);
        } else {
            const int id=s.weapon->bow && (s.weapon->bow->guided || s.weapon->bow->strafe) && p.totals.equipment.animationClass=="xbw"?s.weapon->bow->boltId:s.missileId;
            add(cell(origin),direction,id,frames,s.missileVelocity,Program::Projectile);
        }
        break;
    case SkillBehavior::FireBolt: case SkillBehavior::Fireball: case SkillBehavior::IceBolt: case SkillBehavior::IceBlast:
    case SkillBehavior::HolyBolt:
    case SkillBehavior::Inferno: case SkillBehavior::ChillingArmor: case SkillBehavior::Hydra:
        add(origin + direction.unit() * .7f, direction, s.missileId, frames, s.missileVelocity, Program::Projectile); break;
    case SkillBehavior::ChargedBolt:
        for (int i=0; i<s.missileCount; ++i) {
            auto &m = add(cell(origin), direction, s.missileId, std::min(frames,77), s.missileVelocity, Program::Charged);
            const auto path = chargedBoltPath(origin, origin + direction, i, m.lifetimeFrames); m.path.assign(path.begin(), path.end());
        } break;
    case SkillBehavior::FrostNova: case SkillBehavior::Nova:
        for (int i=0; i<64; ++i) add(cell(origin), missileRingDirection(i), s.missileId, frames, s.missileVelocity, Program::Ring);
        break;
    case SkillBehavior::GlacialSpike:
        add(cell(origin), direction, s.missileId, frames, s.missileVelocity, Program::Projectile); break;
    case SkillBehavior::FrozenOrb:
        add(cell(origin), {std::floor(r.target.x)-std::floor(origin.x),std::floor(r.target.y)-std::floor(origin.y)}, s.missileId,
            s.frozenOrb->lifetimeFrames, s.missileVelocity, Program::Orb); break;
    case SkillBehavior::Blizzard:
        add(cell(r.target), {}, s.missileId, frames, 0, Program::Blizzard); break;
    case SkillBehavior::Lightning: case SkillBehavior::ChainLightning:
        add(cell(origin), direction, s.missileId, frames, s.missileVelocity, Program::Arc).remainingHits = s.arc->count; break;
    case SkillBehavior::FireWall: {
        const auto &f = *s.firewall; const Vec perpendicular = missileWallDirection(origin,r.target);
        add(cell(r.target), perpendicular, f.makerId, f.makerFrames, f.velocity, Program::FirewallMaker);
        add(cell(r.target), perpendicular * -1.f, f.makerId, f.makerFrames, f.velocity, Program::FirewallMaker);
        add(cell(r.target), {}, f.fireId, f.fireFrames, 0, Program::Fire); break;
    }
    case SkillBehavior::Blaze:
        add({float(int(r.target.x)),float(int(r.target.y))}, {}, s.firewall->fireId, s.firewall->fireFrames, 0, Program::Fire); break;
    case SkillBehavior::Meteor:
        add(cell(r.target), {}, s.missileId, frames, 0, Program::Meteor); break;
    case SkillBehavior::BlessedHammer: {
        auto &m=add(cell(origin),direction,s.missileId,frames,s.missileVelocity,Program::Hammer);
        const auto path=blessedHammerPath(m.position);m.path.assign(path.begin(),path.end());
        const int bonus=ports_.effects.stateModifiers(p.actor,s.concentrationState,r.actor.tick).combat.damagePercent*s.concentrationFactor/100;
        m.damage=m.damage*(100+bonus)/100;break;
    }
    case SkillBehavior::FistOfTheHeavens:
        add(cell(r.target),{},s.missileId,s.heaven->delayFrames,0,Program::Heaven);break;
    default: break;
    }
    return result;
}
std::vector<DomainFact> System::visuals(const std::vector<Missile> &missiles) const {
    std::vector<DomainFact> facts;
    for (const auto &m : missiles) {
        if (m.enemy) {
            if (m.enemy->rule.clientSend) facts.emplace_back(MissileFact{m.owner,1,m.area,m.definition,m.enemy->rule.rank,m.lifetimeFrames,m.position,m.position+m.turnTarget});
            continue;
        }
        if(m.program==Program::PoisonCloud || (m.program==Program::Fire && m.skill.weapon && m.skill.weapon->bow && m.skill.weapon->bow->immolation)) continue;
        // These native programs reconstruct regular creation from cast/state
        // notifications. ClientSend permits 73 on visibility admission, not
        // a second spawn broadcast (SUnitMsg::FirstFn / MISSILES_SyncToClient).
        if (m.skill.effect == SkillBehavior::Blaze || m.skill.effect == SkillBehavior::FireWall ||
            m.skill.effect == SkillBehavior::Meteor || m.skill.effect == SkillBehavior::Blizzard) continue;
        const auto &rules = *ports_.players.find(m.player)->rules.skills;
        const auto sent = rules.clientSend.find(m.definition);
        if (sent != rules.clientSend.end() && sent->second)
            facts.emplace_back(MissileFact{m.emitter, m.emitterType, m.area, m.definition, m.skill.rank, m.lifetimeFrames, m.position,
                m.velocity.length() ? m.position + m.turnTarget : Vec{},uint8_t(m.pierces) });
    }
    return facts;
}
DomainResult<EntityId> System::spawn(const Spawn &r) {
    const auto *p = ports_.players.find(r.actor.player); const auto *a = ports_.areas.find(r.actor.area);
    if (!p || !p->entered || p->actor != r.actor.actor || p->area != r.actor.area || (p->persistent.player.hp <= 0 && !r.deathTrigger))
        return {DomainStatus::InvalidActor,{}};
    if (!a || a->generation != r.actor.areaGeneration || a->definition.town || !p->rules.skills)
        return {DomainStatus::Unavailable,{}};
    if (!std::isfinite(r.target.x) || !std::isfinite(r.target.y) || r.skill.rank<=0 || r.skill.rank>255 ||
        !std::isfinite(r.skill.minimumDamage) || !std::isfinite(r.skill.maximumDamage) || r.skill.minimumDamage<0 ||
        r.skill.maximumDamage<r.skill.minimumDamage || double(r.skill.maximumDamage)*256>INT32_MAX)
        return {DomainStatus::InvalidRequest,{}};
    auto random = ports_.random;
    auto launched = launch(r,random); if (launched.empty()) return {DomainStatus::NotImplemented,{}};
    if (launched.size()>4096-state_.missiles.size() || launched.size()>UINT32_MAX-ports_.ids.cursor()) return {DomainStatus::Capacity,{}};
    auto facts = visuals(launched);
    if(r.skill.heaven && r.guidedTarget && r.skill.hitOverlayId>=0) facts.emplace_back(OverlayFact{r.guidedTarget,1,r.actor.area,r.skill.hitOverlayId});
    if (!ports_.events.hasCapacity(facts.size()+1,2)) return {DomainStatus::Capacity,{}};
    std::map<EntityId,Missile> prepared; auto cursor = ports_.ids.cursor();
    for (auto &m : launched) { m.id=EntityId{cursor++}; prepared.emplace(m.id,std::move(m)); }
    const auto id=prepared.begin()->first;
    const auto released = r.cost ? ports_.transactions.commit(*r.cost) : r.free ? DomainResult<>{DomainStatus::Applied,std::monostate{}} : ports_.transactions.release(r.actor,p->characterRevision,r.skill.manaCost,{},r.skill.charge);
    if (!released) return {released.status,{}};
    if (!facts.empty()) ports_.events.publish({0,r.actor.tick,{}, {AudienceKind::Area,{},r.actor.area},std::move(facts)});
    for (size_t i=0;i<prepared.size();++i) ports_.ids.allocate();
    state_.missiles.merge(prepared); ports_.random=random;
    return {DomainStatus::Applied,id};
}
}
