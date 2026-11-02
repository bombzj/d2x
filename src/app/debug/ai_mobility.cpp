#include "ai_impl.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

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
        now-lastSelection<std::chrono::milliseconds(100)) return false;
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
bool DebugAi::Impl::maintainArmor(Context &c) {
    if (Clock::now() < nextArmorAttempt) return false;
    const auto &view = c.session.read();
    if (!view.load.playerUnitId || view.world.npcRequested || view.world.waypointSource) return false;
    std::optional<uint16_t> selected;
    for (const auto name : {"Shiver Armor", "Chilling Armor", "Frozen Armor"}) {
        const auto skill = definitions->skill(name);
        const auto row = definitions->skillRows.find(skill);
        if (row == definitions->skillRows.end()) continue;
        const auto stateName = definitions->skills.value(row->second, "aurastate");
        for (size_t state = 0; state < definitions->states.rows().size(); ++state)
            if (definitions->states.value(state, "state") == stateName) {
                const auto id = definitions->states.number(state, "Id");
                if (id && *id >= 0 && *id <= UINT8_MAX && recovering(c, uint8_t(*id))) return false;
            }
        if (!selected && knownSkill(c, skill) && !stateName.empty() && c.supplies && c.supplies->skillUsable &&
            c.supplies->skillUsable(skill).value_or(false)) selected = skill;
    }
    if (!selected) return false;
    if (!selectSkill(c, *selected, OnlineSkillHand::Right)) {
        wait("armor-select", "Selecting the available defensive armor skill");
        return true;
    }
    releaseCombat(c); cancelWalk(c);
    OnlineCombatCommand cast; cast.action = OnlineCombatCommand::Action::Cast;
    cast.hand = OnlineSkillHand::Right; cast.stationary = true;
    cast.point = OnlinePoint{uint16_t(c.position.x), uint16_t(c.position.y)};
    if (submitCombat(c, cast, DebugActionOwner::Combat, "armor-cast", c.position)) {
        nextArmorAttempt = Clock::now() + std::chrono::seconds(3);
        ++castRequests;
        note("armor-cast", "Waiting for the normal server state assignment");
    }
    return true;
}
bool DebugAi::Impl::teleportAvailable(const Context &c,bool resourceReady) const {
    if(c.town.read().town || (mobilityPolicy.mode!="auto" && mobilityPolicy.mode!="teleport") ||
        !knownSkill(c,definitions->skill("Teleport"))) return false;
    if(resourceReady && c.supplies && c.supplies->skillUsable)
        if(const auto usable=c.supplies->skillUsable(definitions->skill("Teleport"));usable && !*usable) return false;
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
            const auto at=mobilityTeleport && world.playerPosition?
                Vec{float(world.playerPosition->x),float(world.playerPosition->y)}:c.position;
            const float miss=(at-target).length();
            const bool arrived=(!mobilityTeleport || world.playerPosition.has_value()) && miss<=4 &&
                miss<(mobilityFrom-target).length();
            if(arrived) {
                // A near landing can be short of its requested route sample.
                mobilityRouteProgress=std::max(mobilityRouteProgress,mobilityPendingProgress-miss);
                // A confirmed arrival is not a failed landing attempt. A
                // later legitimate return to this point keeps the same goal.
                if(mobilityGoal) {
                    const Vec goal{float(mobilityGoal->x),float(mobilityGoal->y)};
                    const float remaining=(at-goal).length();
                    mobilityBestRemaining=std::min(mobilityBestRemaining.value_or(remaining),remaining);
                    mobilityTrials.erase({mobilityArea,mobilityGoal->x,mobilityGoal->y,mobilityTarget->x,mobilityTarget->y});
                }
            }
            note(arrived?"mobility-progress-observed":"mobility-relocated-off-target");mobilityTarget.reset();nextMobility=now;
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
    const bool combatMovement=!combatOwned && (retreatPoint || combatRun.reposition || combatRun.kitePoint || hazardDodge || phase=="combat-approach");
    bool ranged=policy.enabled && policy.range>3 && !c.town.read().town;
    const auto attack=policy.skill.value_or(definitions->skill("Attack"));
    if(attack==definitions->skill("Attack")) ranged=false;
    for(size_t row=0;row<definitions->skills.rows().size();++row)
        if(definitions->skills.number(row,"Id")==attack && definitions->skills.value(row,"range")=="h2h") ranged=false;
    const auto crowdedLanding=[&](Vec at) {
        return ranged && std::any_of(world.units.begin(),world.units.end(),[&](const auto &entry) {
            const auto &enemy=entry.second;
            return enemy.key.type==1 && enemy.position && !onlineMonsterCorpse(enemy) &&
                (at-Vec{float(enemy.position->x),float(enemy.position->y)}).length()<4;
        });
    };
    const bool combatActive=combatTarget && combatRun.suspendedAt==Clock::time_point{} && !combatMovement;
    if(combatActive || !c.walk.active() || now<nextMobility || world.npcRequested || world.waypointSource ||
        portalUsed || travelPhase==TravelPhase::WaypointWait || travelPhase==TravelPhase::CrossWait) {
        mobilityDiagnostics["skip"]={{"combat",combatActive},{"retainedCombat",bool(combatTarget)},{"childActive",c.walk.active()},
            {"cooldown",now<nextMobility},{"npc",bool(world.npcRequested)},{"waypoint",bool(world.waypointSource)},
            {"portalUsed",portalUsed},{"travelPhase",unsigned(travelPhase)}};
        return false;
    }
    const auto child=c.walk.snapshot();const auto &target=child.at("target");
    std::optional<Vec> destination;
    if(target.is_object()) destination=Vec{target.at("x").get<float>(),target.at("y").get<float>()};
    // A newly created unit walk has not ticked yet. Resolve its already
    // assigned target now so Teleport can own the first approach frame.
    const auto &childUnit=child.at("unit");
    if(!destination && childUnit.is_object()) {
        const auto unit=world.units.find({childUnit.at("type").get<uint8_t>(),childUnit.at("id").get<uint32_t>()});
        if(unit!=world.units.end() && unit->second.position)
            destination=Vec{float(unit->second.position->x),float(unit->second.position->y)};
    }
    if(!destination) return false;
    Vec wanted=*destination;
    Vec delta=wanted-c.position;float distance=delta.length();
    const bool pickup=walkIntent.is_object() && walkIntent.value("purpose",std::string{})=="pickup-approach";
    const bool assignedApproach=childUnit.is_object() && world.units.contains({
        childUnit.at("type").get<uint8_t>(),childUnit.at("id").get<uint32_t>()});
    if(assignedApproach && walkIntent.is_object() && walkIntent.value("interact",false) &&
        c.town.interactionReady(c.session.read(),{childUnit.at("type").get<uint8_t>(),childUnit.at("id").get<uint32_t>()},c.position))
        return false; // A normal click is already possible; do not spend a cast on the object's center.
    // An assigned portal/door is a known approach goal too. Its authored
    // center need not be standing floor or reachable by walking; normal
    // Teleport can stage toward it through observed neighbouring floor.
    // The child still owns the final interaction-perimeter and actual click.
    const bool knownLanding=assignedApproach || (walkIntent.is_object() && (walkIntent.value("purpose",std::string{})=="known-target-teleport" || walkIntent.value("purpose",std::string{})=="combat-evade" || walkIntent.value("purpose",std::string{})=="combat-kite" ||
        ((walkIntent.value("purpose",std::string{})=="exploration-boundary" ||
          walkIntent.value("purpose",std::string{})=="exploration-backtrack") && walkIntent.value("directTeleport",false))));
    if(distance<1 || (distance<8 && !pickup && !knownLanding)) return false;
    const auto teleport=definitions->skill("Teleport"),charge=definitions->skill("Charge"),vigor=definitions->skill("Vigor");
    const bool canTeleport=teleportAvailable(c);
    if(canTeleport && townExitArea==area && townExitArea) {
        const auto &rooms=c.town.navigationRooms();
        const bool confirmed=world.playerPosition && std::any_of(rooms.begin(),rooms.end(),[&](const auto &room) {
            const auto at=*world.playerPosition;
            return room.level==*townExitArea && at.x>=room.x && at.x<room.x+room.width &&
                at.y>=room.y && at.y<room.y+room.height &&
                world.rooms.contains({uint8_t(room.level),uint16_t(room.x/5),uint16_t(room.y/5)});
        });
        if(confirmed) {townExitArea.reset();note("town-exit-position-observed","Server position now belongs to the field; travel casting is eligible");}
        else {
            // Keep the already submitted crossing in flight. Briefly avoid
            // extending it or casting from prediction ahead of the host;
            // sparse receipts then fall back to walking, not a failed cast
            // and an extra three-second skill cooldown.
            mobilityHold=now<townExitObserveUntil;
            wait("mobility-town-exit",mobilityHold?"Waiting briefly for the host's field position while the crossing completes":
                "Continuing the boundary crossing until an actual field position is received");
            return mobilityHold;
        }
    }
    if(distance<8 && !canTeleport) {mobilityDiagnostics["skip"]="short distance without Teleport";return false;}
    const bool canCharge=!canTeleport && !c.town.read().town && knownSkill(c,charge,true) &&
        (mobilityPolicy.mode=="auto" || mobilityPolicy.mode=="charge");
    const bool useVigor=mobilityPolicy.vigor && !canTeleport && knownSkill(c,vigor);
    if(useVigor && !selectSkill(c,vigor,OnlineSkillHand::Right)) {
        wait("mobility-select","Walking while waiting for normal Vigor selection");return false;
    }
    if(!canTeleport && !canCharge) {mobilityDiagnostics["skip"]="no usable travel skill";return false;}
    const auto *map=c.town.map();const auto origin=c.town.read().origin;if(!map || !origin) return false;
    const Vec offset{float(origin->x),float(origin->y)};
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
    if(canTeleport && assignedApproach && walkIntent.value("interact",false)) {
        const OnlineUnitKey key{childUnit.at("type").get<uint8_t>(),childUnit.at("id").get<uint32_t>()};
        const auto entry=std::find_if(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),
            [&](const auto &value){return value.unit==key;});
        if(entry!=c.town.read().mapTargets.end()) {
            // An authored portal/door center can be solid or beyond a chasm.
            // Choose a real standing cell in its normal interaction perimeter,
            // rather than requiring a walking route to that center first.
            const auto grid=c.town.navigationGrid(c.session.read());
            const int width=std::clamp(entry->collisionWidth/2+4,4,16),height=std::clamp(entry->collisionHeight/2+4,4,16);
            std::optional<Vec> perimeter;float best=std::numeric_limits<float>::max();
            for(int y=std::max(0,int(entry->position.y)-height);y<=std::min(65535,int(entry->position.y)+height);++y)
                for(int x=std::max(0,int(entry->position.x)-width);x<=std::min(65535,int(entry->position.x)+width);++x) {
                    const Vec at{float(x),float(y)},local=at-offset+Vec{.5f,.5f};
                    const float span=(at-c.position).length();
                    if(span<1 || span>mobilityPolicy.distance || !nativeNeighbor(at) || crowdedLanding(at) ||
                        !grid.walkable(local,playerMovement) || !c.town.interactionReady(c.session.read(),key,at)) continue;
                    if(span<best) {best=span;perimeter=at;}
                }
            if(perimeter) {wanted=*perimeter;distance=(wanted-c.position).length();}
        }
    }
    const Vec localGoal=wanted-offset+Vec{.5f,.5f};
    const OnlinePoint goal{uint16_t(wanted.x),uint16_t(wanted.y)};
    if(mobilityGoal!=goal || mobilityArea!=area.value_or(0)) {
        mobilityGoal=goal;mobilityArea=area.value_or(0);mobilityRoute.clear();mobilityBestRemaining.reset();
        mobilityRouteProgress=mobilityPendingProgress=mobilityRouteLength=0;
    }
    std::optional<Vec> stagedLanding;
    const bool occupiedGoal=crowdedLanding(wanted);
    const bool standingGoal=map->grid.nativeMovementSegment(localGoal,localGoal,playerMovement);
    if(canTeleport && mobilityRoute.empty() && (distance>mobilityPolicy.distance || !nativeNeighbor(wanted) || occupiedGoal || !standingGoal)) {
        // Only currently reconstructed legal floor is eligible. The distant
        // observed goal stays fixed while ordinary casts advance toward it.
        float remaining=std::min(distance,mobilityBestRemaining.value_or(distance))-3;const int radius=mobilityPolicy.distance;
        for(int y=std::max(0,int(c.position.y)-radius);y<=std::min(65535,int(c.position.y)+radius);++y)
            for(int x=std::max(0,int(c.position.x)-radius);x<=std::min(65535,int(c.position.x)+radius);++x) {
                const Vec at{float(x),float(y)},local=at-offset+Vec{.5f,.5f};
                const float span=(at-c.position).length(),left=(wanted-at).length();
                const auto trial=std::tuple{area.value_or(0),goal.x,goal.y,uint16_t(x),uint16_t(y)};
                if(span<6 || span>mobilityPolicy.distance || left>=remaining ||
                    !nativeNeighbor(at) || crowdedLanding(at) ||
                    (mobilityTrials.contains(trial) && mobilityTrials.at(trial)>=2) ||
                    !map->grid.nativeMovementSegment(local,local,playerMovement)) continue;
                remaining=left;stagedLanding=at;
            }
    }
    const bool directLanding=bool(stagedLanding) || (canTeleport && standingGoal && !occupiedGoal && distance<=mobilityPolicy.distance &&
        nativeNeighbor(wanted));
    if(!directLanding && mobilityRoute.empty()) {
        // Freeze a complete, already observed route for this child goal.
        // Re-solving AStar after each relocation can choose opposing detours
        // as active collision changes. Never accelerate a clipped wall target.
        ++pathSearches;const auto path=c.walk.navigationSearch().path(map->grid,c.position-offset+Vec{.5f,.5f},localGoal,false,playerMovement,true);
        if(!path.empty() && (path.back()-localGoal).length()<=1.f) {
            mobilityRoute.push_back(c.position);
            for(const auto node:path) {
                const auto at=node+offset-Vec{.5f,.5f};
                mobilityRouteLength+=(at-mobilityRoute.back()).length();mobilityRoute.push_back(at);
            }
        }
    }
    mobilityDiagnostics={{"decision",decisions},{"area",area.value_or(0)},
        {"from",{c.position.x,c.position.y}},{"target",{wanted.x,wanted.y}},
        {"goalOccupied",occupiedGoal},{"stagedLanding",stagedLanding?Json{stagedLanding->x,stagedLanding->y}:Json(nullptr)},
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
        if(crowdedLanding(at) || !map->grid.nativeMovementSegment(local,local,playerMovement) ||
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
                    else if(crowdedLanding(point)) trace["result"]="enemy-too-close-to-ranged-landing";
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
    if(!landing) {
        mobilityDiagnostics["result"]="no-forward-known-route-sample";
        // Ordinary walking can still get around a corner. If the current
        // collision also rules out that route, let exploration pick a new
        // viewpoint instead of keeping the same child until its timeout.
        if(canTeleport && interest && *interest==goal && walkIntent.is_object() && walkIntent.value("purpose",std::string{})=="exploration-boundary") {
            const auto grid=c.town.navigationGrid(c.session.read());
            ++pathSearches;
            const auto path=c.walk.navigationSearch().path(grid,c.position-offset+Vec{.5f,.5f},localGoal,false,playerMovement,true);
            if(path.empty() || (path.back()-localGoal).length()>1.f) {
                recordFrontierAttempt(c,goal,"unreachable","No forward landing or complete walk to this viewpoint");
                cancelWalk(c);exploring.reset();interest.reset();
            }
        }
        return false;
    }
    mobilityDiagnostics["selected"]={landing->x,landing->y};
    mobilityDiagnostics["selectedRouteProgress"]=selectedProgress;
    const auto skill=canTeleport?teleport:charge;
    const auto hand=canTeleport?OnlineSkillHand::Right:OnlineSkillHand::Left;
    // Once a valid Teleport landing is chosen, preserve that movement intent
    // through skill selection, animation recovery and the native input rate
    // limit. A walking tick here would replace the cast between two landings.
    const bool ownsApproach=canTeleport;
    if(canTeleport && c.supplies && c.supplies->castInputBusy.value_or(false)) {
        mobilityHold=true;wait("mobility-animation","Waiting for the current normal cast animation to release input");return true;
    }
    if(!selectSkill(c,skill,hand)) {
        mobilityHold=ownsApproach;
        wait("mobility-select",ownsApproach?"Selecting the travel skill for the observed interaction / required landing":
            "Walking while waiting for the normal travel skill selection");return ownsApproach;
    }
    if(world.combatRequest && world.combatRequest->state==OnlineCombatRequest::State::Pending) {
        mobilityHold=ownsApproach;return ownsApproach;
    }
    // A transient input wait must not stop an otherwise useful walking leg.
    OnlineCombatCommand cast;cast.action=OnlineCombatCommand::Action::Cast;
    cast.hand=hand;cast.point=OnlinePoint{uint16_t(std::floor(landing->x)),uint16_t(std::floor(landing->y))};
    cast.stationary=true;cast.repeat=false;
    const auto submitted=submitInput(c,DebugActionOwner::Mobility,"mobility-cast:"+std::to_string(skill),DebugSkill,
        [&]{return c.combat.submit(cast,c.position);},[&]{return c.combat.reason();});
    if(submitted==DebugSubmitResult::Waiting) {mobilityHold=ownsApproach;return ownsApproach;}
    if(submitted==DebugSubmitResult::Expired) {
        note("mobility-unavailable",submissionWaits.at(DebugActionOwner::Mobility).reason);
        submissionWaits.erase(DebugActionOwner::Mobility);nextMobility=now+std::chrono::seconds(3);return false;
    }
    c.control.cancelMovement();
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
