#include "system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/monsters/projectile_math.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "gameplay/monsters/melee_decision.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace d2x::server::monsters {
bool System::canNativeTeleport(EntityId id,Vec point,const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) const {
    const auto *unit=find(id);if(!unit || !unit->alive() || !collision) return false;
    const auto &area=ports_.areas.at(unit->area).definition;const auto global=point+area.origin;
    const int x=int(std::floor(global.x)),y=int(std::floor(global.y));
    const auto room=ports_.areas.roomAtPosition(unit->area,unit->position,x,y);
    return room && !ports_.areas.at(room->area).definition.town &&
        !ports_.areas.nativeCollisionMask(*room,x,y,unit->rule.size,0x3c01,collision) &&
        area.collision.segment(unit->position,point,{}, {0xc01,1});
}
DomainResult<> System::nativeTeleport(EntityId id,Vec point,uint64_t tick,const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end() || !found->second.alive() || !collision) return {DomainStatus::InvalidActor,{}};
    auto &unit=found->second;if(unit.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    const auto global=point+ports_.areas.at(unit.area).definition.origin;
    const int x=int(std::floor(global.x)),y=int(std::floor(global.y));
    auto room=ports_.areas.roomAtPosition(unit.area,unit.position,x,y);
    if(!room) if(const auto *owner=find(minionOwner(id))) {
        const auto &area=ports_.areas.at(owner->area).definition;
        if(const auto *bounds=area.activation.room(owner->position))
            room=RoomView{owner->area,(int(area.origin.x)+bounds->x)/5,(int(area.origin.y)+bounds->y)/5};
    }
    if(!room) return {DomainStatus::Unavailable,{}};
    const auto landing=ports_.areas.nativeFreeCoordinates(*room,x,y,unit.rule.size,0x1c09,collision);
    if(!landing) return {DomainStatus::Unavailable,{}};
    (void)tick;stop(id);unit.area=landing->room.area;unit.position=landing->position+Vec{.5f,.5f};
    ports_.areas.moveRoomUnit(id,unit.area,unit.position);refreshCollision(unit);++unit.revision;
    return {DomainStatus::Applied,std::monostate{}};
}
bool System::canLand(EntityId source) const {
    const auto *actor=find(source);if(!actor || actor->life<=0) return false;
    if(!ports_.areas.at(actor->area).definition.collision.walkable(actor->position,actor->rule.spawnCollision)) return false;
    for(const auto &[id,other]:state_.actors) if(id!=source && other.life>0 && !other.burrowed && !other.airborne && !other.submerged && !other.hatching && other.area==actor->area && meleeDistance(actor->position,actor->rule.size,other.position,other.rule.size)<=0) return false;
    for(const auto &[id,player]:ports_.players.all()) {
        (void)id;if(player.entered && player.area==actor->area && player.persistent.player.hp>0 && meleeDistance(actor->position,actor->rule.size,player.position,2)<=0) return false;
    }
    return true;
}
void System::monsterTargetable(EntityId id,bool enabled) {
    if(const auto found=state_.actors.find(id);found!=state_.actors.end() && found->second.targetFlagsDisabled==enabled) {
        found->second.targetFlagsDisabled=!enabled;++found->second.revision;
    }
}
void System::submerge(EntityId id) {
    auto &actor=state_.actors.at(id);actor.submerged=actor.targetFlagsDisabled=true;
    actor.collisionPattern=NativeCollisionPattern::SmallNoPresence;
    actor.aiPose=8;actor.holdPose=true;++actor.revision;refreshCollision(actor);
}
DomainResult<bool> System::restoreGroundPresence(EntityId source,const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision) {
    const auto found=state_.actors.find(source);if(found==state_.actors.end() || !found->second.alive()) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;if(actor.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
    // Native landing restores target flags before testing its ground presence.
    actor.targetFlagsDisabled=false;++actor.revision;
    if(!actor.airborne && !actor.submerged) return {DomainStatus::Applied,false};
    const auto &area=ports_.areas.at(actor.area).definition;const auto point=actor.position+area.origin;
    const auto room=ports_.areas.roomAtPosition(actor.area,actor.position,int(point.x),int(point.y));
    if(!room) return {DomainStatus::Applied,false};
    const auto landing=ports_.areas.nativeFreeCoordinates(*room,int(point.x),int(point.y),actor.rule.size,0x1c09,collision);
    if(!landing) return {DomainStatus::Applied,false};
    actor.area=landing->room.area;actor.position=landing->position+Vec{.5f,.5f};actor.airborne=actor.submerged=false;
    actor.collisionPattern=NativeCollisionPattern::SmallUnit;
    stop(source);ports_.areas.moveRoomUnit(actor.id,actor.area,actor.position);refreshCollision(actor);
    return {DomainStatus::Applied,true};
}
DomainResult<> System::aiPose(EntityId source,uint8_t mode,bool hold,bool airborne,uint64_t tick,int aiWait,bool preserveMovement,std::optional<uint8_t> direction) {
    auto found=state_.actors.find(source);if(found==state_.actors.end() || !found->second.alive()) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;const auto clock=actor.rule.aiPoseTicks.find(mode);
    if(mode!=1 && clock==actor.rule.aiPoseTicks.end()) return {DomainStatus::Unavailable,{}};
    if(actor.airborne && !airborne && !canLand(source)) return {DomainStatus::Unavailable,{}};
    recordNativeMode(actor,mode);
    actor.aiPose=mode;actor.holdPose=hold;actor.airborne=airborne;actor.poseUntil=tick+uint64_t(mode==1?0:clock->second);
    actor.poseDirection=direction;
    actor.targetFlagsDisabled=airborne;refreshCollision(actor);
    if(!airborne) {if(!preserveMovement) stop(source);actor.busyUntil=std::max(actor.busyUntil,aiWait>0?tick+uint64_t(aiWait):actor.poseUntil);}
    ++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::aiColor(EntityId source,int color,int overlay,uint64_t tick) {
    auto found=state_.actors.find(source);if(found==state_.actors.end() || found->second.life<=0) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;
    if(color<0 || color>2 || actor.rule.blueState<0 || actor.rule.redState<0 || overlay>UINT16_MAX) return {DomainStatus::InvalidRequest,{}};
    auto states=actor.aiStates;states.erase(actor.rule.blueState);states.erase(actor.rule.redState);
    if(color) states.insert(color==1?actor.rule.blueState:actor.rule.redState);
    std::vector<DomainFact> facts;
    for(int state:actor.aiStates) if(!states.contains(state)) facts.emplace_back(StateFact{source,1,actor.area,state,false});
    for(int state:states) if(!actor.aiStates.contains(state)) facts.emplace_back(StateFact{source,1,actor.area,state,true});
    if(overlay>=0) facts.emplace_back(OverlayFact{source,1,actor.area,overlay});
    if(facts.empty()) return {DomainStatus::Applied,std::monostate{}};
    const auto published=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},std::move(facts)});
    if(!published) return {published.status,{}};
    actor.aiStates=std::move(states);++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
