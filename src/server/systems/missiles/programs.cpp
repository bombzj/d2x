#include "system.hpp"
#include "runtime.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/skills/bone_spec.hpp"
#include "gameplay/skills/grim_ward_spec.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/skills/targeting.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::missiles {
namespace {
Vec cell(Vec p) { return {std::floor(p.x)+.5f,std::floor(p.y)+.5f}; }
bool radius(Vec a, Vec b, int r) { return withinSkillArea(a,b,float(r),SkillAreaCoordinates::FlooredCells); }
DamageType element(SkillBehavior s) {
    switch(s) {
    case SkillBehavior::WarCry: return DamageType::Physical;
    case SkillBehavior::HolyBolt: case SkillBehavior::BlessedHammer: case SkillBehavior::Teeth: case SkillBehavior::BoneSpear: case SkillBehavior::BoneSpirit: return DamageType::Magic;
    case SkillBehavior::FistOfTheHeavens: return DamageType::Lightning;
    case SkillBehavior::PoisonNova: return DamageType::Poison;
    case SkillBehavior::IceBolt: case SkillBehavior::IceBlast: case SkillBehavior::FrostNova: case SkillBehavior::GlacialSpike:
    case SkillBehavior::FrozenArmor: case SkillBehavior::FrozenOrb: case SkillBehavior::Blizzard: case SkillBehavior::ShiverArmor: case SkillBehavior::ChillingArmor: return DamageType::Cold;
    case SkillBehavior::ChargedBolt: case SkillBehavior::Nova: case SkillBehavior::Lightning: case SkillBehavior::ChainLightning:
    case SkillBehavior::ThunderStorm: case SkillBehavior::Telekinesis: case SkillBehavior::StaticField: return DamageType::Lightning;
    default: return DamageType::Fire;
    }
}
}
combat::SpellImpact System::impact(Missile &m, std::vector<EntityId> targets, std::optional<int64_t> damage,bool missileContact,std::shared_ptr<MissileDamageRollState> rolls) const {
    const auto *owner=ports_.players.find(m.player);
    if(owner && (!owner->entered || owner->actor!=m.playerActor)) owner=nullptr;
    const auto &rules=*m.rules;
    combat::SpellImpact result{m.id,m.owner,m.area,element(m.skill.effect),damage.value_or(m.damage),std::move(targets)};
    if(!result.targets.empty()) result.area=unitArea(result.targets.front());
    result.allocatedMissile=bool(m.nativeDamage);
    result.collideFriend=m.nativeDamage && m.nativeDamage->rule.collideFriend;
    result.binding=m.binding;result.suppressSourceEvents=m.suppressSourceEvents;
    if(!m.nativeDamage && !ports_.monsters.find(m.owner) && (!owner || owner->actor!=m.owner)) {result.targets.clear();return result;}
    result.returnFire=missileContact && rules.returnFire.at(m.definition);
    result.occurrence=uint64_t(m.ageFrames);
    result.stunFrames=uint64_t(std::max(0,m.skill.stunFrames));result.hitClass=uint8_t(m.skill.hitClass);
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const bool healingContact=!m.nativeDamage && m.skill.effect==SkillBehavior::HolyBolt && !result.targets.empty() &&
        participants.petOrPartyAlly(participants.find(m.owner),participants.find(result.targets.front()),ports_.social);
    if(!damage && rolls && !healingContact) {
        result.missileDamage=std::move(rolls);
        result.weapon=m.weapon;
        const auto &rule=m.nativeDamage->rule;
        result.monsterToHit=rule.toHit;result.monsterRating=m.nativeDamage->attackBonus;result.hitClass=uint8_t(rule.hitClass);
        result.nextDelay=uint64_t(std::max(0,rule.nextDelay));
        if(rule.nextHit && (!rules.pd2 || rule.nextDelay)) result.nativeNextHit=rule.nextHitState;
        result.pd2NextHitKey=pd2NextHitKey(m);
        result.monsterStates={rules.chilled,rules.poisoned,{}};
    }
    if(!result.missileDamage && !damage && m.damageRange && !healingContact) {
        result.targetDamage.reserve(result.targets.size());
        for(size_t index=0;index<result.targets.size();++index)
            result.targetDamage.push_back(rollMissileDamage(*m.damageRange,m.random));
    }
    if(!result.missileDamage && m.skill.missileImpact && (m.skill.missileImpact->undeadDamagePercent || m.skill.missileImpact->demonDamagePercent)) {
        if(result.targetDamage.empty()) result.targetDamage.assign(result.targets.size(),result.damage);
        for(size_t index=0;index<result.targets.size();++index) {
            const auto id=result.targets[index];
            const auto *target=ports_.monsters.find(id);
            const int percent=target?((target->rule.undead?m.skill.missileImpact->undeadDamagePercent:0)+(target->rule.demon?m.skill.missileImpact->demonDamagePercent:0)):0;
            result.targetDamage[index]=result.targetDamage[index]*(100+percent)/100;
        }
    }
    if(!m.nativeDamage && m.skill.effect==SkillBehavior::HolyBolt) {
        result.undeadOnly=true;
        if(healingContact) {
            result.healingContact=true;result.healingRolls=rolls;
            if(!rolls) throw std::logic_error("Native Holy Bolt healing requires its missile seed");
        }
        result.healingOverlay=m.nativeDamage?m.nativeDamage->rule.programOverlay:m.skill.hitOverlayId;
    }
    if(m.skill.weapon && m.skill.weapon->spear && (m.skill.weapon->spear->kind==SpearSkillSpec::Kind::Charged ||
        m.skill.weapon->spear->kind==SpearSkillSpec::Kind::Strike || m.skill.weapon->spear->kind==SpearSkillSpec::Kind::Fury)) result.type=DamageType::Lightning;
    if(m.program==Program::AreaImpact && m.skill.missileImpact && m.skill.missileImpact->areaMissile) {
        result.type=m.skill.missileImpact->areaMissile->element;
        result.freeze=result.type==DamageType::Cold;
    }
    if(m.program==Program::PoisonCloud || m.skill.effect==SkillBehavior::PoisonNova) {result.type=DamageType::Poison;result.poisonFrames=uint64_t(std::max(0,m.skill.poisonDurationFrames.value_or(int(std::lround(m.skill.poisonDuration*25.f)))));}
    result.coldFrames=uint64_t(std::max(0,m.skill.coldDurationFrames.value_or(int(std::lround(m.skill.coldDuration*25.f)))));
    result.freeze=result.freeze || m.skill.effect==SkillBehavior::IceBlast || m.skill.effect==SkillBehavior::GlacialSpike;
    if (m.skill.freezingArea) result.coldFrames=uint64_t(m.skill.freezingArea->freezeFrames);
    const auto modifiers=ports_.effects.combatModifiers(m.owner,m.executionTick);
    result.coldPierce=modifiers.coldPierce;result.poisonPierce=modifiers.poisonPierce;
    if(!result.missileDamage) result.nextDelay=uint64_t(m.skill.arc ? m.skill.arc->nextDelay : std::max(0,int(std::lround(m.skill.missileNextDelay*25.f))));
    return result;
}
combat::SpellImpact System::impact(Advance &plan,std::vector<EntityId> targets,std::optional<int64_t> damage,bool missileContact) const {
    return impact(plan.next,std::move(targets),damage,missileContact,plan.damageRolls);
}
std::optional<combat::SpellImpact> System::nativeAreaContact(EntityId id,bool collisionCallback) const {
    const auto found=state_.missiles.find(id);if(found==state_.missiles.end() || !found->second.nativeDamage) return {};
    auto missile=found->second;
    auto result=impact(missile,{},std::nullopt,collisionCallback,damageRolls(missile));
    result.nativeMissileFlags=true;result.nextDelay=0;result.nativeNextHit.reset();
    return result;
}
void System::finishNativeAreaContact(const combat::SpellImpact &contact) {
    const auto found=state_.missiles.find(contact.projectile);
    if(found!=state_.missiles.end() && contact.missileDamage) {found->second.random=contact.missileDamage->random;++found->second.revision;}
}
void System::removeTransient(EntityId id) {
    const auto found=state_.missiles.find(id);
    if(found!=state_.missiles.end() && found->second.nativeDamage &&
       found->second.nativeDamage->rule.damageFormulas->missiles.at(found->second.definition).number("collision"))
        ports_.areas.setMissileMask(found->second.area,found->second.position,found->second.collision.size,false);
    ports_.events.discardUnitSound(id);ports_.areas.removeRoomUnit(id);ports_.frameEvents.cancelOwner({FrameEventUnit::Missile,id});state_.missiles.erase(id);
}
System::Advance System::advance(const Missile &sourceMissile,TickContext tick) const {
    auto original=sourceMissile;original.executionTick=tick.tick;
    if(auto native=advanceNative(original)) return std::move(*native);
    if(original.enemy) throw std::logic_error("Monster missile lacks its native Do program");
    if(original.program==Program::GroundThrow) return advanceGroundThrow(original);
    if(original.weapon) return advanceWeapon(original);
    Advance plan{original,{},{},false};plan.damageRolls=damageRolls(original); auto &m=plan.next;
    const auto *owner=ports_.players.find(m.player);if(owner && (!owner->entered || owner->actor!=m.playerActor)) owner=nullptr;
    const auto &area=ports_.areas.at(m.area);
    Spawn source{{m.player,m.playerActor,m.area,m.generation,0,m.created},m.skill,m.collision,{},true,m.emitter,m.emitterType,{}};
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto enemy = [&](const auto &target) { return target.alive() && target.life>0 && participants.canHarm(m.owner,target.id) && target.area==m.area; };
    const auto child = [&](Vec at, Vec direction, int id, int frames, float speed, Program program,bool fresh=false,ChildSetup setup={}) -> Missile & {
        return planChild(plan,source,at,direction,id,frames,speed,program,fresh && owner && m.owner==owner->actor,std::move(setup));
    };
    const auto areaHit=[&](Vec at,int range) {areaImpact(plan,at,range);};
    if(m.program==Program::GrimWard) {
        const auto &ward=*m.skill.grimWard;const auto &form=ward.forms.at(size_t(m.directionIndex));
        if(m.remainingHits==1 && (m.lifetimeFrames-m.ageFrames)%std::max(1,form.period)==0) {
            for(const auto &[id,target]:ports_.monsters.read().actors) if(enemy(target) && radius(m.position,target.position,ward.radius)) plan.addCurse(id);
            m.skill.curse->fearOrigin=m.position+area.definition.origin;
            m.skill.curse->fearSource=m.id;
        }
        ++m.ageFrames;
        if(m.ageFrames>=m.lifetimeFrames) {
            if(m.remainingHits<2) {
                auto &phase=child(m.position,{},m.remainingHits?form.stop:form.ward,m.remainingHits?form.stopFrames:ward.duration,0,Program::GrimWard);
                phase.directionIndex=m.directionIndex;phase.remainingHits=m.remainingHits+1;
            }
            plan.finished=true;
        }
        return plan;
    }
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
            const Vec at=cell(m.position)+blizzardOffset(uint32_t(std::floor(m.position.x+area.definition.origin.x)),m.elapsedFrames(),p.radius,false,m.random);
            if(area.definition.collision.missileSegment(at,at,{5,1})) child(at,{},p.shardId,p.shardFrames,0,Program::Shard,true);
        }
        ++m.ageFrames; plan.finished=m.ageFrames>=m.lifetimeFrames; return plan;
    }
    if(m.program==Program::Spirit) {
        const auto &b=*m.skill.bone;
        if(!owner || owner->persistent.player.hp<=0) {plan.finished=true;return plan;}
        if(m.ageFrames+1>=m.lifetimeFrames && !m.guidance && !m.guidanceSearched) {
            m.guidanceSearched=true;
            for(const auto id:nearbyUnits(m)) {
                const auto point=unitPosition(m,id);
                if(point && participants.canHarm(m.owner,id) && radius(m.position,*point,b.searchRadius) && area.definition.collision.missileSegment(m.position,*unitPosition(m,id),m.collision) && (!m.guidance || id<m.guidance)) m.guidance=id;
            }
            m.lifetimeFrames=m.ageFrames+std::max(1,int(std::lround(b.spiritLifetime*25.f)));m.expires=m.created+uint64_t(m.lifetimeFrames);
            if(m.guidance) m.velocity=(*unitPosition(m,m.guidance)-m.position).unit()*m.velocity.length();
        }

        if(unitPosition(m,m.guidance) && participants.canHarm(m.owner,m.guidance) && (m.lifetimeFrames-m.ageFrames)%b.retargetPeriod==0) {
            const int distance=missileDistance(m.position,*unitPosition(m,m.guidance));
            if(distance>3 && distance<25) m.velocity=(*unitPosition(m,m.guidance)-m.position).unit()*m.velocity.length();
        }
    }
    ++m.ageFrames;
    const bool expires=m.ageFrames>=m.lifetimeFrames;
    if(m.program==Program::AreaImpact) {
        if(expires) {areaHit(m.position,int(m.skill.missileImpact->areaMissile->radius));plan.finished=true;}
        return plan;
    }
    if(m.program==Program::Heaven) {
        if(!expires) return plan;
        const auto target=participants.find(m.guidance);
        if(target.aliveIn(m.area) && participants.canHarm(m.owner,m.guidance)) {
            plan.addImpact(impact(plan,{m.guidance}));
            const auto &h=*m.skill.heaven;int count=0;
            for(const auto &[id,t]:ports_.monsters.read().actors) {
                (void)id;if(!enemy(t) || !area.definition.activation.nearby(t.position,m.position) || !radius(m.position,t.position,h.radius) || !area.definition.collision.missileSegment(m.position,t.position,{0x0805,1})) continue;
                if(count++>=h.limit) break;
                child(cell(m.position),t.position-m.position,h.boltId,h.boltFrames,float(h.boltVelocity)*75.f/16.f,Program::Projectile,true,
                    [this,player=m.player](Missile &bolt,const SkillCastSpec &current) {
                        const auto &heaven=*current.heaven;bolt.skill.effect=SkillBehavior::HolyBolt;
                        bolt.skill.minimumDamage=Fixed8::fromRaw(heaven.minimum);bolt.skill.maximumDamage=Fixed8::fromRaw(heaven.maximum);
                        bolt.skill.healingMinimum=float(heaven.healingMinimum);bolt.skill.healingMaximum=float(heaven.healingMaximum);
                        const auto *caster=ports_.players.find(player);
                        if(caster && caster->rules.skills && caster->rules.skills->formulas) {
                            const auto &catalog=*caster->rules.skills->formulas;
                            const auto holy=catalog.skillIds.find("holy bolt");
                            if(holy!=catalog.skillIds.end()) bolt.skill.hitOverlayId=catalog.skills.at(holy->second).reactionOverlay;
                        }
                        bolt.damageRange=MissileDamageRange{heaven.minimum,heaven.maximum};
                    });
            }
        }
        plan.finished=true;return plan;
    }
    if(m.program==Program::Meteor) {
        if(!expires) return plan;
        const auto &p=*m.skill.meteor; areaHit(m.position,p.radius);
        constexpr Vec offsets[]{{2,-2},{-2,-2},{0,2},{0,5},{-3,3},{0,3},{3,3},{-1,2},{1,1},{-1,-1},{2,-1},{-4,-2},{-3,-2},{-1,-3},{0,-4},{1,-3},{3,-3},{4,-2}};
        for(int i=0;i<18;i+=p.fireStep) {
            const Vec at=m.position+offsets[i]; if(!area.definition.collision.missileSegment(at,at,{5,1})) continue;
            child(at,{},p.fire.fireId,p.fire.fireFrames,0,Program::Fire,true,
                [](Missile &fire,const SkillCastSpec &current){fire.skill.firewall=current.meteor->fire;});
        }
        plan.finished=true; return plan;
    }
    if(m.program==Program::Fire || m.program==Program::Shard || m.program==Program::PoisonCloud) {
        if(expires) { plan.finished=true; return plan; }
        const int size=m.program==Program::Fire ? m.skill.firewall->size : m.collision.size;
        Vec next=m.position+m.velocity*TickContext::seconds;
        const auto wall=missileTerrainContact(area.definition.collision,m.position,next,m.collision);
        if(wall) next=m.position+(next-m.position)* *wall;
        if(m.nativeDamage) {
            std::vector<std::pair<float,EntityId>> contacts;
            for(const auto id:collisionCandidates(m,next)) {
                const auto target=participants.find(id);const auto point=unitPosition(m,id);
                if(point) if(const auto fraction=missileUnitIntersection(m.position,next,size,*point,target.size())) contacts.emplace_back(*fraction,id);
            }
            std::stable_sort(contacts.begin(),contacts.end(),[](const auto &a,const auto &b){return a.first<b.first;});
            plan.collisionFlight=Advance::CollisionFlight{m.position,next,std::move(contacts),0,bool(wall),true,false};
            continueMissileFlight(plan);return plan;
        }
        for(const auto id:nearbyUnits(m)) {
            const auto target=participants.find(id);const auto point=unitPosition(m,id);
            if(!point || !participants.canHarm(m.owner,id) ||
                ((m.program==Program::Shard || m.program==Program::PoisonCloud) && id==m.lastHit) ||
                !missileUnitIntersection(m.position,next,size,*point,target.size())) continue;
            int64_t amount{};
            if(!m.nativeDamage && m.program==Program::Fire) {
                const auto &p=*m.skill.firewall;
                amount=(int64_t(p.minimumDamage)+limitedRandom(m.random,uint32_t(p.maximumDamage-p.minimumDamage+1)))<<p.hitShift;
            } else if(!m.nativeDamage) amount=rollMissileDamage(fixedMissileDamageRange(m.skill.minimumDamage.raw(),m.skill.maximumDamage.raw()),m.random);
            plan.addImpact(impact(plan,{id},m.nativeDamage?std::nullopt:std::optional<int64_t>{amount}));
            if(m.program==Program::Shard || m.program==Program::PoisonCloud) {m.lastHit=id;break;}
        }
        m.position=next;if(wall) plan.finished=true;return plan;
    }
    if(const auto step=advanceMissileVelocity(m.velocity.length(),m.acceleration,m.maximumVelocity,m.ageFrames)) {
        m.acceleration=step->acceleration;
        m.velocity=m.velocity.unit()*step->speed;
    }
    const auto moved=moveMissile(m);const Vec next=moved.position;const bool wall=moved.blocked;
    if(wall || moved.pathFinished) --m.ageFrames;
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
    // Native SrvDo01 dispatches the same no-unit SrvHit on both lifetime
    // expiry and wall contact; keep area payload construction in one path.
    const auto terminalImpact=[&](Vec at) {
        if(m.skill.effect==SkillBehavior::GlacialSpike && m.skill.freezingArea) {
            if(owner) m.skill.freezingArea=skills::evaluate(*owner,m.skill.sourceId,m.skill.rank).freezingArea;
            areaHit(at,m.skill.freezingArea->radius);
        } else if(m.radius>0) areaHit(at,int(m.radius));
    };
    if(expires || moved.pathFinished) {terminalImpact(next);plan.finished=true;m.position=next;return plan;}
    auto contacts=wall || moved.pathFinished?std::vector<std::pair<float,EntityId>>{}:collisionContacts(m,next);
    if(m.skill.warcry) {
        for(const auto &[step,id]:contacts) {
            const auto recipient=participants.find(id);
            m.lastHit=id;
            if(recipient.damageable() && participants.auraAligned(participants.find(m.owner),recipient)) plan.addBuff(id);
        }
        m.position=next;++m.revision;if(wall || moved.pathFinished) plan.finished=true;return plan;
    }
    const bool holyBolt=m.skill.effect==SkillBehavior::HolyBolt;
    if(holyBolt && !contacts.empty()) {
        const auto id=contacts.front().second;const auto target=participants.find(id);
        const bool heal=m.skill.healingMaximum>0 && participants.petOrPartyAlly(participants.find(m.owner),target,ports_.social);
        if(!heal && (!target.monster || !target.monster->rule.undead)) contacts.clear();
    }
    const bool piercing=m.program==Program::Hammer || m.program==Program::FuryBolt || m.program==Program::Arc || (m.program==Program::Ring && m.skill.effect!=SkillBehavior::PoisonNova) || m.skill.effect==SkillBehavior::Inferno || m.skill.effect==SkillBehavior::BoneSpear;
    if(m.nativeDamage && !holyBolt && m.skill.effect!=SkillBehavior::Howl && m.skill.effect!=SkillBehavior::BattleCry) {
        plan.collisionFlight=Advance::CollisionFlight{m.position,next,std::move(contacts),0,bool(wall),true,false,piercing};
        continueMissileFlight(plan);return plan;
    }
    if(!contacts.empty()) {
        if(m.skill.effect==SkillBehavior::GlacialSpike && m.skill.freezingArea) {
            terminalImpact(next);plan.finished=true;
        } else if(m.radius>0) {
            m.position=next;
            areaHit(m.position,int(m.radius));plan.finished=true;
        } else {
            std::vector<EntityId> targets;
            for(const auto &[fraction,id]:contacts) {
                targets.push_back(id);m.lastHit=id;
                if(m.program==Program::Arc && (m.skill.effect==SkillBehavior::ChainLightning || (m.skill.weapon && m.skill.weapon->spear && m.skill.weapon->spear->kind==SpearSkillSpec::Kind::Strike))) {
                    const Vec at=next;std::vector<EntityId> eligible;
                    if(m.remainingHits>1) for(const auto candidate:nearbyUnits(m)) {
                        const auto point=unitPosition(m,candidate);
                        if(!point || !participants.canHarm(m.owner,candidate) || candidate==id || !missileHitAllowed(m,candidate) ||
                            !radius(at,*point,m.skill.arc->range) || !area.definition.collision.missileSegment(at,*point,{4,1})) continue;
                        eligible.push_back(candidate);
                    }
                    const auto successor=nativeMissileSuccessor(eligible,id,ports_.ids.nativeIdentities());
                    if(successor) {
                        auto &c=child(cell(at),*unitPosition(m,successor)-cell(at),m.definition,m.lifetimeFrames,m.skill.missileVelocity,Program::Arc,true);
                        c.remainingHits=m.remainingHits-1;c.lastHit=id;
                    }
                    plan.finished=true;break;
                }
                if(!piercing) {plan.finished=true;break;}
            }
            if(m.skill.effect==SkillBehavior::Howl || m.skill.effect==SkillBehavior::BattleCry) {for(const auto id:targets) plan.addCurse(id);}
            else for(const auto id:targets) plan.addImpact(impact(plan,{id}));
        }
    } else if(wall) terminalImpact(next);
    m.position=next; ++m.revision; if(wall) plan.finished=true; return plan;
}
void System::areaImpact(Advance &plan,Vec at,int range,bool full,std::optional<uint32_t> filter,std::optional<int32_t> length) const {
    const auto &m=plan.next;const auto &area=ports_.areas.at(m.area);
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    if(!participants.find(m.owner).id()) return;
    auto targets=ports_.effects.nativeAuraCandidateRecipients({m.owner,unitArea(m.owner),range,filter.value_or(m.environment?0x583u:0x8583u),false,
        at+area.definition.origin,effects::NativeAuraTargetMode::TargetRange,m.executionTick});
    // Preserve room-list traversal across adjacent regions. Split only at a
    // region transition; grouping by region would reorder synchronous hits.
    std::vector<std::pair<RegionId,std::vector<EntityId>>> runs;
    for(const auto id:targets) {
        const auto unit=participants.find(id);
        if(m.environment && unit.monster && ports_.monsters.find(m.owner) && !unit.monster->combatCompanion()) continue;
        const auto region=unit.player?unit.player->area:unit.monster->area;
        if(runs.empty() || runs.back().first!=region) runs.emplace_back(region,std::vector<EntityId>{});
        runs.back().second.push_back(id);
    }
    if(runs.empty()) runs.emplace_back(m.area,std::vector<EntityId>{});
    const auto &rule=m.nativeDamage->rule;
    combat::MissileAreaDamage numeric{rule.damageElement,full || m.program==Program::Meteor ||
        (m.enemy && m.enemy->rule.behavior==MissileRule::Behavior::Meteor),{},std::make_shared<combat::MissileAreaRoll>()};
    const auto &program=rule.damageFormulas->missiles.at(m.definition);
    numeric.hitFlags=uint32_t(program.number("hitflags"));numeric.resultFlags=uint16_t(program.number("resultflags"));
    if(m.enemy && m.enemy->rule.behavior==MissileRule::Behavior::GlacialSpike) numeric.length=int32_t(m.enemy->rule.coldFrames);
    else if(m.skill.effect==SkillBehavior::GlacialSpike && m.skill.freezingArea) numeric.length=m.skill.freezingArea->freezeFrames;
    if(length) numeric.length=length;
    // The native area callback ignores a zero result word. GetDamageValue
    // still ran once before enumeration, including its private seed draws.
    if(!numeric.resultFlags) {runs.clear();runs.emplace_back(m.area,std::vector<EntityId>{});}
    for(auto &[region,recipients]:runs) {
        auto hit=m.enemy?enemyImpact(plan,std::move(recipients)):impact(plan,std::move(recipients));
        hit.area=region;hit.areaDamage=numeric;
        hit.weapon.reset();
        hit.monsterToHit=false;hit.returnFire=false;hit.nativeNextHit.reset();hit.nextDelay=0;
        hit.occurrence=(uint64_t(m.ageFrames)<<32)|(uint64_t{1}<<31)|plan.impacts.size();
        plan.addImpact(std::move(hit));
    }
}
bool System::projectileImpact(Advance &plan,EntityId target) const {
    auto &m=plan.next;
    const auto *owner=ports_.players.findActor(m.owner);
    if(m.skill.effect==SkillBehavior::GlacialSpike && m.skill.freezingArea) {
        if(owner) m.skill.freezingArea=skills::evaluate(*owner,m.skill.sourceId,m.skill.rank).freezingArea;
        areaImpact(plan,m.position,m.skill.freezingArea->radius);plan.collisionFlight->continueFlight=false;return false;
    }
    if(m.radius>0) {areaImpact(plan,m.position,int(m.radius));plan.collisionFlight->continueFlight=false;return false;}
    if(target && m.program==Program::Arc && (m.skill.effect==SkillBehavior::ChainLightning ||
        (m.skill.weapon && m.skill.weapon->spear && m.skill.weapon->spear->kind==SpearSkillSpec::Kind::Strike))) {
        const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
        const auto &area=ports_.areas.at(m.area);std::vector<EntityId> eligible;
        if(m.remainingHits>1) for(const auto id:nearbyUnits(m)) {
            const auto point=unitPosition(m,id);
            if(point && participants.canHarm(m.owner,id) && id!=target && missileHitAllowed(m,id) &&
                radius(m.position,*point,m.skill.arc->range) && area.definition.collision.missileSegment(m.position,*point,{4,1})) eligible.push_back(id);
        }
        const auto next=nativeMissileSuccessor(eligible,target,ports_.ids.nativeIdentities());
        if(next) {
            Spawn source{{m.player,m.playerActor,m.area,m.generation,0,m.created},m.skill,m.collision,{},true,m.emitter,m.emitterType,{}};
            auto &child=planChild(plan,source,cell(m.position),*unitPosition(m,next)-cell(m.position),m.definition,
                m.lifetimeFrames,m.skill.missileVelocity,Program::Arc,bool(owner));
            child.remainingHits=m.remainingHits-1;child.lastHit=target;
        }
        plan.collisionFlight->continueFlight=false;
    }
    return true;
}

}
