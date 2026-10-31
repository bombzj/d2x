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
    submitCombat(c,request,DebugActionOwner::Combat,"skill-select");
    return false; // A queued selection is never a confirmed hand assignment.
}
std::optional<uint16_t> DebugAi::Impl::battleAura(const Context &c) const {
    if(policy.aura) return policy.aura;
    for(const auto name:{"Fanaticism","Concentration","Might"}) {
        const auto skill=definitions->skill(name);if(knownSkill(c,skill)) return skill;
    }
    return {};
}
bool DebugAi::Impl::teleportAvailable(const Context &c) const {
    if(c.town.read().town || (mobilityPolicy.mode!="auto" && mobilityPolicy.mode!="teleport") ||
        !knownSkill(c,definitions->skill("Teleport"))) return false;
    for(size_t row=0;row<definitions->levels.rows().size();++row)
        if(definitions->levels.number(row,"Id")==area)
            return definitions->levels.number(row,"Teleport").value_or(0)!=0;
    return false;
}
bool DebugAi::Impl::observeMobility(Context &c) {
    const auto now=Clock::now();const auto &world=c.session.read().world;
    if(mobilityTarget) {
        // Observe the normal path's relocation/progress. Casting acceptance
        // never writes coordinates or proves arrival at the intended endpoint.
        const Vec target{float(mobilityTarget->x),float(mobilityTarget->y)};
        const auto player=world.units.find({0,c.session.read().load.playerUnitId.value_or(0)});
        const bool progress=mobilityTeleport?player!=world.units.end() && player->second.positionDiscontinuity!=mobilityDiscontinuity:
            (c.position-target).length()<3 || (c.position-mobilityFrom).length()>3;
        if(progress) {
            if((mobilityTeleport && world.playerPosition &&
                (Vec{float(world.playerPosition->x),float(world.playerPosition->y)}-target).length()<=4) ||
                (!mobilityTeleport && (c.position-target).length()<=4))
                mobilityRouteProgress=std::max(mobilityRouteProgress,mobilityPendingProgress);
            note("mobility-progress-observed");mobilityTarget.reset();nextMobility=now;
            return false;
        }
        if(now>=mobilityDeadline) {
            note("mobility-no-progress","Falling back to ordinary walking");
            mobilityTarget.reset();nextMobility=now+std::chrono::seconds(3);return false;
        }
        mobilityHold=true;wait("mobility-wait","Waiting for normal skill movement/position feedback");return true;
    }
    return false;
}
bool DebugAi::Impl::mobility(Context &c) {
    if(mobilityTarget) return observeMobility(c);
    const auto now=Clock::now();const auto &world=c.session.read().world;
    const bool combatMovement=!combatOwned && (retreatPoint || combatRun.reposition || phase=="combat-approach");
    const bool combatActive=combatTarget && combatRun.suspendedAt==Clock::time_point{} && !combatMovement;
    if(combatActive || !c.walk.active() || now<nextMobility || world.npcRequested || world.waypointSource ||
        portalUsed || travelPhase==TravelPhase::WaypointWait || travelPhase==TravelPhase::CrossWait) {
        mobilityDiagnostics["skip"]={{"combat",combatActive},{"retainedCombat",bool(combatTarget)},{"childActive",c.walk.active()},
            {"cooldown",now<nextMobility},{"npc",bool(world.npcRequested)},{"waypoint",bool(world.waypointSource)},
            {"portalUsed",portalUsed},{"travelPhase",unsigned(travelPhase)}};
        return false;
    }
    const auto child=c.walk.snapshot();const auto &target=child.at("target");
    if(target.is_null()) return false;
    const Vec wanted{target.at("x").get<float>(),target.at("y").get<float>()};
    Vec delta=wanted-c.position;float distance=delta.length();
    const bool pickup=walkIntent.is_object() && walkIntent.value("purpose",std::string{})=="pickup-approach";
    const bool knownLanding=walkIntent.is_object() && (walkIntent.value("purpose",std::string{})=="known-target-teleport" || walkIntent.value("purpose",std::string{})=="combat-evade" ||
        ((walkIntent.value("purpose",std::string{})=="exploration-boundary" ||
          walkIntent.value("purpose",std::string{})=="exploration-backtrack") && walkIntent.value("directTeleport",false)));
    if(distance<1 || (distance<8 && !pickup && !knownLanding)) return false;
    const auto teleport=definitions->skill("Teleport"),charge=definitions->skill("Charge"),vigor=definitions->skill("Vigor");
    const bool canTeleport=teleportAvailable(c);
    if(distance<8 && !canTeleport) {mobilityDiagnostics["skip"]="short distance without Teleport";return false;}
    const bool canCharge=!canTeleport && !c.town.read().town && knownSkill(c,charge,true) &&
        (mobilityPolicy.mode=="auto" || mobilityPolicy.mode=="charge");
    const bool useVigor=mobilityPolicy.vigor && !canTeleport && knownSkill(c,vigor);
    if(useVigor && !selectSkill(c,vigor,OnlineSkillHand::Right)) {
        mobilityHold=true;wait("mobility-select","Waiting for normal Vigor selection");return true;
    }
    if(!canTeleport && !canCharge) {mobilityDiagnostics["skip"]="no usable travel skill";return false;}
    const auto *map=c.town.map();const auto origin=c.town.read().origin;if(!map || !origin) return false;
    const Vec offset{float(origin->x),float(origin->y)};
    const Vec localGoal=wanted-offset+Vec{.5f,.5f};
    const OnlinePoint goal{uint16_t(wanted.x),uint16_t(wanted.y)};
    const auto &rooms=c.town.navigationRooms();size_t nativeRoom=rooms.size();
    const auto contains=[](const auto &room,Vec at) {
        return at.x>=room.x && at.y>=room.y && at.x<room.x+room.width && at.y<room.y+room.height;
    };
    for(size_t i=0;i<rooms.size();++i) if(rooms[i].loaded && rooms[i].level==area && contains(rooms[i],c.position)) {nativeRoom=i;break;}
    const auto nativeNeighbor=[&](Vec at) {
        // Do27 -> SUNIT relocation -> DUNGEON_GetRoomAtPosition only
        // checks this Room1 and its active adjacent list, not all cached floor.
        if(nativeRoom==rooms.size()) return false;
        if(contains(rooms[nativeRoom],at)) return true;
        for(const auto next:rooms[nativeRoom].near)
            if(next<rooms.size() && rooms[next].loaded && contains(rooms[next],at)) return true;
        return false;
    };
    std::optional<Vec> stagedLanding;
    if(canTeleport && (knownLanding || pickup) && (distance>mobilityPolicy.distance || !nativeNeighbor(wanted))) {
        // Only currently reconstructed legal floor is eligible. The distant
        // observed goal stays fixed while ordinary casts advance toward it.
        float remaining=distance-3;const int radius=mobilityPolicy.distance;
        for(int y=std::max(0,int(c.position.y)-radius);y<=std::min(65535,int(c.position.y)+radius);++y)
            for(int x=std::max(0,int(c.position.x)-radius);x<=std::min(65535,int(c.position.x)+radius);++x) {
                const Vec at{float(x),float(y)},local=at-offset+Vec{.5f,.5f};
                const float span=(at-c.position).length(),left=(wanted-at).length();
                const auto trial=std::tuple{area.value_or(0),goal.x,goal.y,uint16_t(x),uint16_t(y)};
                if(span<6 || span>mobilityPolicy.distance || left>=remaining ||
                    !nativeNeighbor(at) ||
                    (mobilityTrials.contains(trial) && mobilityTrials.at(trial)>=2) ||
                    !map->grid.nativeMovementSegment(local,local,playerMovement)) continue;
                remaining=left;stagedLanding=at;
            }
    }
    const bool directLanding=bool(stagedLanding) || (canTeleport && distance<=mobilityPolicy.distance &&
        nativeNeighbor(wanted) && (pickup || knownLanding));
    if(mobilityGoal!=goal || mobilityArea!=area.value_or(0)) {
        mobilityGoal=goal;mobilityArea=area.value_or(0);mobilityRoute.clear();
        mobilityRouteProgress=mobilityPendingProgress=mobilityRouteLength=0;
    }
    if(!directLanding && mobilityRoute.empty()) {
        // Freeze a complete, already observed route for this child goal.
        // Re-solving AStar after each relocation can choose opposing detours
        // as active collision changes. Never accelerate a clipped wall target.
        ++pathSearches;const auto path=c.walk.navigationSearch().path(map->grid,c.position-offset+Vec{.5f,.5f},localGoal,false,playerMovement,true);
        if(!path.empty()) {
            mobilityRoute.push_back(c.position);
            for(const auto node:path) {
                const auto at=node+offset-Vec{.5f,.5f};
                mobilityRouteLength+=(at-mobilityRoute.back()).length();mobilityRoute.push_back(at);
            }
        }
    }
    mobilityDiagnostics={{"decision",decisions},{"area",area.value_or(0)},
        {"from",{c.position.x,c.position.y}},{"target",{wanted.x,wanted.y}},
        {"routeLength",mobilityRouteLength},{"confirmedRouteProgress",mobilityRouteProgress},
        {"remainingRoute",std::max(0.f,mobilityRouteLength-mobilityRouteProgress)},
        {"routePoints",mobilityRoute.size()},{"candidates",Json::array()},{"selected",nullptr}};
    mobilityDiagnostics["nativeRoom"]=nativeRoom<rooms.size()?Json{{rooms[nativeRoom].level,rooms[nativeRoom].x,rooms[nativeRoom].y}}:Json(nullptr);
    if(!directLanding && mobilityRoute.empty()) {
        mobilityDiagnostics["result"]="no-complete-known-route-to-child-target";return false;
    }
    std::optional<Vec> landing;float selectedProgress=mobilityRouteProgress,best=-1e9f;
    unsigned candidates=0;
    if(directLanding) {
        const Vec at=stagedLanding.value_or(wanted),local=at-offset+Vec{.5f,.5f};
        const auto trial=std::tuple{area.value_or(0),goal.x,goal.y,uint16_t(at.x),uint16_t(at.y)};
        if(!map->grid.nativeMovementSegment(local,local,playerMovement) ||
            (mobilityTrials.contains(trial) && mobilityTrials.at(trial)>=2)) {
            mobilityDiagnostics["result"]="direct landing blocked or attempts exhausted";return false;
        }
        landing=at;
    } else {
        float travelled=0;
        for(size_t i=1;i<mobilityRoute.size();++i) {
            const Vec from=mobilityRoute[i-1],leg=mobilityRoute[i]-from;const float length=leg.length();
            if(length>0) for(float t=std::min(4.f,length);;t=std::min(t+4.f,length)) {
                const float progress=travelled+t;
                const Vec sample=from+leg*(t/length);
                const Vec point{std::floor(sample.x+.5f),std::floor(sample.y+.5f)};
                const Vec local=point-offset+Vec{.5f,.5f};const float span=(point-c.position).length();
                if(progress>mobilityRouteProgress+1 && span>=6 && span<=mobilityPolicy.distance &&
                    point.x>=0 && point.y>=0 && point.x<=65535 && point.y<=65535) {
                    const auto trial=std::tuple{area.value_or(0),goal.x,goal.y,uint16_t(point.x),uint16_t(point.y)};
                    Json trace{{"point",{point.x,point.y}},{"routeProgress",progress}};
                    ++candidates;
                    if(canTeleport && !nativeNeighbor(point)) trace["result"]="outside-native-room-neighborhood";
                    else if(!map->grid.nativeMovementSegment(local,local,playerMovement)) trace["result"]="unavailable-landing";
                    else if(mobilityTrials.contains(trial) && mobilityTrials.at(trial)>=2) trace["result"]="landing-attempt-limit";
                    else if(canCharge && (!map->grid.segment(c.position-offset+Vec{.5f,.5f},local,{},playerMovement) ||
                        !map->grid.nativeMovementSegment(c.position-offset+Vec{.5f,.5f},local,playerMovement)))
                        trace["result"]="charge-ray-blocked";
                    else {
                        int clearance=0;
                        for(int radius:{2,4,6}) {
                            bool clear=true;
                            for(int x:{-1,0,1}) for(int y:{-1,0,1}) if(x || y)
                                clear=clear && map->grid.walkable(local+Vec{float(x*radius),float(y*radius)},playerMovement);
                            if(!clear) break;
                            clearance=radius;
                        }
                        const float score=progress+float(clearance)*.25f;
                        trace["result"]="candidate";trace["score"]=score;
                        if(score>best) {best=score;landing=point;selectedProgress=progress;}
                    }
                    if(mobilityDiagnostics["candidates"].size()<15)
                        mobilityDiagnostics["candidates"].push_back(std::move(trace));
                }
                if(t>=length) break;
            }
            travelled+=length;
        }
    }
    mobilityDiagnostics["candidateCount"]=candidates;
    if(!landing) {mobilityDiagnostics["result"]="no-forward-known-route-sample";return false;}
    mobilityDiagnostics["selected"]={landing->x,landing->y};
    mobilityDiagnostics["selectedRouteProgress"]=selectedProgress;
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
    if(!submitCombat(c,cast,DebugActionOwner::Mobility,"mobility-cast",c.position)) {
        nextMobility=now+std::chrono::seconds(3);note("mobility-unavailable",c.combat.reason());return false;
    }
    ++castRequests;mobilityFrom=c.position;mobilityTarget=cast.point;
    mobilityPendingProgress=selectedProgress;
    ++mobilityTrials[{area.value_or(0),uint16_t(wanted.x),uint16_t(wanted.y),cast.point->x,cast.point->y}];
    mobilityTeleport=canTeleport;
    const auto player=world.units.find({0,c.session.read().load.playerUnitId.value_or(0)});
    mobilityDiscontinuity=player==world.units.end()?0:player->second.positionDiscontinuity;
    mobilityDeadline=now+std::chrono::seconds(3);mobilityHold=true;
    note(canTeleport?"teleport-submitted":"charge-submitted",std::to_string(cast.point->x)+","+std::to_string(cast.point->y));return true;
}
}
