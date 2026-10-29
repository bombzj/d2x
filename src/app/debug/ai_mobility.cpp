#include "ai_impl.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
bool DebugAi::Impl::knownSkill(const Context &c,uint16_t id,bool left) const {
    return std::any_of(c.combat.skills().begin(),c.combat.skills().end(),[&](const auto &s){
        return s.id==id && !s.passive && (s.level || s.innate) && (!left || s.left);
    });
}
bool DebugAi::Impl::selectSkill(Context &c,uint16_t skill,OnlineSkillHand hand) {
    const auto &world=c.session.read().world;
    const auto selected=hand==OnlineSkillHand::Left?world.leftSkill:world.rightSkill;
    if(selected && selected->skill==skill && selected->owner==UINT32_MAX) return true;
    const auto now=Clock::now();
    if((world.combatRequest && world.combatRequest->state==OnlineCombatRequest::State::Pending) ||
        now-lastSelection<std::chrono::milliseconds(600)) return false;
    lastSelection=now;OnlineCombatCommand request;request.action=OnlineCombatCommand::Action::SelectSkill;
    request.hand=hand;request.skill=skill;
    if(!c.combat.submit(request)) reason=c.combat.reason();
    return false; // A queued selection is never a confirmed hand assignment.
}
std::optional<uint16_t> DebugAi::Impl::battleAura(const Context &c) const {
    if(policy.aura) return policy.aura;
    for(const auto name:{"Fanaticism","Concentration","Might"}) {
        const auto skill=definitions->skill(name);if(knownSkill(c,skill)) return skill;
    }
    return {};
}
bool DebugAi::Impl::mobility(Context &c) {
    const auto now=Clock::now();const auto &world=c.session.read().world;
    if(mobilityTarget) {
        // Observe the normal path's relocation/progress. Casting acceptance
        // never writes coordinates or proves arrival at the intended endpoint.
        const Vec target{float(mobilityTarget->x),float(mobilityTarget->y)};
        const auto player=world.units.find({0,c.session.read().load.playerUnitId.value_or(0)});
        const bool progress=mobilityTeleport?player!=world.units.end() && player->second.positionDiscontinuity!=mobilityDiscontinuity:
            (c.position-target).length()<3 || (c.position-mobilityFrom).length()>3;
        if(progress) {
            note("mobility-progress-observed");mobilityTarget.reset();nextMobility=now;
            return false;
        }
        if(now>=mobilityDeadline) {
            note("mobility-no-progress","Falling back to ordinary walking");
            mobilityTarget.reset();nextMobility=now+std::chrono::seconds(3);return false;
        }
        mobilityHold=true;wait("mobility-wait","Waiting for normal skill movement/position feedback");return true;
    }
    if(combatTarget || !c.walk.active() || now<nextMobility || world.npcRequested || world.waypointSource ||
        portalUsed || travelPhase==TravelPhase::WaypointWait || travelPhase==TravelPhase::CrossWait) return false;
    const auto child=c.walk.snapshot();const auto &target=child.at("target");
    if(target.is_null()) return false;
    const Vec wanted{target.at("x").get<float>(),target.at("y").get<float>()};
    Vec delta=wanted-c.position;float distance=delta.length();
    if(distance<8) return false; // Doors, items and NPCs retain normal close approach.
    const auto teleport=definitions->skill("Teleport"),charge=definitions->skill("Charge"),vigor=definitions->skill("Vigor");
    bool teleportPermitted=false;
    for(size_t row=0;row<definitions->levels.rows().size();++row)
        if(definitions->levels.number(row,"Id")==area) teleportPermitted=definitions->levels.number(row,"Teleport").value_or(0)!=0;
    const bool canTeleport=teleportPermitted && !c.town.read().town && knownSkill(c,teleport) &&
        (mobilityPolicy.mode=="auto" || mobilityPolicy.mode=="teleport");
    const bool canCharge=!canTeleport && !c.town.read().town && knownSkill(c,charge,true) &&
        (mobilityPolicy.mode=="auto" || mobilityPolicy.mode=="charge");
    const bool useVigor=mobilityPolicy.vigor && !canTeleport && knownSkill(c,vigor);
    if(useVigor && !selectSkill(c,vigor,OnlineSkillHand::Right)) {
        mobilityHold=true;wait("mobility-select","Waiting for normal Vigor selection");return true;
    }
    if(!canTeleport && !canCharge) return false;
    const OnlinePoint goal{uint16_t(wanted.x),uint16_t(wanted.y)};
    if(!mobilityGoal || *mobilityGoal!=goal) {
        mobilityGoal=goal;mobilityBestDistance=distance;mobilityProgressAt=now;
    } else if(distance+2<mobilityBestDistance) {
        mobilityBestDistance=distance;mobilityProgressAt=now;
    } else if(now-mobilityProgressAt>=std::chrono::seconds(8)) {
        // A relocation alone is not progress toward this stable child goal.
        // Repeated landings in a wall pocket must release input ownership so
        // ordinary native walking can follow its route around the obstruction.
        nextMobility=now+std::chrono::seconds(15);mobilityGoal.reset();
        if(child.value("interactionRequests",0u)==0 && walkIntent.is_object()) {
            c.walk.command(walkIntent,c.session,c.town,c.control,c.combat);++moveGoals;
        }
        note("mobility-route-stalled","No closer approach for 8 seconds; allowing 15 seconds of ordinary walking");return false;
    }
    const auto *map=c.town.map();const auto origin=c.town.read().origin;if(!map || !origin) return false;
    const Vec offset{float(origin->x),float(origin->y)};
    const Vec localGoal=wanted-offset+Vec{.5f,.5f};
    const Vec clipped{std::clamp(localGoal.x,.5f,float(map->grid.width)-.5f),std::clamp(localGoal.y,.5f,float(map->grid.height)-.5f)};
    ++pathSearches;const auto path=map->grid.path(c.position-offset+Vec{.5f,.5f},clipped,true,playerMovement,true);
    Vec previous=c.position;std::optional<Vec> routeTarget;float travelled=0,bestProgress=0;
    for(const auto node:path) {
        const Vec endpoint=node+offset-Vec{.5f,.5f};const Vec leg=endpoint-previous;const float length=leg.length();
        if(length>0) for(float t=std::min(4.f,length);;t=std::min(t+4.f,length)) {
            const Vec sample=previous+leg*(t/length);const auto local=sample-offset+Vec{.5f,.5f};
            const float span=(sample-c.position).length();
            const bool clear=!canCharge || (map->grid.segment(c.position-offset+Vec{.5f,.5f},local,{},playerMovement) &&
                map->grid.nativeMovementSegment(c.position-offset+Vec{.5f,.5f},local,playerMovement));
            if(span>=8 && span<=mobilityPolicy.distance && travelled+t>bestProgress && clear) {
                bestProgress=travelled+t;routeTarget=sample;
            }
            if(t>=length) break;
        }
        travelled+=length;previous=endpoint;
    }
    if(!routeTarget) return false;
    // Follow progress along the known route. Euclidean progress toward the
    // final goal alone can repeatedly teleport into an irregular wall pocket
    // while the walking path correctly backs out of it.
    delta=*routeTarget-c.position;distance=delta.length();
    const Vec direction=delta*(1/distance),side{-direction.y,direction.x};
    std::optional<Vec> landing;float best=-1e9f;
    for(int back:{0,4,8}) for(int lateral:{0,-4,4,-8,8}) {
        const Vec candidate=c.position+direction*(std::min(distance,float(mobilityPolicy.distance))-float(back))+side*float(lateral);
        const auto cell=map->grid.nativeSpawn(candidate-offset+Vec{.5f,.5f},2,playerMovement);
        if(!cell) continue; // Only already materialized client collision is consulted.
        // nativeSpawn returns integer native cells, unlike path()'s cell
        // centers. Check the exact cell that will be sent on the wire: a
        // half-cell subtraction followed by floor would send its neighbour.
        const Vec point=*cell+offset,landingCell=*cell+Vec{.5f,.5f};
        const float progress=distance-(point-*routeTarget).length();
        if(point.x<0 || point.y<0 || point.x>65535 || point.y>65535 || (point-c.position).length()<6 ||
            (point-c.position).length()>mobilityPolicy.distance || progress<2) continue;
        const auto trial=std::tuple{area.value_or(0),uint16_t(wanted.x),uint16_t(wanted.y),uint16_t(point.x),uint16_t(point.y)};
        if(mobilityTrials.contains(trial) && mobilityTrials.at(trial)>=2) continue;
        const auto future=*routeTarget-offset+Vec{.5f,.5f};
        if(!map->grid.segment(landingCell,future,{},playerMovement) || !map->grid.nativeMovementSegment(landingCell,future,playerMovement)) continue;
        if(canCharge && (!map->grid.segment(c.position-offset+Vec{.5f,.5f},landingCell,{},playerMovement) ||
            !map->grid.nativeMovementSegment(c.position-offset+Vec{.5f,.5f},landingCell,playerMovement))) continue;
        int clearance=0;
        for(int radius:{2,4,6}) {
            bool clear=true;
            for(int x:{-1,0,1}) for(int y:{-1,0,1}) if(x || y)
                clear=clear && map->grid.walkable(landingCell+Vec{float(x*radius),float(y*radius)},playerMovement);
            if(!clear) break;
            clearance=radius;
        }
        // Prefer an interior landing that exposes the same nearby rooms over
        // repeatedly spending mana on wall/unknown-terrain edge pixels.
        const float score=progress+float(clearance)*3;
        if(score>best) {best=score;landing=point;}
    }
    if(!landing) return false;
    const auto skill=canTeleport?teleport:charge;
    const auto hand=canTeleport?OnlineSkillHand::Right:OnlineSkillHand::Left;
    if(!selectSkill(c,skill,hand)) {
        mobilityHold=true;wait("mobility-select","Waiting for the normal travel skill selection");return true;
    }
    if(world.combatRequest && world.combatRequest->state==OnlineCombatRequest::State::Pending) {
        mobilityHold=true;return true;
    }
    c.control.cancelMovement();OnlineCombatCommand cast;cast.action=OnlineCombatCommand::Action::Cast;
    cast.hand=hand;cast.point=OnlinePoint{uint16_t(std::floor(landing->x)),uint16_t(std::floor(landing->y))};
    cast.stationary=true;cast.repeat=false;
    if(!c.combat.submit(cast)) {
        nextMobility=now+std::chrono::seconds(3);note("mobility-unavailable",c.combat.reason());return false;
    }
    ++castRequests;mobilityFrom=c.position;mobilityTarget=cast.point;
    ++mobilityTrials[{area.value_or(0),uint16_t(wanted.x),uint16_t(wanted.y),cast.point->x,cast.point->y}];
    mobilityTeleport=canTeleport;
    const auto player=world.units.find({0,c.session.read().load.playerUnitId.value_or(0)});
    mobilityDiscontinuity=player==world.units.end()?0:player->second.positionDiscontinuity;
    mobilityDeadline=now+std::chrono::seconds(3);mobilityHold=true;
    note(canTeleport?"teleport-submitted":"charge-submitted",std::to_string(cast.point->x)+","+std::to_string(cast.point->y));return true;
}
}
