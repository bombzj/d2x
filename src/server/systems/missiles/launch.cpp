#include "system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/amazon_missile.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/skills/common_actions.hpp"
#include "gameplay/skills/bone_spec.hpp"
#include "gameplay/skills/grim_ward_spec.hpp"
#include "core/random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::missiles {
namespace { Vec cell(Vec p) { return {std::floor(p.x) + .5f, std::floor(p.y) + .5f}; } }
Missile System::make(const Spawn &r, Vec origin, Vec direction, int id, int frames, float speed, Program program, uint64_t &random,bool deferSeed,uint64_t *ownerRandom,const Missile *originSource,uint64_t *originRandom) const {
    const auto *player=ports_.players.find(r.actor.player);
    const auto prepared=r.preparedRules?r.preparedRules:player?player->rules.skills:nullptr;
    if(!prepared) throw std::logic_error("Missing native missile rule lifetime");
    const auto &rules=*prepared;
    const auto initialRule=nativeDamageRule(rules,id,r.skill.rank);
    Missile m;m.rules=prepared;m.playerActor=r.actor.actor; m.owner = r.sourceOwner?r.sourceOwner:r.actor.actor; m.player = r.actor.player; m.area = r.actor.area; m.generation = r.actor.areaGeneration;
    const auto *unit=ports_.monsters.find(r.sourceOwner?r.sourceOwner:r.emitterType==1?r.emitter:EntityId{});
    const bool nativeUnit=unit && (r.sourceOwner || (unit->hireling && unit->owner==r.actor.player));
    if(nativeUnit) m.owner=unit->id;
    if(!deferSeed) m.binding=combat::Participants{ports_.players,ports_.monsters,ports_.social}.bind(nativeUnit?combat::ParticipantView{nullptr,unit}:combat::ParticipantView{ports_.players.find(r.actor.player),nullptr});
    m.parameters=r.parameters;
    m.pathInitialization=r.pathInitialization;
    if(program==Program::Charged && m.pathInitialization.kind==MissilePathInitialization::Kind::None)
        m.pathInitialization.kind=MissilePathInitialization::Kind::ChargedSkill;
    m.definition = id; m.created = m.executionTick = r.actor.tick; m.initializeFrames(frames,initialRule.activate);
    if(speed>0 && !direction.length()) direction={1,1};
    m.position = cell(origin); m.velocity = direction.unit() * speed; m.turnTarget = direction;
    m.pathTarget=origin+direction;
    m.collision = rules.collisions.at(id); m.skill = r.skill; m.program = program;
    if(!deferSeed) {
        auto room=m;const auto &rule=initialRule;
        if(!creationRoom(room) || (ports_.areas.at(room.area).definition.town && !rule.town) ||
            (rule.sourceTown && ports_.areas.at(unitArea(m.owner)).definition.town)) {
            m.creationRejected=true;m.weapon=r.weapon;m.damageRange=fixedMissileDamageRange(r.skill.minimumDamage.raw(),r.skill.maximumDamage.raw());return m;
        }
    }
    m.random = deferSeed?initialRandom(0):childRandom(random); m.emitter = r.emitter ? r.emitter : r.actor.actor; m.emitterType = r.emitterType;
    if(!deferSeed) initializePath(m);
    if(!deferSeed && rules.slowableMissiles.contains(id) && speed>0) {
        const int slow=ports_.effects.combatModifiers(m.owner,r.actor.tick).slowMissiles;
        if(slow) {
            const auto &motion=rules.missileVelocities.at(id);
            const auto fixed=missileVelocityFixed(motion.first,motion.second,r.skill.rank,slow);
            if(!fixed) throw std::runtime_error("Invalid prepared slowed missile velocity");
            m.velocity=m.velocity.unit()*(float(*fixed)*25.f/4096.f);
        }
    }
    m.acceleration = r.skill.missileAcceleration; m.maximumVelocity = r.skill.missileMaxVelocity;
    if(deferSeed) m.weapon=r.weapon;
    else if(r.weapon) {
        const auto modifiers=ports_.effects.combatModifiers(m.owner,r.actor.tick);
        if(r.weapon->weapon.potion) {
            WeaponSkillDamage potion;potion.weapon=r.weapon->weapon;potion.level=r.weapon->level;
            potion.automatic=potion.projectile=true;potion.weapon.target={};potion.weapon.blunt=false;
            m.weapon=std::move(potion);
        } else m.weapon=prepareWeaponSkillProcedures(r.weapon->weapon,modifiers,r.skill,r.weapon->level,true);
    }
    if(!deferSeed) {
        auto rule=initialRule;
        if(nativeUnit) {rule.damageStats=unit->rule.stats;rule.damageFormulas=unit->rule.formulas;}
        NativeDamage damage;damage.rule=rule;
        auto local=nativeUnit?unit->combatRandom:ports_.players.find(r.actor.player)->combatRandom;bool ownerDrawn{};
        auto &seed=ownerRandom?*ownerRandom:local;
        damage.data=prepareDamage(m,rule,{},seed,ownerDrawn,originSource,
            originRandom?std::optional{std::ref(*originRandom)}:std::nullopt,&damage.monsterTypeBonuses);
        if(ownerDrawn && !ownerRandom) throw std::logic_error("Native missile requires an owner seed loan");
        if(nativeUnit) {
            const auto values=ports_.effects.unitStatValues(unit->id,r.actor.tick);
            if(!values) throw std::logic_error("Missing native missile attack-bonus source");
            damage.attackBonus=values->get({unit->rule.stats->names.at("tohit"),0});
        }
        if(r.weapon && !r.weapon->weapon.potion && r.skill.weapon) {
            damage.data.damagePercent=nativeAdd(damage.data.damagePercent,r.skill.weapon->damagePercent);
            const auto source=rules.formulas->skills.find(r.skill.sourceId);
            const int program=source==rules.formulas->skills.end()?0:source->second.number("srvdofunc");
            // Native missile accuracy does not automatically add a skill's
            // ToHitFactor. Only the actual launch initializer writes TOHIT.
            if(program==74) damage.attackBonus=nativeAdd(damage.attackBonus,r.skill.weapon->attackRating);
            else if(program==3 || program==5) {
                const bool throwing=r.skill.weapon->useThrowMastery;
                if(throwing) {
                    damage.attackBonus=nativeAdd(damage.attackBonus,r.weapon->weapon.throwMastery[0]);
                    damage.data.damagePercent=nativeAdd(damage.data.damagePercent,r.weapon->weapon.throwMastery[1]);
                }
            }
        }
        m.nativeDamage=std::move(damage);
    }
    if(!deferSeed) initializePierce(m);
    m.guidance=r.guidedTarget;
    initializeGuidance(m,r.guidedTarget,r.target);
    m.radius = r.skill.missileImpact ? r.skill.missileImpact->radius : 0;
    const auto range = fixedMissileDamageRange(r.skill.minimumDamage.raw(),r.skill.maximumDamage.raw());
    if(!m.nativeDamage) m.damageRange=range;
    return m;
}
std::vector<Missile> System::launch(const Spawn &r, uint64_t &random,uint64_t *ownerRandom) const {
    const auto &p = *ports_.players.find(r.actor.player); const auto &s = r.skill;
    Vec origin = r.origin.value_or(p.position), direction = r.target - origin;
    if (!direction.length()) direction = p.look.length() ? p.look * 10.f : Vec{1,1};
    const int frames = std::max(1, int(std::lround(s.missileLifetime * 25.f)));
    std::vector<Missile> result;
    const auto add = [&](Vec at, Vec heading, int id, int life, float speed, Program program,int pathIndex=0) -> Missile & {
        auto request=r;request.pathInitialization.index=pathIndex;
        result.push_back(make(request, at, heading, id, life, speed, program, random,false,ownerRandom)); return result.back();
    };
    switch (s.effect) {
    case SkillBehavior::GrimWard: {
        const auto form=size_t(s.missileCount);const auto &shape=s.grimWard->forms.at(form);
        auto &ward=add(cell(r.target),{},shape.start,shape.startFrames,0,Program::GrimWard);
        ward.directionIndex=int(form);break;
    }
    case SkillBehavior::PoisonExplosion: {
        const auto &burst=*s.missileImpact->cloudBurst;const auto &cloud=burst.cloud;
        for(const auto heading:poisonCloudDirections(burst.mainStep,burst.subStep)) {
            auto &m=add(cell(origin),heading.direction,cloud.missileId,cloud.lifetimeFrames,heading.secondary?burst.subSpeed:burst.mainSpeed,Program::PoisonCloud);
            m.skill.minimumDamage=Fixed8::fromRaw(cloud.minimum);m.skill.maximumDamage=Fixed8::fromRaw(cloud.maximum);m.skill.poisonDuration=float(cloud.poisonFrames)/25.f;m.skill.poisonDurationFrames=cloud.poisonFrames;
        }
        break;
    }
    case SkillBehavior::Teeth:
        for(const auto target:missileFanTargets(origin,r.target,s.missileCount,direction))
            add(cell(origin),target-origin,s.missileId,frames,s.missileVelocity,Program::Projectile);
        break;
    case SkillBehavior::BoneSpirit: {
        const int life=r.guidedTarget?frames:std::max(1,int(std::lround(direction.length()/std::max(.001f,s.missileVelocity)*25.f)));
        add(cell(origin),direction,s.missileId,life,s.missileVelocity,Program::Spirit);break;
    }
    case SkillBehavior::WeaponProjectile:
        if(r.weapon && r.weapon->weapon.potion) {
            const auto &projectile=*r.weapon->weapon.projectile;
            const int lifetime=groundThrowFrames(origin,r.target,projectile.velocityUnits);
            if(lifetime<=0) break;
            add(cell(origin),direction,s.missileId,lifetime,s.missileVelocity,Program::GroundThrow);break;
        }
        if(s.weapon->spear && s.weapon->spear->kind==SpearSkillSpec::Kind::Charged && !r.weapon) {
            for(int i=0;i<s.missileCount;++i) {
                add(cell(origin),direction,s.missileId,frames,s.missileVelocity,Program::Charged,i);
            }
            break;
        }
        if(s.weapon->bow && s.weapon->bow->multiple) {
            const auto targets=missileFanTargets(origin,r.target,s.missileCount,direction);
            const int id=p.totals.equipment.animationClass=="xbw"?s.weapon->bow->boltId:s.missileId;
            const int central=std::min(int(targets.size()),s.weapon->bow->centralArrows);
            const int first=(int(targets.size())-central)/2;
            for(size_t arrow=0;arrow<targets.size();++arrow) {
                auto &missile=add(cell(origin),targets[arrow]-origin,id,frames,s.missileVelocity,Program::Projectile);
                // SrvDo008's outer missiles suppress item damage events. Leech
                // still belongs to their physical channel, unlike CB and OW.
                if(missile.weapon && (int(arrow)<first || int(arrow)>=first+central)) {
                    missile.weapon->crushingBlow=missile.weapon->openWounds=0;
                    missile.suppressSourceEvents=true;
                }
            }
        } else {
            const int id=s.weapon->bow && (s.weapon->bow->guided || s.weapon->bow->strafe) && p.totals.equipment.animationClass=="xbw"?s.weapon->bow->boltId:s.missileId;
            add(cell(origin),direction,id,frames,s.missileVelocity,Program::Projectile);
        }
        break;
    case SkillBehavior::FireBolt: case SkillBehavior::Fireball: case SkillBehavior::IceBolt: case SkillBehavior::IceBlast:
    case SkillBehavior::HolyBolt:
    case SkillBehavior::BoneSpear:
    case SkillBehavior::Inferno: case SkillBehavior::ChillingArmor: case SkillBehavior::Hydra:
        add(origin + direction.unit() * .7f, direction, s.missileId, frames, s.missileVelocity, Program::Projectile); break;
    case SkillBehavior::ChargedBolt:
        for (int i=0; i<s.missileCount; ++i) {
            add(cell(origin),direction,s.missileId,frames,s.missileVelocity,Program::Charged,i);
        } break;
    case SkillBehavior::Howl: case SkillBehavior::Shout: case SkillBehavior::BattleCry: case SkillBehavior::BattleOrders: case SkillBehavior::WarCry: case SkillBehavior::BattleCommand:
    case SkillBehavior::FrostNova: case SkillBehavior::Nova: case SkillBehavior::PoisonNova:
        for (int i=0; i<64; ++i) add(cell(origin), missileRingDirection(i), s.missileId, frames, s.missileVelocity, Program::Ring);
        break;
    case SkillBehavior::GlacialSpike:
        add(cell(origin), direction, s.missileId, frames, s.missileVelocity, Program::Projectile); break;
    case SkillBehavior::FrozenOrb:
        add(cell(origin), {std::floor(r.target.x)-std::floor(origin.x),std::floor(r.target.y)-std::floor(origin.y)}, s.missileId,
            s.frozenOrb->lifetimeFrames, s.missileVelocity, Program::Orb); break;
    case SkillBehavior::Blizzard:
        add(cell(r.target), {}, s.missileId, frames, 0, Program::Blizzard); break;
    case SkillBehavior::Lightning: case SkillBehavior::ChainLightning: {
        auto &missile=add(cell(origin),direction,s.missileId,frames,s.missileVelocity,Program::Arc);
        missile.remainingHits=s.arc->count;missile.parameters[0]=s.arc->count;break;
    }
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
        const int bonus=nativeMultiplyDivide(ports_.effects.stateModifiers(p.actor,s.concentrationState,r.actor.tick).combat.damagePercent,s.concentrationFactor,100);
        if(m.nativeDamage) m.nativeDamage->data.magic=scaleMissileDamage(m.nativeDamage->data.magic,nativeAdd(100,bonus));
        else m.damageRange=scaleMissileDamage(*m.damageRange,100+bonus);break;
    }
    case SkillBehavior::FistOfTheHeavens: {
        auto &missile=add(cell(r.target),{},s.missileId,s.heaven->delayFrames,0,Program::Heaven);
        missile.parameters={ports_.players.findActor(r.guidedTarget)?0:1,nativeNarrow(r.guidedTarget.value)};break;
    }
    default: break;
    }
    return result;
}
std::vector<DomainFact> System::visuals(const std::vector<Missile> &missiles) const {
    std::vector<DomainFact> facts;
    for(const auto &m:missiles) {
        const bool send=m.enemy?m.enemy->rule.clientSend:m.rules && m.rules->clientSend.at(m.definition);
        if(!send) continue;
        Vec destination;
        if(m.velocity.length()) destination=m.path.empty()?m.position+m.turnTarget:m.path.front();
        const auto remaining=m.lifetimeFrames-m.ageFrames;if(remaining<=0) continue;
        facts.emplace_back(MissileFact{m.owner,uint8_t(ports_.players.findActor(m.owner)?0:1),m.area,m.definition,
            m.skill.rank,remaining,m.position,destination,uint8_t(m.pierces),m.id});
    }
    return facts;
}
std::vector<MissileFact> System::visible(PlayerId id) const {
    const auto *player=ports_.players.find(id);if(!player || !player->entered) return {};
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    std::vector<MissileFact> result;
    for(const auto &[key,missile]:state_.missiles) {
        (void)key;
        if(!participants.find(missile.owner).id() || !ports_.areas.nearby(player->area,player->position,missile.area,missile.position)) continue;
        for(const auto &fact:visuals({missile})) result.push_back(std::get<MissileFact>(fact));
    }
    return result;
}
DomainResult<EntityId> System::spawn(const Spawn &r) {
    const auto *p = ports_.players.find(r.actor.player); const auto *a = ports_.areas.find(r.actor.area);
    if (!p || !p->entered || p->actor != r.actor.actor || p->area != r.actor.area || (p->persistent.player.hp <= 0 && !r.deathTrigger))
        return {DomainStatus::InvalidActor,{}};
    if (!a || a->generation != r.actor.areaGeneration || !p->rules.skills)
        return {DomainStatus::Unavailable,{}};
    if (!std::isfinite(r.target.x) || !std::isfinite(r.target.y) || r.skill.rank<=0 || r.skill.rank>255 ||
        r.skill.minimumDamage.raw()<0 || r.skill.maximumDamage<r.skill.minimumDamage)
        return {DomainStatus::InvalidRequest,{}};
    if(r.emitterType==1) if(const auto *unit=ports_.monsters.find(r.emitter);unit && unit->hireling &&
        (unit->life<=0 || unit->owner!=r.actor.player || unit->area!=r.actor.area || !r.free)) return {DomainStatus::InvalidActor,{}};
    auto random = ports_.random;
    const auto *nativeOwner=r.emitterType==1?ports_.monsters.find(r.emitter):nullptr;
    if(r.sourceOwner && r.sourceOwner!=(nativeOwner?nativeOwner->id:p->actor)) return {DomainStatus::InvalidRequest,{}};
    if(nativeOwner && (r.cost || !r.free || !nativeOwner->hireling || nativeOwner->owner!=r.actor.player)) return {DomainStatus::InvalidRequest,{}};
    const auto ownerExpected=nativeOwner?nativeOwner->combatRandom:r.cost && r.cost->random?r.cost->random->expected:p->combatRandom;
    auto ownerRandom=nativeOwner?nativeOwner->combatRandom:r.cost && r.cost->random?r.cost->random->next:p->combatRandom;
    auto launched = launch(r,random,&ownerRandom); if (launched.empty()) return {DomainStatus::NotImplemented,{}};
    std::erase_if(launched,[&](auto &missile){return missile.creationRejected || !creationRoom(missile) || !townAllowed(missile);});
    if(launched.empty()) return {DomainStatus::Unavailable,{}};
    if (launched.size()>UINT32_MAX-ports_.ids.cursor()) return {DomainStatus::Capacity,{}};
    auto identity=ports_.ids.cursor();for(auto &missile:launched) missile.id=EntityId{identity++};
    auto facts = visuals(launched);
    if(r.skill.heaven && r.guidedTarget && r.skill.hitOverlayId>=0) facts.emplace_back(OverlayFact{r.guidedTarget,1,r.actor.area,r.skill.hitOverlayId});
    if (!ports_.events.hasCapacity(facts.size()+1,2)) return {DomainStatus::Capacity,{}};
    std::map<EntityId,Missile> prepared; auto cursor = ports_.ids.cursor();
    for (auto &m : launched) { m.id=EntityId{cursor++}; prepared.emplace(m.id,std::move(m)); }
    const auto id=prepared.begin()->first;
    auto timers=prepareTimers(prepared);
    auto cost=r.cost;
    // Ordinary missile release changes mana and the owner's random cursor,
    // not equipment/passive sources. Publish those with the prepared visuals
    // in one resource transaction; charged items retain their full item edit.
    const bool resourcePayment=!nativeOwner && !cost && !r.deathTrigger && !r.skill.charge && (!r.free || ownerRandom!=p->combatRandom);
    if(!nativeOwner && !cost && !resourcePayment && (!r.free || ownerRandom!=p->combatRandom)) {
        transactions::CharacterEdit edit{r.actor,p->inventoryRevision,p->characterRevision,p->persistent.player};
        if(!r.free) {edit.player.mana-=r.skill.manaCost;edit.skillPayment=transactions::SkillManaPayment{r.skill.manaCost,r.skill.sourceId};edit.charge=r.skill.charge;}
        edit.random=transactions::UnitRandomEdit{ownerExpected,ownerRandom};
        auto transaction=ports_.transactions.prepare(std::move(edit));if(!transaction) return {transaction.status,{}};
        cost=std::move(*transaction.value);
    } else if(cost) cost->random=transactions::UnitRandomEdit{ownerExpected,ownerRandom};
    const auto released=nativeOwner?ports_.monsters.consumeRandom(nativeOwner->id,ownerExpected,ownerRandom):
        resourcePayment?ports_.transactions.release(r.actor,p->characterRevision,r.free?Fixed8{}:r.skill.manaCost,
            {},{},std::move(facts),transactions::UnitRandomEdit{ownerExpected,ownerRandom},r.free?-1:r.skill.sourceId):
        cost?ports_.transactions.commit(std::move(*cost)):DomainResult<>{DomainStatus::Applied,std::monostate{}};
    if (!released) return {released.status,{}};
    if(resourcePayment) facts.clear();
    if (!facts.empty()) ports_.events.publish({0,r.actor.tick,{}, {AudienceKind::Area,{},r.actor.area},std::move(facts)});
    for (size_t i=0;i<prepared.size();++i) ports_.ids.allocate();
    state_.missiles.merge(prepared);publishTimers(timers);ports_.random=random;
    return {DomainStatus::Applied,id};
}
}