void System::aiRegeneration(EntityId source,int eighths) {
    auto found=state_.actors.find(source);if(found==state_.actors.end() || eighths<0 || eighths>65535) return;
    auto &actor=found->second;const int64_t bonus=(actor.maximumLife*actor.rule.damageRegen/4096)*eighths/8;
    if(actor.aiRegenerationBonus!=bonus) {actor.aiRegenerationBonus=bonus;++actor.revision;}
}
int System::nativeAiRegeneration(EntityId source,int eighths,int previousBonus,uint64_t tick) {
    auto &actor=state_.actors.at(source);
    attributes::MonsterResourceTrace resources(actor,tick);
    const auto sources=ports_.effects.unitStatSources(source,tick);resources.sampleSources(sources);
    const int rate=nativeStatValue(resources.values(sources),*actor.rule.stats,"hpregen");
    const int bonus=eighths?nativeMultiply(rate,eighths)/8:previousBonus;
    resources.writeBaseStat("hpregen",eighths?nativeAdd(rate,bonus):nativeSubtract(rate,bonus),sources);
    auto changed=resources.take();actor.nativeStats=std::move(changed.intrinsic);
    actor.life=changed.life.raw();actor.maximumLife=changed.maximum.raw();++actor.revision;
    return bonus;
}
void System::beginHatch(EntityId id) {
    auto &actor=state_.actors.at(id);if(!actor.hatching) {actor.hatching=true;++actor.revision;refreshCollision(actor);}
}
bool System::mummySupportTarget(EntityId source,EntityId target,uint64_t tick,bool dead,bool healthy) const {
    const auto *caster=find(source),*other=find(target);
    if(!caster || !other || source==target || caster->life<=0 || caster->rule.ai.kind!=MonsterAiKind::GreaterMummy ||
       other->area!=caster->area || other->targetFlagsDisabled || other->hatching ||
       !sameAlignment(caster->alignment(),other->alignment()) || other->alignment()==UnitAlignment::Good) return false;
    // Fn022: ordinary greater mummies support lUndead; Radament supports both
    // undead classes and accepts unique corpses only on Normal difficulty.
    const bool radament=caster->identity.monster=="radament";
    if(radament?!other->rule.undead:!other->rule.lesserUndead) return false;
    if(!(radament && caster->rule.difficulty==0) && (other->identity.rank==MonsterRank::Unique ||
       other->identity.rank==MonsterRank::SuperUnique || other->identity.rank==MonsterRank::Champion)) return false;
    const int radius=caster->rule.ai.params[4]+(radament?10:0);
    const Vec delta{float(int(caster->position.x)-int(other->position.x)),float(int(caster->position.y)-int(other->position.y))};
    if(delta.x*delta.x+delta.y*delta.y>radius*radius || !ports_.areas.at(caster->area).definition.activation.nearby(caster->position,other->position)) return false;
    for(const auto state:ports_.effects.unitStates(target,tick))
        if(other->rule.states->at(state).corpseUnselectable) return false;
    if(healthy) return true; // The native counter includes healthy and dying units.
    if(dead) {
        for(const auto state:ports_.effects.unitStates(target,tick))
            if(other->rule.states->at(state).hideOnDeath) return false;
        return usableCorpse(target,caster->area,tick);
    }
    return other->alive() && other->life!=other->maximumLife;
}
DomainResult<> System::spawnSkillChildren(EntityId source,Vec target,int count,bool hatch,uint64_t tick,std::shared_ptr<SkillChildrenExecution> &retained) {
    if(!retained) retained=std::make_shared<SkillChildrenExecution>();
    auto &execution=*retained;
    if(execution.complete) return {DomainStatus::Applied,std::monostate{}};
    auto found=state_.actors.find(source);
    if(found==state_.actors.end() || (!execution.published && !found->second.alive()) || !found->second.rule.nestChild) return {DomainStatus::InvalidActor,{}};
    if(!execution.published) {
        auto parent=found->second;const auto &child=*parent.rule.nestChild;
        const auto capacity=std::max(count,0);
        if(size_t(capacity)>65536-state_.actors.size() || uint64_t(capacity)>UINT32_MAX-ports_.ids.cursor() || parent.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        auto gameRandom=ports_.random;ComponentPaletteState palettes;std::map<EntityId,Actor> candidates;
        std::set<size_t> limitedUniques;
        const auto &grid=ports_.areas.at(parent.area).definition.collision;
        constexpr std::array directionIndices{10,8,22,20,18,16,14,12};
        const Vec origin=hatch?parent.position:parent.rule.ai.kind==MonsterAiKind::SandMaggotQueen?
            parent.position+parent.rule.spawnOffset:parent.position+monsterDirectionOffset(directionIndices[size_t(monsterFacing8(parent.position,target))]);
        int born=0;DomainStatus birthStatus=DomainStatus::Applied;size_t allocated=0;
        const auto place=[&](Vec point) {
            if(birthStatus!=DomainStatus::Applied) return false;
            if(!grid.walkable(point,child.rule.spawnCollision)) return false;
            bool occupied=false;
            for(const auto &[id,other]:state_.actors) if(id!=source && other.area==parent.area && other.life>0 && !other.burrowed && !other.airborne && !other.submerged && !other.hatching && meleeDistance(point,child.rule.size,other.position,other.rule.size)<=0) {occupied=true;break;}
            for(const auto &[id,other]:candidates) if(meleeDistance(point,child.rule.size,other.position,other.rule.size)<=0) {occupied=true;break;}
            for(const auto &[id,p]:ports_.players.all()) {(void)id;if(p.entered && p.area==parent.area && p.persistent.player.hp>0 && meleeDistance(point,child.rule.size,p.position,2)<=0) occupied=true;}
            if(occupied) return false;
            auto identity=child.identity;identity.spawnKey=parent.identity.spawnKey+".skill-child."+std::to_string(parent.nestSpawned);identity.ownerSpawnKey=parent.identity.spawnKey;
            auto rule=child.rule;rule.noExperience=true;
            if(allocated>=UINT32_MAX-ports_.ids.cursor()) {birthStatus=DomainStatus::Capacity;return false;}
            const EntityId id{ports_.ids.cursor()+allocated};
            auto prepared=prepareAdmissionActor({identity,child.implementation,parent.area,point,true,std::move(rule),{},tick},id,gameRandom,palettes,0,&limitedUniques);
            if(!prepared) {birthStatus=prepared.status;return false;}
            auto actor=std::move(*prepared.value);allocated+=1+actor.birthEquipmentItems;
            limitedUniques.insert(actor.birthLimitedUniques.begin(),actor.birthLimitedUniques.end());
            actor.riseMode=parent.rule.spawnMode;actor.riseUntil=actor.busyUntil=tick+uint64_t(parent.rule.spawnTicks);
            candidates.emplace(id,std::move(actor));++parent.nestSpawned;++born;
            return true;
        };
        if(count>0) place(origin);
        if(hatch) {
            // SrvDo084 first probes the origin. Subsequent SpawnNormalMonster
            // calls use search limits 1..4, translated to radii 3,6,9,12.
            // Only a failed complete search raises the limit; at most 2*count
            // calls follow the initial probe. Keep the parent's random advances.
            auto random=parent.combatRandom;
            int searchLimit=1;
            for(int64_t attempt=0;attempt<int64_t{2}*count && born<count && searchLimit<5;++attempt) {
                bool placed=false;
                for(int radius=3;radius<=3*searchLimit && !placed;radius+=3) {
                    const auto start=monsterWanderPoint(origin,radius,random)-origin;
                    int x=int(start.x),y=int(start.y);
                    for(int probe=0;probe<8*radius && !placed;++probe) {
                        if(y==-radius && x<radius) ++x;
                        else if(x==radius && y<radius) ++y;
                        else if(y==radius && x>-radius) --x;
                        else --y;
                        placed=place(origin+Vec{float(x),float(y)});
                    }
                }
                if(!placed) ++searchLimit;
            }
            parent.combatRandom=random;
        }
        if(birthStatus!=DomainStatus::Applied) return {birthStatus,{}};
        for(const auto &[id,actor]:candidates) execution.children.push_back(id);
        ++parent.revision;candidates.emplace(source,std::move(parent));
        auto publication=prepareActorPublication(std::move(candidates),allocated);
        if(!validateActors(publication)) {execution.children.clear();return {DomainStatus::Conflict,{}};}
        publishComponents(palettes);publishActors(publication);ports_.random=gameRandom;execution.published=true;
    }
    while(execution.nextAura<execution.children.size()) {
        const auto id=execution.children[execution.nextAura];const auto *child=find(id);
        if(child && child->rule.enchantment && child->rule.enchantment->aura && !child->rightSkill) {
            const auto aura=*child->rule.enchantment->aura;const auto result=ports_.effects.assignUnitAura(id,aura,tick,aura.rank);
            if(!result) return result;
        }
        ++execution.nextAura;
    }
    if(hatch) {
        // The hatch is terminal even if some larvae cannot be placed.
        const auto *parent=find(source);
        const auto result=parent->alive()?scriptDeath(source,tick):ports_.effects.monsterDeathEffect(source,tick);
        if(!result) return result;
        auto &dead=state_.actors.at(source);dead.rewardComplete=true;dead.corpse.unavailable=true;
    }
    execution.complete=true;
    return hatch || !execution.children.empty()?DomainResult<>{DomainStatus::Applied,std::monostate{}}:DomainResult<>{DomainStatus::Unavailable,{}};
}
DomainResult<> System::burrow(EntityId source,bool below,uint64_t tick) {
    auto found=state_.actors.find(source);if(found==state_.actors.end() || found->second.life<=0) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;
    if(!below) {
        const auto &grid=ports_.areas.at(actor.area).definition.collision;
        if(!grid.walkable(actor.position,actor.rule.spawnCollision)) return {DomainStatus::Unavailable,{}};
        for(const auto &[id,other]:state_.actors) if(id!=source && other.life>0 && !other.burrowed && !other.airborne && !other.submerged && !other.hatching && other.area==actor.area && meleeDistance(actor.position,actor.rule.size,other.position,other.rule.size)<=0) return {DomainStatus::Unavailable,{}};
        for(const auto &[id,p]:ports_.players.all()) {(void)id;if(p.entered && p.area==actor.area && p.persistent.player.hp>0 && meleeDistance(actor.position,actor.rule.size,p.position,2)<=0) return {DomainStatus::Unavailable,{}};}
    }
    (void)tick;stop(source);actor.burrowed=below;refreshCollision(actor);
    ++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::startSkillMotion(EntityId source,Vec target,uint64_t until,int skill,bool leap,uint64_t tick,EntityId victim) {
    auto found=state_.actors.find(source);if(found==state_.actors.end() || !found->second.alive() || until<=tick) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;const auto &grid=ports_.areas.at(actor.area).definition.collision;
    const auto occupied=[&](Vec point) {
        for(const auto &[id,other]:state_.actors) if(id!=source && other.area==actor.area && other.life>0 && !other.burrowed && !other.airborne && !other.submerged && !other.hatching && meleeDistance(point,actor.rule.size,other.position,other.rule.size)<=0) return true;
        for(const auto &[id,p]:ports_.players.all()) {(void)id;if(p.entered && p.area==actor.area && p.persistent.player.hp>0 && meleeDistance(point,actor.rule.size,p.position,2)<=0) return true;}
        return false;
    };
    const auto free=[&](Vec point) {return grid.walkable(point,actor.rule.spawnCollision) && !occupied(point);};
    if(leap && !actor.aiStates.contains(actor.rule.skillMoveState)) {
        const auto landing=leapLanding(source,target);if(!landing) return {DomainStatus::Unavailable,{}};
        target=*landing;
    } else if(!leap && !free(target)) return {DomainStatus::Unavailable,{}};
    if(!leap && !ports_.spatial.navigationGrid(actor.area,actor.id).nativeMovementSegment(
        actor.position+Vec{.5f,.5f},target+Vec{.5f,.5f},actor.rule.collision)) return {DomainStatus::Unavailable,{}};
    if(!actor.rule.formulas || !actor.rule.stats || actor.rule.nativeRunVelocity<=0) return {DomainStatus::Unavailable,{}};
    auto velocity=nativeShiftLeft(actor.rule.nativeRunVelocity,8);
    if(!leap) {
        const auto sampled=ports_.effects.unitStatValues(source,tick);if(!sampled) return {DomainStatus::Unavailable,{}};
        const auto percent=std::max(50,logicalStatValue(*sampled,*actor.rule.stats,"velocitypercent"));
        velocity=nativePercentage(velocity,nativeAdd(actor.rule.formulas->skills.at(skill).number("param1"),percent),100);
    }
    const float speed=float(velocity)*25.f/4096.f;
    if(speed<=0) return {DomainStatus::Unavailable,{}};
    if(actor.rule.skillMoveState>=0 && !actor.aiStates.contains(actor.rule.skillMoveState) && !ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},{StateFact{source,1,actor.area,actor.rule.skillMoveState,true}}})) return {DomainStatus::Capacity,{}};
    until=std::max(until,tick+uint64_t(std::ceil((target-actor.position).length()/speed/TickContext::seconds))+1);
    actor.skillMotion=Actor::SkillMotion{target,leap?UINT64_MAX:until,actor.interruption,speed,leap,{}};
    actor.pathStep.reset();
    actor.skillMotion->target=victim;actor.skillMotion->moveState=actor.rule.skillMoveState>=0 && !actor.aiStates.contains(actor.rule.skillMoveState);
    actor.busyUntil=std::max(actor.busyUntil,until);actor.movementGoal=target;
    if(actor.neutralAt) actor.neutralAt=actor.busyUntil;
    ++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::startSwarmMove(EntityId source,Vec target,uint64_t tick) {
    auto found=state_.actors.find(source);if(found==state_.actors.end() || found->second.life<=0) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;const auto &grid=ports_.areas.at(actor.area).definition.collision;
    auto path=grid.path(actor.position,target,true,actor.rule.collision);
    if(path.empty()) return {DomainStatus::Unavailable,{}};
    std::deque<Vec> route;Vec previous=actor.position;float remaining=5.f;
    for(Vec point:path) {
        const Vec delta=point-previous;const float distance=delta.length();
        if(distance>remaining) {route.push_back(previous+delta.unit()*remaining);break;}
        route.push_back(point);remaining-=distance;previous=point;if(remaining<=0) break;
    }
    const float speed=monsterMovementSpeed(actor.rule.nativeVelocity,actor.velocityPercent+(actor.chilledUntil>tick?actor.rule.coldEffect:0));if(route.empty() || speed<=0) return {DomainStatus::Unavailable,{}};
    const auto until=tick+uint64_t(std::ceil(5.f/speed/TickContext::seconds))+1;
    actor.skillMotion=Actor::SkillMotion{route.back(),until,actor.interruption,speed,false,std::move(route)};
    actor.busyUntil=std::max(actor.busyUntil,until);if(actor.neutralAt) actor.neutralAt=actor.busyUntil;
    ++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::settleDelayedDeath(EntityId id,uint64_t tick) {
    const auto *actor=find(id);
    if(!actor || !actor->delayedDeathSource || ports_.effects.unitStates(id,tick).contains(actor->rule.uninterruptableState)) return {DomainStatus::Applied,std::monostate{}};
    const auto result=damage(id,actor->killer,0,tick,0,false,0,{},true,0,{},0,false,0,0,true,{},false,{*actor->delayedDeathSource});
    return {result.status,result?std::optional{std::monostate{}}:std::nullopt};
}
DomainResult<> System::startWhirlwindMotion(EntityId source,Vec target) {
    const auto found=state_.actors.find(source);if(found==state_.actors.end() || !found->second.alive()) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;const Vec center{.5f,.5f};
    const auto grid=ports_.spatial.navigationGrid(actor.area,actor.id);
    auto route=grid.nativeUnitPath(actor.position+center,target+center,7,100,{0xc01,actor.rule.size});
    if(route.empty()) return {DomainStatus::Unavailable,{}};
    for(auto &point:route) point=point-center;
    actor.skillReservation=Actor::SkillReservation{route.back()};
    ports_.areas.setNativePatternMask(actor.area,route.back(),actor.rule.collisionPattern,0x100,true);
    actor.skillMotion=Actor::SkillMotion{route.back(),UINT64_MAX,actor.interruption,monsterMovementSpeed(actor.rule.nativeVelocity,100),false,std::move(route)};
    actor.skillMotion->eventDriven=true;actor.skillMotion->collision=MovementCollisionRule{0x401,actor.rule.size};
    actor.pathStep.reset();actor.movementTarget={};actor.movementGoal=target;actor.pathType=7;actor.pathDistance=100;actor.pathSteps=1;
    ++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::startDiabloRun(EntityId source,Vec target,int32_t velocity) {
    const auto found=state_.actors.find(source);
    if(found==state_.actors.end() || !found->second.alive() || velocity<0) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;const Vec center{.5f,.5f};
    const auto grid=ports_.spatial.navigationGrid(actor.area,actor.id);
    auto route=grid.nativeUnitPath(actor.position+center,target+center,1,5,actor.rule.collision);
    for(auto &point:route) point=point-center;
    // St54 captured coordinates and cleared pTargetUnit. Do103 must not
    // pursue the target's new position, use ordinary run speed, or apply 75%.
    actor.skillMotion=Actor::SkillMotion{route.empty()?actor.position:route.back(),UINT64_MAX,actor.interruption,
        float(velocity)*25.f/4096.f,false,std::move(route)};
    actor.skillMotion->eventDriven=true;actor.pathStep.reset();actor.movementTarget={};actor.movementGoal=target;
    ++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
bool System::advanceScriptedMotion(EntityId source,uint64_t tick) {
    const auto found=state_.actors.find(source);if(found==state_.actors.end()) return false;
    auto &actor=found->second;
    const bool moving=actor.skillMotion && actor.skillMotion->eventDriven && advanceSkillMotion(actor,tick);
    if(!moving) {actor.moving=false;++actor.revision;}
    return moving;
}
void System::stopScriptedMotion(EntityId source) {
    const auto found=state_.actors.find(source);if(found==state_.actors.end()) return;
    auto &actor=found->second;
    if(actor.pd2DashAbsent) {actor.pd2DashAbsent=false;actor.pathType=2;refreshCollision(actor);++actor.revision;}
    if(actor.skillReservation) {
        ports_.areas.setNativePatternMask(actor.area,actor.skillReservation->position,actor.rule.collisionPattern,actor.skillReservation->clearMask,false);
        actor.skillReservation.reset();++actor.revision;
    }
    if(actor.skillMotion && actor.skillMotion->eventDriven && actor.skillMotion->leap) {actor.collisionPattern.reset();refreshCollision(actor);}
    if(!actor.skillMotion || !actor.skillMotion->eventDriven) return;
    actor.skillMotion.reset();actor.pathStep.reset();actor.moving=false;++actor.revision;
}
bool System::advanceSkillMotion(Actor &actor,uint64_t tick) {
    auto &motion=*actor.skillMotion;
    const bool interrupted=!actor.alive() || actor.interruption!=motion.interruption || actor.frozenUntil>tick || actor.stunnedUntil>tick || actor.knockedUntil>tick;
    if(!interrupted) {
        if(motion.target) {
            const auto target=targetPosition(motion.target,actor.area);
            if(target && combat::Participants{ports_.players,*this,ports_.social}.canHarm(actor.id,motion.target)) {
                const Vec delta=target->first-actor.position;
                const float stop=float(actor.rule.meleeRange+3)+.5f;
                motion.destination=actor.position+delta.unit()*std::max(0.f,delta.length()-stop);
            } else motion.until=tick;
        }
        while(!motion.route.empty() && (motion.route.front()-actor.position).length()<.01f) motion.route.pop_front();
        if(tick<motion.until) {
            AreaStore::CollisionMove collisionMove{ports_.areas,actor.id,actor.area};
            const Vec center{.5f,.5f};const auto grid=ports_.spatial.movementGrid(actor.area,collisionMove);
            const auto step=actor.pathStep.advance(grid,actor.position+center,(motion.route.empty()?motion.destination:motion.route.front())+center,
                motion.speed*TickContext::seconds,motion.collision.value_or(motion.leap?MovementCollisionRule{0,1}:actor.rule.collision));
            actor.position=step.position-center;ports_.areas.moveRoomUnit(actor.id,actor.area,actor.position);refreshCollision(actor);
            collisionMove.commit();
            actor.moving=step.travelled>0;++actor.revision;if(step.blocked) motion.until=tick;
        } else motion.until=tick;
        if((motion.destination-actor.position).length()>.01f && tick<motion.until) return true;
    }
    if(motion.moveState && actor.rule.skillMoveState>=0 && !ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},{StateFact{actor.id,1,actor.area,actor.rule.skillMoveState,false}}})) return true;
    if(motion.leap && motion.eventDriven) {actor.collisionPattern.reset();actor.pathType=2;refreshCollision(actor);}
    actor.skillMotion.reset();++actor.revision;return false;
}
}
