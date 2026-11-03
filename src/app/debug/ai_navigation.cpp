#include "ai_impl.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>

namespace d2x {
namespace {
// Operate72 sends the expansion finale. Ordinary travel must leave it to
// the explicit quest action even when the normal town destination is known.
bool ordinaryConnection(const OnlineMapTarget &target,const OnlineView &view) {
    if(!target.destination || (target.interaction!=OnlineMapInteraction::Portal && target.interaction!=OnlineMapInteraction::Exit)) return false;
    const auto unit=view.world.units.find(target.unit);
    return unit!=view.world.units.end() && !(target.unit.type==2 && unit->second.classId==565);
}

OnlinePoint nativePoint(Vec p) {
    if(!std::isfinite(p.x) || !std::isfinite(p.y) || p.x<0 || p.y<0 || p.x>65535 || p.y>65535)
        throw std::runtime_error("AI navigation point outside native coordinates");
    return {uint16_t(std::floor(p.x)),uint16_t(std::floor(p.y))};
}
float distance(Vec a,Vec b) {return (a-b).length();}
// The parent can click the assigned unit as soon as both axes are within
// 50 subtiles. Leave room for the point walker's one-cell arrival tolerance.
std::optional<OnlinePoint> townClickApproach(const std::deque<Vec> &path,Vec from,Vec offset,OnlinePoint unit,const Grid &live) {
    for(const Vec next:path) {
        const Vec delta=next-from;
        const int steps=int(std::ceil(std::max(std::abs(delta.x),std::abs(delta.y))));
        for(int i=1;i<=steps;++i) {
            const Vec local=from+delta*(float(i)/steps);
            const auto point=nativePoint(local+offset);
            if(std::abs(int(point.x)-unit.x)>48 || std::abs(int(point.y)-unit.y)>48) continue;
            if(live.walkable({std::floor(local.x)+.5f,std::floor(local.y)+.5f},playerMovement)) return point;
        }
        from=next;
    }
    return {};
}
size_t roomCoverage(const std::vector<RemoteTown::NavigationRoom> &rooms,uint16_t level,OnlinePoint at) {
    for(const auto &r:rooms) if(r.level==level && at.x>=r.x && at.y>=r.y && at.x<r.x+r.width && at.y<r.y+r.height)
        return r.exploredTiles;
    return 0;
}
bool possibleWalkingPassage(const RemoteTown &town,const RemoteTown::NavigationRoom &a,const RemoteTown::NavigationRoom &b) {
    // D2MOO DRLGROOM near lists include rooms separated by up to five tiles.
    // They are visibility candidates, not evidence of a walking connection.
    bool vertical=false;int plane=0,start=0,end=0;
    if(a.x+a.width==b.x || b.x+b.width==a.x) {
        vertical=true;plane=std::max(a.x,b.x);start=std::max(a.y,b.y);end=std::min(a.y+a.height,b.y+b.height);
    } else if(a.y+a.height==b.y || b.y+b.height==a.y) {
        plane=std::max(a.y,b.y);start=std::max(a.x,b.x);end=std::min(a.x+a.width,b.x+b.width);
    } else return false;
    if(start>=end) return false;
    const auto *map=town.map();const auto origin=town.read().origin;
    if(!map || !origin || (!a.loaded && !b.loaded)) return true;
    const auto &grid=map->grid;
    for(int along=start;along<end;++along) {
        bool blocked=false,unknown=false;
        for(int side:{-1,0}) for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            if(!nativeCollisionSizeCell(playerMovement.size,dx,dy)) continue;
            const int x=(vertical?plane+side:along)-origin->x+dx;
            const int y=(vertical?along:plane+side)-origin->y+dy;
            const auto mask=grid.movementMask(x,y);
            if(mask==0xffff) unknown=true;
            else if(mask&playerMovement.mask) blocked=true;
        }
        if(!blocked) {
            if(unknown) return true; // Unknown collision is a candidate, never a route guarantee.
            const Vec first{float((vertical?plane-1:along)-origin->x)+.5f,float((vertical?along:plane-1)-origin->y)+.5f};
            const Vec last=first+(vertical?Vec{1,0}:Vec{0,1});
            if(grid.nativeMovementSegment(first,last,playerMovement)) return true;
        }
    }
    return false;
}
bool currentRoomPassage(const RemoteTown &town,const RemoteTown::NavigationRoom &a,const RemoteTown::NavigationRoom &b,float teleportRange) {
    if(teleportRange<=0) return possibleWalkingPassage(town,a,b);
    const auto *map=town.map();const auto origin=town.read().origin;
    if(!a.loaded || !b.loaded) {
        // Guide observation toward the known neighbor. Only loaded floor
        // can be selected for the actual jump.
        const float dx=float(std::max({0,b.x-a.x-a.width+1,a.x-b.x-b.width+1}));
        const float dy=float(std::max({0,b.y-a.y-a.height+1,a.y-b.y-b.height+1}));
        return Vec{dx,dy}.length()<=teleportRange;
    }
    if(!map || !origin) return false;
    // A visibility-neighbor edge is a Teleport candidate only when actual
    // known standing cells on both sides fit one cast. Water/solid room
    // rectangles must not create an imaginary chain across a river.
    const auto standing=[&](const auto &room) {
        std::vector<Vec> result;
        for(const float y:{.25f,.5f,.75f}) for(const float x:{.25f,.5f,.75f}) {
            const Vec local{room.x+room.width*x-origin->x+.5f,room.y+room.height*y-origin->y+.5f};
            const auto open=map->grid.nativeSpawn(local,8,playerMovement);if(!open) continue;
            const Vec world=*open+Vec{float(origin->x),float(origin->y)};
            if(world.x>=room.x && world.y>=room.y && world.x<room.x+room.width && world.y<room.y+room.height)
                result.push_back(world);
        }
        return result;
    };
    const auto first=standing(a),second=standing(b);
    for(const auto from:first) for(const auto to:second) if(distance(from,to)<=teleportRange) return true;
    return false;
}
bool possibleRoomPassage(const RemoteTown &town,const RemoteTown::NavigationRoom &a,const RemoteTown::NavigationRoom &b,float teleportRange,
    std::map<std::tuple<int,int,int,int,int,int>,bool> &observed) {
    const bool ordered=std::pair{a.x,a.y}<std::pair{b.x,b.y};
    const auto key=std::tuple{a.level,ordered?a.x:b.x,ordered?a.y:b.y,ordered?b.x:a.x,ordered?b.y:a.y,int(teleportRange)};
    if(a.loaded && b.loaded) {
        const bool result=currentRoomPassage(town,a,b,teleportRange);
        observed[key]=result;return result;
    }
    // Releasing Room1 must not turn a previously observed solid seam back
    // into an unknown shortcut. Recompute whenever both sides are current;
    // this is static terrain knowledge, never a failed-navigation blacklist.
    if(const auto found=observed.find(key);found!=observed.end()) return found->second;
    return currentRoomPassage(town,a,b,teleportRange);
}
std::vector<std::vector<size_t>> observedRoomConnections(const std::vector<RemoteTown::NavigationRoom> &rooms,int level) {
    std::vector<std::vector<size_t>> result;result.reserve(rooms.size());
    for(const auto &room:rooms) result.push_back(room.near);
    // Released Room1s have no active near list. Retain a candidate return
    // graph through room rectangles with real observation, without changing
    // native adjacency, generating rooms, or treating an unseen void as floor.
    for(size_t i=0;i<rooms.size();++i) {
        const auto &a=rooms[i];if(a.level!=level) continue;
        for(size_t j=0;j<i;++j) {
            const auto &b=rooms[j];if(b.level!=level || (!a.exploredTiles && !b.exploredTiles)) continue;
            const bool touching=((a.x+a.width==b.x || b.x+b.width==a.x) && std::max(a.y,b.y)<std::min(a.y+a.height,b.y+b.height)) ||
                ((a.y+a.height==b.y || b.y+b.height==a.y) && std::max(a.x,b.x)<std::min(a.x+a.width,b.x+b.width));
            if(!touching) continue;
            if(std::find(result[i].begin(),result[i].end(),j)==result[i].end()) result[i].push_back(j);
            if(std::find(result[j].begin(),result[j].end(),i)==result[j].end()) result[j].push_back(i);
        }
    }
    return result;
}
}
bool DebugAi::Impl::resumeExplorationReturn(Context &c) {
    if(!explorationReturn) return false;
    if(Clock::now()>=explorationReturn->deadline) throw std::runtime_error("Exploration town/waypoint shortcut was not confirmed");
    auto &trip=*explorationReturn;
    if(!trip.returning) {
        if(!reachTown(c)) return true;
        cancelWalk(c);portalIssued=portalAttempted=portalUsed=false;oldPortals.clear();submittedTravel.reset();
        travelDestination=trip.area;travelRoute.clear();travelVisitedAreas.clear();unavailableWaypointArea.reset();
        waypointDestination=trip.area;travelPhase=TravelPhase::WaypointOpen;legDestination.reset();legUnit.reset();
        trip.returning=true;note("exploration-waypoint-return","Returning to the unlocked exploration hub");
    }
    if(!travel(c,trip.area)) return true;
    visited=std::move(trip.visited);explorationReturn.reset();
    exploring.reset();interest.reset();note("exploration-hub-arrived","Continuing remaining observed frontiers");return true;
}
std::string DebugAi::Impl::walkState(const Context &c) const {return c.walk.snapshot().at("state").get<std::string>();}
void DebugAi::Impl::rememberTravel(Context &c,OnlineUnitKey key) {
    const auto &view=c.session.read();const auto unit=view.world.units.find(key);
    const auto child=c.walk.snapshot();const auto &submitted=child.at("submittedInteraction");
    if(!area || unit==view.world.units.end() || !submitted.is_object()) return;
    submittedTravel=TravelSubmission{key,unit->second.assignmentRevision,view.gameGeneration,view.world.areaGeneration,
        submitted.at("worldRevision").get<uint64_t>(),submitted.at("discontinuity").get<uint64_t>(),*area};
}
bool DebugAi::Impl::recoverTravel(Context &c) {
    if(!submittedTravel || inputBlocked) return false;
    const auto &sent=*submittedTravel;const auto &view=c.session.read();const auto &world=view.world;
    const auto unit=world.units.find(sent.unit),player=world.units.find({0,view.load.playerUnitId.value_or(0)});
    if(view.stage!=OnlineStage::ProtocolReady || !view.load.serverLoadComplete || view.gameGeneration!=sent.game ||
        world.areaGeneration!=sent.world || c.town.read().area!=sent.sourceArea || onlinePlayerDead(world) ||
        unit==world.units.end() || unit->second.assignmentRevision!=sent.assignment ||
        player==world.units.end() || !player->second.position || player->second.positionRevision<=sent.request ||
        player->second.positionDiscontinuity!=sent.discontinuity || !c.town.permitsInteraction(view,sent.unit)) return false;
    const auto at=*player->second.position;
    const auto &rooms=c.town.navigationRooms();
    if(std::none_of(rooms.begin(),rooms.end(),[&](const auto &room) {
        return room.loaded && room.level==sent.sourceArea && at.x>=room.x && at.y>=room.y &&
            at.x<room.x+room.width && at.y<room.y+room.height;
    })) return false;
    auto &attempt=travelRetries[{sent.sourceArea,sent.unit,sent.assignment}];
    if(attempt>=2) return false;
    ++attempt;note("travel-source-confirmed-recovery",std::to_string(sent.unit.id)+" attempt "+std::to_string(attempt));
    cancelWalk(c);submittedTravel.reset();return true;
}
void DebugAi::Impl::observeChild(Context &c,const Goal &goal) {
    if(!walkIntent.is_object() || walkIntent.value("goal",std::string{})!="unit" || !walkIntent.value("interact",false)) return;
    const auto child=c.walk.snapshot();
    if(!child.value("interactionRequests",0u)) return;
    const OnlineUnitKey unit{uint8_t(walkIntent.at("unitType").get<int>()),walkIntent.at("unitId").get<uint32_t>()};
    const bool waypoint=walkIntent.value("purpose",std::string{})=="activate-waypoint";
    if(child.value("state",std::string{})!="completed") {
        // AI observes packets before the walk ticks this frame. Consume an
        // arrived menu before a new route or combat input replaces its child.
        const auto &view=c.session.read();
        if(!waypoint || !c.walk.active() || child.at("gameGeneration")!=view.gameGeneration ||
            child.at("areaGeneration")!=view.world.areaGeneration) return;
        const auto &activation=view.world.waypointActivation;
        const bool activated=!walkIntent.value("confirmMenu",false) && activation && activation->source==unit.id &&
            activation->generation==view.world.interactionGeneration;
        if(!activated && view.world.waypointSource!=unit.id) return;
    }
    const auto now=Clock::now();
    const bool door=walkIntent.value("purpose",std::string{})=="door";
    // Capture accepted interactions before combat/pause can discard a finished
    // child intent. The goal still waits for its separate real postcondition.
    if(door) {
        const auto found=c.session.read().world.units.find(unit);
        if(found!=c.session.read().world.units.end()) pendingDoor=std::pair{unit,found->second.assignmentRevision};
        doorDeadline=now+std::chrono::seconds(12);
        ++interactions;note("door-interaction-submitted",std::to_string(unit.id));cancelWalk(c);
    } else if(waypoint) {
        const auto found=c.session.read().world.units.find(unit);
        if(found!=c.session.read().world.units.end())
            pendingWaypoint=WaypointActivation{unit,found->second.assignmentRevision,walkIntent.at("waypointArea").get<uint16_t>(),walkIntent.value("confirmMenu",false)};
        waypointDeadline=now+std::chrono::seconds(12);++interactions;
        note("waypoint-activation-submitted",std::to_string(unit.id));cancelWalk(c);
    } else if(walkIntent.value("purpose",std::string{})=="exploration-pad" && localPad && localPad->unit==unit) {
        const auto &submission=child.at("submittedInteraction");
        if(submission.is_null()) throw std::runtime_error("Local pad has no actual interaction submission observation");
        localPad->from={submission.at("position").at("x").get<uint16_t>(),submission.at("position").at("y").get<uint16_t>()};
        localPad->discontinuity=submission.at("discontinuity").get<uint64_t>();
        localPad->requestRevision=submission.at("worldRevision").get<uint64_t>();
        localPad->deadline=now+std::chrono::seconds(20);localPad->waiting=true;
        ++interactions;note("local-pad-submitted",std::to_string(unit.id));cancelWalk(c);
    } else if(walkIntent.value("purpose",std::string{})=="quest-recover-stash") {
        QuestPending pending;pending.action=QuestPending::Action::RecoverOpen;pending.item=unit.id;
        pending.deadline=now+std::chrono::seconds(12);questRun.pending=pending;cancelWalk(c);
        note("quest-recovery-stash-submitted",std::to_string(unit.id));
    } else if(walkIntent.value("purpose",std::string{})=="supply-heal") {
        healingIssued=true;healingObserved=false;shopDeadline=now+std::chrono::seconds(12);cancelWalk(c);
        note("town-healing-submitted",std::to_string(unit.id));
    } else if(walkIntent.value("purpose",std::string{})=="supply-return" && supplyStage==SupplyStage::Return) {
        rememberTravel(c,unit);
        supplyPortalUsed=true;supplyPortalDeadline=now+std::chrono::seconds(15);cancelWalk(c);
        note("supply-return-submitted",std::to_string(unit.id));
    } else if(supplyStage==SupplyStage::None && (walkIntent.value("purpose",std::string{})=="quest-object" ||
        walkIntent.value("purpose",std::string{})=="quest-portal" || walkIntent.value("purpose",std::string{})=="quest-orb" || walkIntent.value("purpose",std::string{})=="quest-forge")) {
        const auto intent=walkIntent;observeQuestInteraction(c,intent);
    } else if(supplyStage==SupplyStage::None && goal.kind==Kind::Acquire && goal.sourceObject && target==unit && !objectIssued) {
        objectIssued=true;setActionDeadline(std::chrono::seconds(12));++interactions;
        note("item-source-submitted",goal.item);cancelWalk(c);
    } else if(travelPhase==TravelPhase::WarpExit && legUnit==unit) {
        rememberTravel(c,unit);
        travelPhase=TravelPhase::CrossWait;setActionDeadline(std::chrono::seconds(15));++interactions;
        note("warp-interaction-submitted",std::to_string(unit.id));cancelWalk(c);
    } else if((goal.kind==Kind::ReturnTown || goal.kind==Kind::Restock || supplyStage==SupplyStage::ToTown || (explorationReturn && !explorationReturn->returning)) && portalIssued && !portalUsed) {
        rememberTravel(c,unit);
        portalUsed=true;setActionDeadline(std::chrono::seconds(15));++interactions;
        note("town-portal-interaction-submitted",std::to_string(unit.id));cancelWalk(c);
    }
}
bool DebugAi::Impl::defendNavigation(Context &c,const Goal &goal) {
    using Owner=DebugActionOwner;
    if(c.moving || c.town.read().town || pendingDoor || pendingWaypoint || questRun.pending || pickup ||
        supplyStage!=SupplyStage::None || Clock::now()>=goalDeadline || (!c.walk.navigationSearch().prolongedWait() && !c.walk.blockedByUnit()) ||
        !canInput(c,Owner::Combat,DebugMotion|DebugSkill)) return false;
    if(!defendedTravel && c.walk.active() && walkIntent.is_object() && !walkIntent.value("interact",false)) {
        const auto kind=walkIntent.value("goal",std::string{});
        const auto purpose=walkIntent.value("purpose",std::string{});
        if(kind=="exit" || (kind=="point" && (walkIntent.value("exploration",false) || purpose.starts_with("known-target")))) {
            defendedTravel=walkIntent;defendedStep=questRun.step;
        }
    }
    if(canInput(c,Owner::Potion,DebugItem) && drink(c)) return true;
    return avoidHazards(c) || maintainArmor(c) || combat(c,goal,true,true);
}
void DebugAi::Impl::resumeDefendedTravel(Context &c) {
    if(!defendedTravel) return;
    auto intent=std::move(*defendedTravel);defendedTravel.reset();
    if(intent.value("goal",std::string{})=="exit" && area && requiredWaypointAreas.contains(*area) && !knownWaypointAreas.contains(*area)) return;
    if(questRun.step!=defendedStep || supplyStage!=SupplyStage::None || questRun.pending || pickup) return;
    // Only ordinary travel may be replayed. Preserve the actual outcome of
    // a child which finished while a stationary defensive cast was in flight.
    if(walkIntent==intent) return;
    if(walkIntent.is_object()) {
        const auto purpose=walkIntent.value("purpose",std::string{});
        if(!purpose.starts_with("combat-") && !retreatPoint && !combatRun.reposition && !combatRun.kitePoint &&
            !(walkIntent.value("goal",std::string{})=="point" && purpose.empty())) return;
    }
    releaseCombat(c);cancelWalk(c);combatTarget.reset();combatRun={};retreatPoint.reset();
    note("travel-resumed-after-defense",intent.value("goal",std::string{}));walk(c,std::move(intent));
}
bool DebugAi::Impl::walk(Context &c,Json intent) {
    intent["command"]="debug-walk";intent["timeoutSeconds"]=120;
    const bool continuing=c.walk.active() && walkIntent==intent;
    bool nearbyInteraction=false;
    if(intent.value("goal",std::string{})=="unit") {
        const OnlineUnitKey key{intent.at("unitType").get<uint8_t>(),intent.at("unitId").get<uint32_t>()};
        for(const auto &entry:c.town.read().mapTargets) if(entry.unit==key)
            nearbyInteraction=intent.value("interact",false) &&
                distance(c.position,{float(entry.position.x),float(entry.position.y)})<=6;
        const auto assigned=std::find_if(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &entry){return entry.unit==key;});
        if(assigned!=c.town.read().mapTargets.end() && !continuing && intent.value("interact",false)) {
            const auto &view=c.session.read();
            const auto states=c.combat.states().find({0,view.load.playerUnitId.value_or(0)});
            const bool locked=states!=c.combat.states().end() && states->second.decoded &&
                std::any_of(states->second.states.begin(),states->second.states.end(),[](const auto &state) {
                    return state.second.name=="uninterruptable";
                });
            if((c.supplies && c.supplies->castInputBusy.value_or(false)) || locked) {
                mobilityHold=true;wait("interaction-action-settle","Waiting for the normal skill/action lock before a new unit click");return false;
            }
            const Vec at{float(assigned->position.x),float(assigned->position.y)};
            const bool outsideNativeRange=std::abs(c.position.x-at.x)>50 || std::abs(c.position.y-at.y)>50;
            // All unit requests share the native pointer range. Approach a
            // distant door/NPC too, allowing the existing known-floor skill
            // planner to escape a blocked walking start before a unit walk.
            // Nearby doors/portals keep their interaction-perimeter planner;
            // their authored centers need not be standing cells.
            const bool pointApproach=assigned->interaction!=OnlineMapInteraction::Door &&
                assigned->interaction!=OnlineMapInteraction::Npc && assigned->interaction!=OnlineMapInteraction::Portal &&
                assigned->interaction!=OnlineMapInteraction::Exit &&
                (assigned->interaction!=OnlineMapInteraction::Waypoint || teleportAvailable(c)) &&
                distance(c.position,at)>8;
            bool blockedStart=false;
            if(distance(c.position,at)>8 && teleportAvailable(c)) {
                const auto *map=c.town.map();const auto origin=c.town.read().origin;
                if(map && origin) {
                    const Vec local=c.position-Vec{float(origin->x),float(origin->y)}+Vec{.5f,.5f};
                    blockedStart=!map->grid.nativeMovementSegment(local,local,playerMovement);
                }
            }
            // openDoor already selected a reachable perimeter. Keep that
            // child as owner even if a moving unit now overlaps the start.
            if(intent.value("purpose",std::string{})!="door" && (outsideNativeRange || pointApproach || blockedStart)) {
                const bool enterUnitRange=outsideNativeRange && c.town.read().town && !pointApproach;
                if(!approachPoint(c,assigned->position,"assigned interaction target",6,false,enterUnitRange))
                    explore(c,"reachable approach to assigned interaction target");
                return false;
            }
        }
        if(std::none_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &t){return t.unit==key;})) {
            const auto unit=c.session.read().world.units.find(key);
            if(unit!=c.session.read().world.units.end() && unit->second.position &&
                approachPoint(c,*unit->second.position,"client-observed assigned target")) return false;
            cancelWalk(c);wait("target-reassignment","Waiting for a current interactable client assignment");return false;
        }
    }
    if(intent.value("goal",std::string{})!="point") {
        bool isDoor=intent.value("purpose",std::string{})=="door";
        if(intent.value("goal",std::string{})=="unit") {
            const OnlineUnitKey unit{intent.at("unitType").get<uint8_t>(),intent.at("unitId").get<uint32_t>()};
            isDoor=isDoor || std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &t){
                return t.unit==unit && t.interaction==OnlineMapInteraction::Door;
            });
        }
        // An already adjacent portal/item/NPC must not be postponed in favour
        // of unrelated closed doors, taking us out of its assignment range.
        if(!isDoor && !nearbyInteraction && openDoor(c)) return false;
    }
    if(walkIntent!=intent) {
        cancelWalk(c);walkIntent=intent;
        if(intent.value("goal",std::string{})=="unit") {
            const OnlineUnitKey key{intent.at("unitType").get<uint8_t>(),intent.at("unitId").get<uint32_t>()};
            const auto unit=c.session.read().world.units.find(key);
            if(unit!=c.session.read().world.units.end() && travelRetries.contains({area.value_or(0),key,unit->second.assignmentRevision})) {
                intent["forceApproach"]=true;intent["changeStance"]=true;
            }
        }
        c.walk.command(intent,c.session,c.town,c.control,c.combat);++moveGoals;
        auto description=intent.at("goal").get<std::string>();
        if(description=="unit") description+=" "+std::to_string(intent.at("unitType").get<unsigned>())+"/"+
            std::to_string(intent.at("unitId").get<uint32_t>());
        else if(description=="point") description+=" "+std::to_string(intent.at("x").get<int>())+","+
            std::to_string(intent.at("y").get<int>());
        if(intent.contains("level")) description+=" level="+std::to_string(intent.at("level").get<int>());
        if(intent.contains("purpose")) description+=" "+intent.at("purpose").get<std::string>();
        behaviorLog.event(cursor,"walk-goal",description);
        return false;
    }
    const auto status=c.walk.snapshot();const auto value=status.at("state").get<std::string>();
    if(value=="completed") return true;
    if(value=="failed" && status.value("reason",std::string{})=="Target is no longer assigned in the client scene") {
        note("navigation-target-withdrawn");cancelWalk(c);return false;
    }
    if(value=="cancelled") {
        // The normal client can replace its scene binding during a walk or
        // teleport before the parent next samples the new area. Rebind the
        // child; only a real area observation can satisfy the travel goal.
        note("navigation-rebound",status.value("reason",std::string{}));cancelWalk(c);return false;
    }
    if(value=="failed") {
        const auto purpose=intent.value("purpose",std::string{});
        if(purpose=="initial-waypoint-memory" || purpose=="activate-waypoint") {
            const auto &source=status.at("unit");
            if(source.is_object()) {
                const OnlineUnitKey key{source.at("type").get<uint8_t>(),source.at("id").get<uint32_t>()};
                const auto &world=c.session.read().world;
                const auto unit=world.units.find(key);
                deferredWaypoint=WaypointDeferral{key,unit==world.units.end()?0:unit->second.assignmentRevision,
                    Clock::now()+std::chrono::seconds(30),c.position};
                if(world.waypointRequested==key.id) c.session.use_waypoint(0);
                note("waypoint-approach-deferred",status.value("reason",std::string{}));cancelWalk(c);return false;
            }
        }
        if((intent.value("goal",std::string{})=="waypoint" || purpose=="travel-waypoint") && travelDestination && !pendingWaypoint) {
            note("waypoint-approach-deferred",status.value("reason",std::string{}));
            unavailableWaypointArea=area;waypointDestination.reset();travelPhase=TravelPhase::Route;
            cancelWalk(c);return false;
        }
        if(intent.value("goal",std::string{})=="point" &&
            (intent.value("purpose",std::string{})=="known-target-teleport" || intent.value("directTeleport",false))) {
            recordFrontierAttempt(c,{intent.at("x").get<uint16_t>(),intent.at("y").get<uint16_t>()},
                "failed",status.value("reason",std::string{}));
            note("travel-landing-deferred","The selected movement could not reach this landing");
            cancelWalk(c);return false;
        }
        const auto detail=status.value("reason",std::string{});
        const bool recoverable=detail.starts_with("No observed player progress") ||
            detail=="No integer input target on the collision-checked leg" ||
            detail=="No client collision path to target" ||
            detail=="Path produced no reachable movement segment" ||
            detail=="Server did not open the requested interaction" ||
            detail=="Interaction opening had no result after quick clicks and local approaches";
        if(recoverable && intent.value("goal",std::string{})=="unit" &&
            !questRun.pending && !shopIssued && !potionRequest) {
            const OnlineUnitKey key{intent.at("unitType").get<uint8_t>(),intent.at("unitId").get<uint32_t>()};
            const auto &world=c.session.read().world;const auto unit=world.units.find(key);
            // Only an opening or an approach that has not submitted an object
            // operation can be restarted. Never replay a reward/transaction.
            if(unit!=world.units.end() && (key.type==1 || status.at("submittedInteraction").is_null())) {
                auto &attempt=navigationRetries[{area.value_or(0),key,unit->second.assignmentRevision}];
                if(attempt<2) {
                    ++attempt;note("navigation-local-recovery",std::to_string(key.id)+" attempt "+std::to_string(attempt)+": "+detail);
                    if(key.type==1 && world.npcRequested==key.id) c.session.close_npc();
                    cancelWalk(c);walkIntent=intent;
                    auto retry=intent;retry["forceApproach"]=true;retry["resyncApproach"]=attempt==2;
                    c.walk.command(retry,c.session,c.town,c.control,c.combat);++moveGoals;return false;
                }
            }
        }
        if(intent.value("goal",std::string{})=="unit" && travelPhase==TravelPhase::WarpExit && legUnit && legDestination &&
            status.at("submittedInteraction").is_null()) {
            auto &failures=failedWalkEdges[{area.value_or(0),*legDestination}];failures=std::min(failures+1,16u);
            note("travel-connection-approach-failed",std::to_string(*legDestination)+": "+detail);
            cancelWalk(c);travelRoute.clear();travelPhase=TravelPhase::Route;legDestination.reset();legUnit.reset();
            exitRetryAt=Clock::now()+std::chrono::seconds(5);return false;
        }
        cancelWalk(c);
        throw std::runtime_error("AI navigation: "+detail);
    }
    return false;
}
bool DebugAi::Impl::walkPoint(Context &c,OnlinePoint point,bool exploration) {
    if(c.walk.active() && walkIntent.is_object() && walkIntent.value("goal",std::string{})=="point") {
        const int x=walkIntent.value("x",0),y=walkIntent.value("y",0);
        if(walkIntent.value("exploration",false)==exploration && std::abs(x-int(point.x))<=2 && std::abs(y-int(point.y))<=2) return false;
    }
    return walk(c,{{"goal","point"},{"x",point.x},{"y",point.y},{"interact",false},{"exploration",exploration}});
}
std::vector<int> DebugAi::Impl::route(const Context &c,int from,int to) const {
    auto graph=c.town.navigationConnections();
    // Special quest portals (notably Tristram's return red portal) are not
    // Levels.Vis links. Add only actually assigned outgoing connections.
    if(c.town.read().area) for(const auto &target:c.town.read().mapTargets)
        if(ordinaryConnection(target,c.session.read())) graph[*c.town.read().area].push_back(*target.destination);
    std::map<int,int> previous{{from,from}};std::map<int,float> costs{{from,0}};
    using Pending=std::pair<float,int>;
    std::priority_queue<Pending,std::vector<Pending>,std::greater<Pending>> queue;queue.emplace(0,from);
    while(!queue.empty()) {
        const auto [cost,current]=queue.top();queue.pop();
        if(cost>costs.at(current)) continue;
        if(current==to) break;
        const auto edges=graph.find(current);if(edges==graph.end()) continue;
        for(int next:edges->second) {
            const std::optional<Vec> start=current==from && area==from?std::optional<Vec>{c.position}:std::nullopt;
            const float candidate=cost+travelLegCost(c,current,next,start);
            if(!costs.contains(next) || candidate<costs.at(next)) {
                costs[next]=candidate;previous[next]=current;queue.emplace(candidate,next);
            }
        }
    }
    if(!previous.contains(to)) return {};
    std::vector<int> result;for(int p=to;;p=previous.at(p)) {result.push_back(p);if(p==from) break;}
    std::reverse(result.begin(),result.end());return result;
}
float DebugAi::Impl::navigationCost(const Context &c,int level,std::optional<Vec> start,std::optional<OnlinePoint> target) const {
    // Debug planning estimates in subtile units, not native game rules or
    // assertions that unknown terrain is traversable. Unknown exits include
    // one extra observed-room span for searching; a transition costs 8.
    float span=0;unsigned count=0;
    for(const auto &room:c.town.navigationRooms()) if(room.level==level && room.loaded) {
        span+=(float(room.width)+float(room.height))*.5f;++count;
    }
    const float roomSpan=count?span/count:40.f;
    if(!target) return roomSpan*2;
    if(!start) return roomSpan;
    float length=distance(*start,{float(target->x),float(target->y)});
    if(area==level && !teleportAvailable(c,false) && c.town.map() && c.town.read().origin) {
        const auto origin=*c.town.read().origin;const Vec offset{float(origin.x)-.5f,float(origin.y)-.5f};
        // Static collision gives a stable distance estimate; dynamic units
        // remain the actual walker's responsibility, never a permanent veto.
        const auto path=c.walk.navigationSearch().path(c.town.map()->grid,*start-offset,Vec{float(target->x),float(target->y)}-offset,false,playerMovement,true);
        if(path.empty()) return length+roomSpan;
        length=0;Vec previous=*start-offset;
        for(const auto point:path) {length+=distance(previous,point);previous=point;}
    }
    return length;
}
float DebugAi::Impl::travelLegCost(const Context &c,int from,int to,std::optional<Vec> start) const {
    const float cost=navigationCost(c,from,start,c.town.knownConnectionPosition(uint16_t(from),uint16_t(to)))+8;
    const auto failed=failedWalkEdges.find({from,to});
    if(failed==failedWalkEdges.end() || failed->second<2) return cost;
    // The route estimate covers only one approach. Repeated failed crossings
    // also cost a return trip. Keep the leg usable when it is the only route.
    return cost+2*std::max(80.f,cost)*failed->second;
}
float DebugAi::Impl::routeCost(const Context &c,const std::vector<int> &path,std::optional<Vec> start) const {
    if(path.empty()) return std::numeric_limits<float>::infinity();
    if(!start && area==path.front()) start=c.position;
    float cost=0;
    for(size_t i=1;i<path.size();++i) {
        cost+=travelLegCost(c,path[i-1],path[i],start);
        const auto entry=c.town.knownConnectionPosition(uint16_t(path[i]),uint16_t(path[i-1]));
        start=entry?std::optional<Vec>{{float(entry->x),float(entry->y)}}:std::nullopt;
    }
    return cost;
}
bool DebugAi::Impl::approachTownGate(Context &c,uint16_t destination) {
    if(!c.town.read().town || !definitions->harrogathGate || area!=definitions->area("Act 5 - Town") ||
        destination!=definitions->area("Act 5 - Siege 1")) return false;
    const Selector gate{2,*definitions->harrogathGate,"Harrogath main gate"};
    const auto &world=c.session.read().world;
    for(const auto &[key,unit]:world.units) {
        if(key.type!=gate.type || unit.classId!=gate.identity || !unit.position || !unit.mode) continue;
        const auto hint=std::tuple{*area,gate.type,gate.identity,unit.position->x,unit.position->y};
        if(*unit.mode!=0) {
            checkedHints.insert(hint);
            if(c.walk.active() && walkIntent.is_object() && walkIntent.value("purpose",std::string{})=="door" &&
                walkIntent.value("unitId",0u)==key.id) cancelWalk(c);
            return false;
        }
        const auto &targets=c.town.read().mapTargets;
        if(std::none_of(targets.begin(),targets.end(),[&](const auto &target) {
            return target.unit==key && target.interaction==OnlineMapInteraction::Door;
        })) break;
        checkedHints.erase(hint);
        const bool outsidePointerRange=std::abs(c.position.x-unit.position->x)>50 || std::abs(c.position.y-unit.position->y)>50;
        if(outsidePointerRange && approachPoint(c,*unit.position,gate.name,6)) return true;
        wait("town-exit-gate","Approaching the assigned closed main gate");
        if(walk(c,{{"goal","unit"},{"unitType",key.type},{"unitId",key.id},{"interact",true},{"purpose","door"}}))
            observeChild(c,goals[cursor]);
        return true;
    }
    // Returning to a real last-seen gate position can restore its assignment.
    // The remembered position alone never issues an interaction.
    return approachKnown(c,gate);
}
bool DebugAi::Impl::openDoor(Context &c,bool frontier) {
    if(teleportAvailable(c)) return false; // A closed ordinary door does not divide teleportable floor.
    // Keep the accepted approach until its child finishes. Re-ranking nearby
    // doors from the moving position can alternate two doors every decision
    // and cancel both paths before either interaction reaches the server.
    if(c.walk.active() && walkIntent.is_object() && walkIntent.value("purpose",std::string{})=="door") {
        wait("open-door","Continuing the current normal door approach");return true;
    }
    std::optional<OnlineMapTarget> best;float nearest=std::numeric_limits<float>::max();
    const auto *map=c.town.map();const auto origin=c.town.read().origin;if(!map || !origin) return false;
    const Vec offset{float(origin->x),float(origin->y)};
    const auto &grid=map->grid;
    for(const auto &entry:c.town.read().mapTargets) if(entry.interaction==OnlineMapInteraction::Door) {
        const auto unit=c.session.read().world.units.find(entry.unit);
        if(unit==c.session.read().world.units.end() || unit->second.mode!=0 || operatedDoors.contains({entry.unit,unit->second.assignmentRevision})) continue;
        // Door presentation also includes quest barriers (e.g. Operate71 at
        // the summit). Ordinary doors and Harrogath's reversible main gate
        // belong here; quest barriers keep their own completion contract.
        bool ordinary=false;
        for(size_t row=0;row<definitions->objects.rows().size();++row)
            if(definitions->objects.number(row,"Id")==unit->second.classId) {
                const auto operation=definitions->objects.number(row,"OperateFn");
                ordinary=operation==8 || operation==61;break;
            }
        if(!ordinary) continue;
        const float d=distance(c.position,{float(entry.position.x),float(entry.position.y)});
        // Door handling is a local aid to the current route, not a new
        // exploration goal. An assigned door across town must not divert a
        // waypoint/NPC approach merely because a partial path can reach it.
        const float nearby=std::max(8.f,float(std::max(entry.collisionWidth,entry.collisionHeight))/2+4);
        if(d>=nearest || (!frontier && d>nearby)) continue;
        if(c.town.interactionReady(c.session.read(),entry.unit,c.position)) {
            nearest=d;best=entry;continue;
        }
        const Vec wanted{float(entry.position.x),float(entry.position.y)};
        const auto path=c.walk.navigationSearch().path(grid,c.position-offset+Vec{.5f,.5f},wanted-offset+Vec{.5f,.5f},true,playerMovement,true);
        bool reachable=!path.empty() &&
            c.town.interactionReady(c.session.read(),entry.unit,path.back()+offset-Vec{.5f,.5f});
        if(!reachable) {
            // A long slime door can clip a center path against its blocked
            // corner, although another real point on its perimeter is usable.
            // Use the same normal interaction geometry as the child walker.
            std::vector<Vec> approaches;
            const int width=std::clamp(entry.collisionWidth/2+4,4,16),height=std::clamp(entry.collisionHeight/2+4,4,16);
            for(int y=int(entry.position.y)-height;y<=int(entry.position.y)+height;++y)
                for(int x=int(entry.position.x)-width;x<=int(entry.position.x)+width;++x) {
                    const Vec at{float(x),float(y)},local=at-offset+Vec{.5f,.5f};
                    if(grid.nativeMovementSegment(local,local,playerMovement) &&
                        c.town.interactionReady(c.session.read(),entry.unit,at)) approaches.push_back(local);
                }
            std::stable_sort(approaches.begin(),approaches.end(),[&](Vec a,Vec b){return distance(c.position,a+offset)<distance(c.position,b+offset);});
            for(const auto at:approaches) if(c.walk.navigationSearch().reachable(grid,c.position-offset+Vec{.5f,.5f},at,playerMovement,true)) {
                reachable=true;break;
            }
        }
        if(!reachable) continue;
        nearest=d;best=entry;
    }
    if(!best) return false;
    wait("open-door","Approaching an assigned closed door through the normal client interaction");
    if(walk(c,{{"goal","unit"},{"unitType",best->unit.type},{"unitId",best->unit.id},{"interact",true},{"purpose","door"}})) {
        const auto unit=c.session.read().world.units.find(best->unit);
        if(unit!=c.session.read().world.units.end()) pendingDoor=std::pair{best->unit,unit->second.assignmentRevision};
        doorDeadline=Clock::now()+std::chrono::seconds(12);
        ++interactions;note("door-interaction-submitted",std::to_string(best->unit.id));cancelWalk(c);
    }
    return true;
}
void DebugAi::Impl::observeWaypointMemory(const Context &c) {
    const auto &view=c.session.read();const auto &world=view.world;
    if(waypointMemoryGame!=view.gameGeneration) {
        waypointMemoryGame=view.gameGeneration;waypointMemoryRevision=0;waypointActivationGeneration.reset();
        knownWaypointAreas.clear();waypointTownArrivals.clear();
    }
    // Normal act travel activates the destination town in SUnitNpc / ObjMode.
    // This is AI route knowledge, never a locally fabricated protocol bit.
    // Observe each arrival once so a later full 63 can correct that knowledge.
    if(c.town.read().town && area && waypointTownArrivals.insert(*area).second && !knownWaypointAreas.empty())
        for(size_t row=0;row<definitions->levels.rows().size();++row) {
            const auto id=definitions->levels.number(row,"Id"),number=definitions->levels.number(row,"Waypoint");
            if(id==*area && number && *number>=0 && *number<112) knownWaypointAreas.insert(*area);
        }
    if(world.waypointHistory && waypointMemoryRevision!=world.waypointHistoryRevision) {
        knownWaypointAreas.clear();
        for(size_t row=0;row<definitions->levels.rows().size();++row) {
            const auto id=definitions->levels.number(row,"Id"),number=definitions->levels.number(row,"Waypoint");
            if(id && *id>0 && *id<=UINT16_MAX && number && *number>=0 && *number<112 &&
                ((*world.waypointHistory)[1+*number/16] & (1u<<(*number&15)))) knownWaypointAreas.insert(uint16_t(*id));
        }
        waypointMemoryRevision=world.waypointHistoryRevision;
        note("waypoint-memory-synchronized","Actual personal 63 replaced the prior route knowledge");
    }
    const auto &receipt=world.waypointActivation;
    if(receipt && receipt->generation==world.interactionGeneration && waypointActivationGeneration!=receipt->generation) {
        if(const auto level=c.town.assignedUnitLevel({2,receipt->source})) {
            waypointActivationGeneration=receipt->generation;
            if(knownWaypointAreas.insert(*level).second)
                note("waypoint-unlocked","area="+std::to_string(*level)+" unit="+std::to_string(receipt->source));
        }
    }
    // Remember a point passed during a cast or a quest interaction until the
    // scheduler can act on it. Its assignment must belong to this level.
    if(c.town.read().nativeMapReady && area && !c.town.read().town && !knownWaypointAreas.contains(*area) && nearbyWaypointArea!=area) {
        for(const auto &target:c.town.read().mapTargets) {
            if(target.interaction==OnlineMapInteraction::Waypoint && c.town.assignedUnitLevel(target.unit)==area &&
                std::abs(c.position.x-target.position.x)<=50 && std::abs(c.position.y-target.position.y)<=50) {
                nearbyWaypointArea=area;break;
            }
        }
    }
}
bool DebugAi::Impl::activateWaypoint(Context &c,bool required) {
    const auto &world=c.session.read().world;
    if(!area || (!required && world.npcRequested) || pendingWaypoint || closeWaypoint) return false;
    const auto purpose=walkIntent.is_object()?walkIntent.value("purpose",std::string{}):std::string{};
    const bool initial=purpose=="initial-waypoint-memory";
    if(c.town.read().town && (initial || !waypointMemoryRevision)) {
        if(deferredWaypoint && Clock::now()<deferredWaypoint->retryAt) return false;
        if(world.waypointSource && world.waypointHistory) {
            closeWaypoint=world.waypointSource;cancelWalk(c);return true;
        }
        if(!waypointMemoryRevision) {
            wait("waypoint-memory-initial","Reading personal waypoint history in town");
            walk(c,{{"goal","waypoint"},{"purpose","initial-waypoint-memory"}});return true;
        }
        cancelWalk(c);
    }
    if(knownWaypointAreas.contains(*area)) {
        if(purpose=="activate-waypoint") {
            if(world.waypointSource==walkIntent.at("unitId").get<uint32_t>()) closeWaypoint=world.waypointSource;
            cancelWalk(c);
            return closeWaypoint.has_value();
        }
        return false;
    }
    if(world.waypointSource || (c.town.read().town && !required)) return false;
    if(deferredWaypoint && Clock::now()>=deferredWaypoint->retryAt) deferredWaypoint.reset();
    if(c.walk.active() && purpose=="activate-waypoint") {
        wait("activate-waypoint","Continuing the waypoint approach");
        if(canInput(c,DebugActionOwner::Mobility,DebugMotion|DebugSkill)) mobility(c);
        return true;
    }
    if(!required && nearbyWaypointArea!=area) return false;
    const OnlineMapTarget *selected=nullptr;
    float nearest=std::numeric_limits<float>::infinity();
    for(const auto &entry:c.town.read().mapTargets) {
        if(entry.interaction!=OnlineMapInteraction::Waypoint || c.town.assignedUnitLevel(entry.unit)!=area) continue;
        const auto unit=world.units.find(entry.unit);
        if(unit==world.units.end() || !unit->second.mode) continue;
        if(deferredWaypoint && deferredWaypoint->unit==entry.unit && deferredWaypoint->assignment==unit->second.assignmentRevision) continue;
        const float span=distance(c.position,{float(entry.position.x),float(entry.position.y)});
        if(span<nearest) {nearest=span;selected=&entry;}
    }
    if(!selected) {
        const auto seen=c.town.knownWaypointPosition(*area);
        if(deferredWaypoint && required) {
            wait("waypoint-retry","Waiting to retry the unconfirmed waypoint in this area");return true;
        }
        if(!deferredWaypoint && seen && missingWaypointAt!=seen) {
            if(approachPoint(c,*seen,"previously observed waypoint",4)) {
                if(canInput(c,DebugActionOwner::Mobility,DebugMotion|DebugSkill)) mobility(c);
                return true;
            }
            // Explore for a fresh assignment instead of orbiting a stale hint.
            missingWaypointAt=seen;
        }
        return false;
    }
    missingWaypointAt.reset();
    wait("activate-waypoint","Requesting the current area's waypoint");
    const Json intent{{"goal","unit"},{"unitType",selected->unit.type},{"unitId",selected->unit.id},
        {"interact",true},{"purpose","activate-waypoint"},{"waypointArea",*area},{"confirmMenu",world.units.at(selected->unit).mode!=0}};
    if(walk(c,intent)) observeChild(c,goals[cursor]);
    if(canInput(c,DebugActionOwner::Mobility,DebugMotion|DebugSkill)) mobility(c);
    return true;
}
bool DebugAi::Impl::waypointBeforeDeparture(Context &c) {
    if(!area || !requiredWaypointAreas.contains(*area) || knownWaypointAreas.contains(*area)) return false;
    if(waypointDepartureArea!=area) {
        if(walkIntent.is_object() && walkIntent.value("goal",std::string{})=="exit") cancelWalk(c);
        waypointDepartureArea=area;note("waypoint-required-before-departure",std::to_string(*area));
    }
    if(pendingWaypoint || closeWaypoint) return true;
    if(c.session.read().world.waypointSource) {closeWaypoint=c.session.read().world.waypointSource;return true;}
    if(!activateWaypoint(c,true)) {
        explore(c,"required waypoint before leaving this area");
        if(canInput(c,DebugActionOwner::Mobility,DebugMotion|DebugSkill)) mobility(c);
    }
    return true;
}
void DebugAi::Impl::refreshNavigationAttempts(const Context &c) {
    const auto now=Clock::now();const auto revision=c.town.navigationRevision();
    std::erase_if(frontierAttempts,[&](const auto &entry) {
        const auto &attempt=entry.second;
        return now>=attempt.retryAt || (!attempt.retainAfterMove && (attempt.geometryRevision!=revision || distance(c.position,attempt.position)>=8));
    });
    if(searchGeometryRevision!=revision || !searchPosition || distance(*searchPosition,c.position)>=8 || now>=searchRetry) {
        searchGeometryRevision=revision;searchPosition=c.position;unavailableRooms.clear();searchRetry=now+std::chrono::seconds(5);
    }
}
void DebugAi::Impl::recordFrontierAttempt(const Context &c,OnlinePoint point,std::string outcome,std::string detail,std::optional<OnlinePoint> relayGoal) {
    refreshNavigationAttempts(c);
    // Attempts control short-term replanning only. Neither completion nor
    // failure writes discovery; the rendered viewport is its sole producer.
    if(frontierAttempts.size()>=256 && !frontierAttempts.contains({point.x,point.y}))
        frontierAttempts.erase(std::min_element(frontierAttempts.begin(),frontierAttempts.end(),[](const auto &a,const auto &b) {
            return a.second.retryAt<b.second.retryAt;
        }));
    const bool reached=distance(c.position,{float(point.x),float(point.y)})<=3;
    const bool exploration=walkIntent.is_object() && walkIntent.value("exploration",false);
    if(!reached || exploration || relayGoal==point) relayGoal.reset();
    const bool retain=reached && (exploration || relayGoal.has_value());
    frontierAttempts[{point.x,point.y}]={c.position,c.town.navigationRevision(),Clock::now()+std::chrono::seconds(retain?30:5),
        reached,retain,std::move(outcome),std::move(detail),relayGoal};
    note("navigation-frontier-attempt",std::to_string(point.x)+","+std::to_string(point.y)+(reached?" reached":" not reached"));
}
bool DebugAi::Impl::frontierCoolingDown(OnlinePoint point,float radius,std::optional<OnlinePoint> relayGoal) const {
    return std::any_of(frontierAttempts.begin(),frontierAttempts.end(),[&](const auto &entry) {
        return (!entry.second.relayGoal || entry.second.relayGoal==relayGoal) && Clock::now()<entry.second.retryAt && distance({float(point.x),float(point.y)},
            {float(entry.first.first),float(entry.first.second)})<radius;
    });
}
void DebugAi::Impl::deferViewpoint(const Context &c,OnlinePoint at) {
    if(!area) return;
    if(viewpointAttempts.size()>=256) viewpointAttempts.erase(std::min_element(viewpointAttempts.begin(),viewpointAttempts.end(),
        [](const auto &a,const auto &b){return a.second.retryAt<b.second.retryAt;}));
    viewpointAttempts[{at.x,at.y}]={roomCoverage(c.town.navigationRooms(),*area,at),Clock::now()+std::chrono::seconds(30)};
    note("observation-viewpoint-deferred",std::to_string(at.x)+","+std::to_string(at.y));
}
bool DebugAi::Impl::explore(Context &c,std::string purpose,bool recoveryOnly) try {
    const auto *map=c.town.map();const auto origin=c.town.read().origin;if(!map || !origin || !area) return false;
    if(localPad && exploreLocalPad(c)) return false;
    const Vec offset{float(origin->x),float(origin->y)},from=c.position-offset+Vec{.5f,.5f};
    // Rank observation candidates against persistent known terrain. A moving
    // pet must not invalidate the entire deferred candidate batch each frame.
    // The child still validates every issued segment against current units.
    const auto &grid=map->grid;
    const auto &rooms=c.town.navigationRooms();mapCacheBuilds=c.town.navigationCacheBuilds();
    // Partial native paths depend on the current starting point. A failed
    // frontier is not proof that its room stays unreachable after movement
    // or after a dynamic obstruction clears, even with unchanged terrain.
    refreshNavigationAttempts(c);
    knownExploredRooms=size_t(std::count_if(rooms.begin(),rooms.end(),[](const auto &room){return room.exploredTiles!=0;}));
    const auto observedCells=c.town.navigationMemory().observedCells;
    std::erase_if(viewpointAttempts,[&](const auto &entry){
        return Clock::now()>=entry.second.retryAt || roomCoverage(rooms,*area,{entry.first.first,entry.first.second})>entry.second.observedCells;
    });
    // Keep the route outside the child command: a budget yield between two
    // legs must retain both its destination and the positions already tried.
    if(viewpointRoute && walkIntent.is_object() && !walkIntent.value("explorationViewpoint",false)) viewpointRoute.reset();
    if(viewpointRoute) {
        auto &route=*viewpointRoute;
        bool failed=false;
        if(walkIntent.is_object() && walkIntent.value("explorationViewpoint",false)) {
            if(c.walk.active()) {wait("explore-staging","Completing the current observation leg");return false;}
            const auto state=walkState(c);
            failed=state=="failed";
            if(failed || state=="completed")
                recordFrontierAttempt(c,{walkIntent.at("x").get<uint16_t>(),walkIntent.at("y").get<uint16_t>()},state,c.walk.snapshot().value("reason",std::string{}));
            cancelWalk(c);approachHint.reset();approachFrontier.reset();
        }
        if(observedCells>route.observed) {route.origins.clear();route.observed=observedCells;}
        const bool revisited=std::any_of(route.origins.begin(),route.origins.end(),[&](Vec at){return distance(c.position,at)<=3;});
        if(!failed && !revisited && route.origins.size()<8 && distance(c.position,{float(route.target.x),float(route.target.y)})>3 &&
            approachPoint(c,route.target,"reconstructed boundary viewpoint",3,true)) {
            if(walkIntent.is_object() && walkIntent.value("exploration",false)) {
                walkIntent["explorationViewpoint"]=true;route.origins.push_back(c.position);
            } else viewpointRoute.reset();
            return false;
        }
        deferViewpoint(c,route.target);viewpointRoute.reset();
    }
    if(walkIntent.is_object() && walkIntent.contains("backtrackRoom")) {
        if(c.walk.active()) {wait("explore-backtrack","Returning through reconstructed rooms to another frontier");return false;}
        const auto completedIntent=walkIntent;const auto outcome=walkState(c);
        const auto detail=c.walk.snapshot().value("reason",std::string{});
        if(outcome=="completed" || outcome=="failed") {
            recordFrontierAttempt(c,{completedIntent.at("x").get<uint16_t>(),completedIntent.at("y").get<uint16_t>()},outcome,detail);
            const auto observed=c.town.navigationMemory().observedCells;
            if(observed<=walkIntent.value("observedBefore",size_t{})) {
                const auto room=walkIntent.at("backtrackRoom");
                const auto key=std::tuple{room.at(0).get<int>(),room.at(1).get<int>(),room.at(2).get<int>()};
                const auto destination=std::find_if(rooms.begin(),rooms.end(),[&](const auto &r) {
                    return std::tuple{r.level,r.x,r.y}==key;
                });
                const bool entered=destination!=rooms.end() && destination->loaded &&
                    c.position.x>=destination->x && c.position.x<destination->x+destination->width &&
                    c.position.y>=destination->y && c.position.y<destination->y+destination->height;
                if(backtrackAttempts.size()>=256 && !backtrackAttempts.contains(key))
                    throw std::runtime_error("Backtrack attempt capacity exhausted; retaining exploration scene");
                // Only new coverage in this destination can invalidate its
                // no-discovery result. A few newly seen cells elsewhere must
                // not restart every old dead-end approach.
                backtrackAttempts[key]={destination==rooms.end()?0:destination->exploredTiles,
                    Clock::now()+(entered?std::chrono::seconds(120):std::chrono::seconds(30)),
                    entered?"arrived without new observation":"approach produced no further route"};
                const unsigned legs=completedIntent.value("backtrackLegs",1u);
                if(outcome=="completed" && !entered && destination!=rooms.end() && legs<8) {
                    const OnlinePoint center{uint16_t(destination->x+destination->width/2),uint16_t(destination->y+destination->height/2)};
                    cancelWalk(c);approachHint.reset();approachFrontier.reset();
                    if(approachPoint(c,center,"retained exploration junction",3,completedIntent.value("exploration",false))) {
                        if(walkIntent.is_object() && walkIntent.value("goal",std::string{})=="point") {
                            walkIntent["backtrackRoom"]=room;walkIntent["observedBefore"]=observed;walkIntent["backtrackLegs"]=legs+1;
                        }
                        return false;
                    }
                }
                note("exploration-backtrack-no-discovery",std::to_string(std::get<1>(key))+","+std::to_string(std::get<2>(key)));
            }
        }
        note("exploration-backtrack-finished",outcome);cancelWalk(c);
    }
    if(recoveryOnly) return false; // Return control to the original known goal after this recovery completes.
    const bool clearing=supplyStage==SupplyStage::None && cursor<goals.size() && goals[cursor].kind==Kind::Quest &&
        questRun.step<goals[cursor].steps.size() && goals[cursor].steps[questRun.step].action==QuestAction::Clear &&
        goals[cursor].steps[questRun.step].area==area;
    const bool precise=clearing;
    const float arrival=3.f;
    const auto observation=c.town.navigationObservation();
    // In isometric coordinates a viewport is a rectangle. Index unseen tile
    // centres by diagonal so each candidate counts only rows inside its view.
    std::map<int,std::vector<int>> unseenBands;
    size_t frontierTiles=0,coverageTiles=0;
    std::set<std::tuple<int,int,int>> representedRooms;
    for(const auto &room:map->terrain.rooms)
        representedRooms.emplace(room.level,room.x*5+origin->x,room.y*5+origin->y);
    size_t emptyLoadedRooms=0;
    std::set<std::tuple<int,int,int>> terrainRooms;
    std::map<std::tuple<int,int,int>,std::pair<bool,bool>> roomObservation;
    if(!clearing && observation) {
        const auto &seen=*observation;
        const auto observed=[&](int x,int y) {
            return x>=0 && y>=0 && x<seen.width && y<seen.height && seen.seen[size_t(y)*seen.width+x];
        };
        std::set<std::pair<int,int>> cells;
        // Only actual reconstructed DT1 instances are candidates. A blank
        // rectangle outside the terrain is not unexplored walkable ground.
        for(const auto &tile:map->terrain.instances) {
            if(tile.type==13 || tile.type==15 || (tile.flags&8) ||
                map->terrain.rooms.at(tile.room).level!=*area) continue;
            const auto &room=map->terrain.rooms.at(tile.room);
            terrainRooms.emplace(room.level,room.x*5+origin->x,room.y*5+origin->y);
            const auto &image=map->terrain.tiles.at(size_t(tile.tile))->image;
            if(image.width<=0 || image.height<=0) continue;
            const int x=tile.x+origin->x/5-seen.tileX,y=tile.y+origin->y/5-seen.tileY;
            if(x<0 || y<0 || x>=seen.width || y>=seen.height) continue;
            auto &coverage=roomObservation[{room.level,room.x*5+origin->x,room.y*5+origin->y}];
            coverage.first=true;coverage.second|=!observed(x,y);
            if(observed(x,y)) continue;
            const bool border=observed(x-1,y) || observed(x+1,y) || observed(x,y-1) || observed(x,y+1);
            if((!border && !seen.outdoor) || !cells.emplace(x,y).second) continue;
            if(border) ++frontierTiles;
            const int worldX=(x+seen.tileX)*5,worldY=(y+seen.tileY)*5;
            unseenBands[worldX-worldY].push_back(worldX+worldY+5);
            ++coverageTiles;
        }
        for(auto &[diagonal,values]:unseenBands) std::sort(values.begin(),values.end());
    }
    const auto visibleGain=[&](Vec at) {
        size_t gain=0;
        if(unseenBands.empty()) return gain;
        const float u=at.x-at.y,v=at.x+at.y;
        const float margin=observation->outdoor?0.f:5.f;
        const float rx=std::max(0.f,observation->halfExtent.x-margin),ry=std::max(0.f,observation->halfExtent.y-margin);
        for(auto row=unseenBands.lower_bound(int(std::ceil(u-rx)));row!=unseenBands.end() && row->first<=u+rx;++row) {
            const auto &values=row->second;
            gain+=std::upper_bound(values.begin(),values.end(),v+ry)-std::lower_bound(values.begin(),values.end(),v-ry);
        }
        return gain;
    };
    for(const auto &room:rooms) if(room.level==*area) {
        const auto actual=roomObservation.find({room.level,room.x,room.y});
        // A represented, reconstructed room can contain only void/removed
        // DT1 cells. It offers no observation target. This skips a candidate,
        // never writes seen coverage or removes the room from route graphs.
        if(!clearing && observation && room.loaded && representedRooms.contains({room.level,room.x,room.y}) &&
            !terrainRooms.contains({room.level,room.x,room.y})) {
            visited.emplace(room.level,room.x,room.y);++emptyLoadedRooms;
        }
        // Caverns/platforms contain real empty space. Missing DT1 cells are
        // not unseen terrain that warrants revisiting an already viewed room.
        const bool observed=!clearing && (room.exploredTiles>=size_t((room.width/5)*(room.height/5)) ||
            (room.loaded && actual!=roomObservation.end() && actual->second.first && !actual->second.second));
        if(!clearing && observation && observation->outdoor && room.loaded &&
            actual!=roomObservation.end() && actual->second.second)
            visited.erase({room.level,room.x,room.y});
        else if(observed) visited.emplace(room.level,room.x,room.y);
        // Clearing follows assigned monsters. Once the host has subscribed
        // this room, its empty center is not a useful exploration objective.
        const auto population=populationMemory.find(*area);
        if(clearing && population!=populationMemory.end() && Clock::now()>=population->second.readyAt &&
            population->second.rooms.contains({room.level,room.x,room.y})) visited.emplace(room.level,room.x,room.y);
    }
    const bool ownApproach=exploring && interest && walkIntent.is_object() && walkIntent.value("goal",std::string{})=="point" &&
        walkIntent.value("x",0)==interest->x && walkIntent.value("y",0)==interest->y;
    const bool boundaryApproach=ownApproach && walkIntent.value("purpose",std::string{})=="exploration-boundary";
    const bool boundaryObserved=boundaryApproach && observation &&
        visibleGain({float(interest->x),float(interest->y)})==0;
    if(exploring && interest && (boundaryObserved ||
        (!boundaryApproach && visited.contains(*exploring)) || distance(c.position,{float(interest->x),float(interest->y)})<=arrival)) {
        // Exploration needs an observation position, not an exact arrival at
        // a clipped wall/frontier pixel. Interaction goals keep their precision.
        recordFrontierAttempt(c,*interest,"observation-satisfied");
        if(ownApproach) cancelWalk(c);
        exploring.reset();interest.reset();
    }
    // Combat or a known exit/NPC approach may have replaced the child. Its
    // completion is not evidence that the old exploration point was visited.
    if(exploring && !ownApproach) {exploring.reset();interest.reset();}
    if(openDoor(c)) return false;
    if(exploring && c.walk.active()) {wait("explore",std::move(purpose));return false;}
    if(exploring && walkIntent.is_object()) {
        const auto state=walkState(c);
        if(interest && (state=="completed" || state=="failed")) {
            recordFrontierAttempt(c,*interest,state,c.walk.snapshot().value("reason",std::string{}));
            if(!clearing && state=="failed") deferViewpoint(c,*interest);
        }
        if(state=="failed") {
            // This is an exploration approach, not an interaction result.
            unavailableRooms.insert(*exploring);note("exploration-approach-unavailable",c.walk.snapshot().value("reason",std::string{}));
        }
        cancelWalk(c);exploring.reset();
    }
    std::optional<std::tuple<int,int,int>> chosen;std::optional<OnlinePoint> point;
    float best=std::numeric_limits<float>::max();
    std::vector<const RemoteTown::NavigationRoom *> candidates;
    for(const auto &room:rooms) if(room.level==*area && !visited.contains({room.level,room.x,room.y}) &&
        !unavailableRooms.contains({room.level,room.x,room.y})) candidates.push_back(&room);
    const auto priority=[&](const auto *room) {
        return distance(c.position,{float(room->x)+float(room->width)/2,float(room->y)+float(room->height)/2})+(room->exploredTiles?0.f:8.f);
    };
    std::stable_sort(candidates.begin(),candidates.end(),[&](const auto *a,const auto *b){return priority(a)<priority(b);});
    searchDiagnostics={{"decision",decisions},{"area",*area},{"purpose",purpose},
        {"from",{c.position.x,c.position.y}},{"candidateRooms",candidates.size()},
        {"emptyLoadedRooms",emptyLoadedRooms},
        {"candidates",Json::array()},{"selected",nullptr}};
    const bool canTeleport=teleportAvailable(c);
    if(!clearing && observation) {
        searchDiagnostics["strategy"]=observation->outdoor?"outdoor-coverage":"visible-boundary";
        searchDiagnostics["frontierTiles"]=frontierTiles;searchDiagnostics["coverageTiles"]=coverageTiles;
        searchDiagnostics["viewHalfExtent"]={observation->halfExtent.x,observation->halfExtent.y};
        struct Viewpoint {Vec local;size_t gain;float score;};
        const TeleportRooms neighborhood(c);
        std::vector<Viewpoint> viewpoints;
        // Sample standing cells, not room centers. A good viewpoint exposes a
        // long unseen boundary from inside its sight range without hugging it.
        for(int y=2;coverageTiles && y<grid.height;y+=5) for(int x=2;x<grid.width;x+=5) {
            const auto standing=grid.nativeSpawn({float(x)+.5f,float(y)+.5f},2,playerMovement);
            if(!standing) continue;
            const Vec local=*standing,world=local+offset;
            if(std::none_of(map->terrain.rooms.begin(),map->terrain.rooms.end(),[&](const auto &room) {
                return room.level==*area && local.x>=room.x*5 && local.y>=room.y*5 &&
                    local.x<(room.x+room.width)*5 && local.y<(room.y+room.height)*5;
            })) continue;
            const float span=distance(c.position,world);
            if(span<4) continue;
            if(frontierCoolingDown(nativePoint(world),3)) continue;
            if(std::any_of(viewpointAttempts.begin(),viewpointAttempts.end(),[&](const auto &entry) {
                return distance(world,{float(entry.first.first),float(entry.first.second)})<8;
            })) continue;
            const size_t gain=visibleGain(world);if(!gain) continue;
            // Prefer useful coverage per movement cost. Exact paths are
            // checked below; predicted coverage never changes discovery.
            const float cost=canTeleport?std::ceil(span/float(mobilityPolicy.distance))*mobilityPolicy.distance:span;
            const int clearance=c.walk.navigationSearch().wallClearance(grid,local);
            viewpoints.push_back({local,gain,(cost+8+DebugNavigationSearch::wallCost(clearance))/float(gain)});
        }
        std::stable_sort(viewpoints.begin(),viewpoints.end(),[](const auto &a,const auto &b){return a.score<b.score;});
        searchDiagnostics["candidateViewpoints"]=viewpoints.size();
        // Candidates already rank coverage, travel and wall clearance. Take
        // the best legal one-cast observation before pricing walking detours.
        if(canTeleport) {
            const auto threats=travelLandingThreats(c);
            const auto collision=c.town.navigationGrid(c.session.read());
            for(const auto &candidate:viewpoints) {
                const auto endpoint=nativePoint(candidate.local+offset);
                const Vec at{float(endpoint.x),float(endpoint.y)};
                const auto trial=std::tuple{*area,endpoint.x,endpoint.y,endpoint.x,endpoint.y};
                if(distance(c.position,at)>mobilityPolicy.distance || !neighborhood.contains(at) ||
                    (mobilityTrials.contains(trial) && mobilityTrials.at(trial)>=2) ||
                    !collision.walkable(at-offset+Vec{.5f,.5f},playerMovement) || crowdedTravelLanding(threats,at)) continue;
                // A viewpoint inside a previously visited room can still see
                // a new boundary from a different angle.
                const auto owner=std::find_if(rooms.begin(),rooms.end(),[&](const auto &r) {
                    return r.level==*area && at.x>=r.x && at.x<r.x+r.width && at.y>=r.y && at.y<r.y+r.height;
                });
                if(owner==rooms.end()) continue;
                exploring=std::tuple{owner->level,owner->x,owner->y};interest=endpoint;
                searchDiagnostics["selected"]={endpoint.x,endpoint.y};searchDiagnostics["result"]="direct-teleport-boundary";
                searchDiagnostics["expectedNewTiles"]=candidate.gain;
                wait("explore-boundary",std::move(purpose));
                walk(c,{{"goal","point"},{"x",endpoint.x},{"y",endpoint.y},{"interact",false},
                    {"purpose","exploration-boundary"},{"exploration",true},{"directTeleport",true}});
                return false;
            }
        }
        unsigned probes=0,pendingRoutes=0;
        std::vector<Vec> testedViewpoints;
        for(const auto &candidate:viewpoints) {
            if(std::any_of(testedViewpoints.begin(),testedViewpoints.end(),[&](Vec at){return distance(at,candidate.local)<8;})) continue;
            if(probes++>=24 || (point && candidate.score>=best)) break;
            testedViewpoints.push_back(candidate.local);
            if(canTeleport) continue;
            const auto endpoint=nativePoint(candidate.local+offset);
            auto &trace=searchDiagnostics["candidates"].emplace_back(Json{{"endpoint",{endpoint.x,endpoint.y}},
                {"newBoundaryTiles",candidate.gain},{"score",candidate.score}});
            ++pathSearches;
            DebugNavigationSearch::TravelRoute route;
            try {route=c.walk.navigationSearch().travelRoute(grid,from,candidate.local+Vec{.5f,.5f});}
            catch(const DebugSearchPending &) {
                trace["result"]="background";
                if(++pendingRoutes>=4) break;
                continue;
            }
            const auto &path=route.points;
            if(path.empty()) {trace["result"]="no-complete-known-route";continue;}
            float length=distance(from,route.origin);Vec previous=route.origin;
            for(const auto node:path) {length+=distance(previous,node);previous=node;}
            const float cost=length;
            const int clearance=c.walk.navigationSearch().wallClearance(grid,candidate.local);
            const float score=(cost+8+DebugNavigationSearch::wallCost(clearance))/float(candidate.gain);
            trace["wallClearance"]=clearance;
            trace["pathLength"]=length;trace["pathScore"]=score;trace["result"]="candidate";
            if(score>=best) continue;
            for(const auto &room:rooms) if(room.level==*area && endpoint.x>=room.x && endpoint.x<room.x+room.width &&
                endpoint.y>=room.y && endpoint.y<room.y+room.height) {
                best=score;point=endpoint;chosen={room.level,room.x,room.y};searchDiagnostics["expectedNewTiles"]=candidate.gain;break;
            }
        }
        if(point) {
            searchDiagnostics["selected"]={point->x,point->y};searchDiagnostics["result"]="observe-boundary";
            exploring=chosen;interest=point;wait("explore-boundary",std::move(purpose));
            walk(c,{{"goal","point"},{"x",point->x},{"y",point->y},{"interact",false},
                {"purpose","exploration-boundary"},{"exploration",true}});
            return false;
        }
        if(pendingRoutes) throw DebugSearchPending{};
        if(canTeleport) {
            // A useful known viewpoint need not fit in one cast. Approach it
            // through currently reconstructed legal landing cells rather than
            // rejecting an otherwise traversable chain of Teleport inputs.
            unsigned staged=0;
            for(const auto &local:testedViewpoints) {
                if(staged++>=8) break;
                const auto viewpoint=nativePoint(local+offset);
                if(approachPoint(c,viewpoint,"reconstructed boundary viewpoint",3,true)) {
                    if(walkIntent.is_object() && walkIntent.value("exploration",false)) {
                        viewpointRoute=ViewpointRoute{viewpoint,observedCells,{c.position}};
                        walkIntent["explorationViewpoint"]=true;
                    }
                    searchDiagnostics["result"]="approach-known-viewpoint";return false;
                }
            }
        }
    }
    unsigned probes=0;
    if(clearing) for(const auto *candidate:candidates) {
        if(probes++>=8) break;
        const auto &room=*candidate;const auto key=std::tuple{room.level,room.x,room.y};
        auto &trace=searchDiagnostics["candidates"].emplace_back(Json{{"room",{room.level,room.x,room.y}},
            {"loaded",room.loaded},{"exploredTiles",room.exploredTiles}});
        const Vec center{float(room.x)+float(room.width)/2,float(room.y)+float(room.height)/2};
        const Vec wanted=center-offset+Vec{.5f,.5f};
        Vec goal{std::clamp(wanted.x,.5f,float(grid.width)-.5f),std::clamp(wanted.y,.5f,float(grid.height)-.5f)};
        if(room.loaded) {
            if(const auto open=grid.nativeSpawn(goal,8,playerMovement)) goal=*open;
            else {
                // Arcane rooms can have a void center and a real platform at
                // an edge. Inspect their already reconstructed cells instead
                // of repeatedly backtracking toward the same unusable center.
                std::optional<Vec> floor;float weight=std::numeric_limits<float>::max();bool complete=true;
                for(int y=room.y;y<room.y+room.height;++y) for(int x=room.x;x<room.x+room.width;++x) {
                    const Vec local{float(x)-offset.x+.5f,float(y)-offset.y+.5f};
                    if(grid.movementMask(int(local.x),int(local.y))==0xffff) {complete=false;continue;}
                    if(!grid.nativeMovementSegment(local,local,playerMovement)) continue;
                    const float score=distance({float(x),float(y)},center);
                    if(score<weight) {weight=score;floor=local;}
                }
                if(floor) goal=*floor;
                else {trace["result"]="no-known-floor";if(complete) visited.insert(key);unavailableRooms.insert(key);continue;}
            }
        }
        // A loaded room has known collision: require a real route rather
        // than walking to a wall merely because it is closer to its center.
        std::deque<Vec> path;
        if(canTeleport) {
            if(room.loaded && grid.walkable(goal,playerMovement)) path.push_back(goal);
        } else {
            ++pathSearches;path=c.walk.navigationSearch().path(grid,from,goal,!room.loaded,playerMovement,true);
        }
        if(path.empty()) {trace["result"]="no-complete-known-route";unavailableRooms.insert(key);continue;}
        trace["endpoint"]={path.back().x+offset.x-.5f,path.back().y+offset.y-.5f};
        if(!room.loaded && !debugUnknownFrontier(grid,path.back())) {trace["result"]="known-wall-without-unknown-frontier";unavailableRooms.insert(key);continue;}
        const auto endpoint=nativePoint(path.back()+offset);
        const Vec to{float(endpoint.x),float(endpoint.y)};const auto progress=distance(c.position,to);
        if(progress<=arrival || frontierCoolingDown(endpoint,precise?3.f:12.f)) {
            trace["result"]="nearby-or-cooling-down-frontier";unavailableRooms.insert(key);continue;
        }
        const float score=progress+.25f*distance(to,center)+(room.exploredTiles?0.f:8.f);
        trace["result"]="candidate";trace["score"]=score;
        if(score<best) {best=score;chosen={room.level,room.x,room.y};point=endpoint;}
    }
    if(!point) {
        // A closed door can be the only remaining observation frontier. Only
        // exhausted exploration may approach a farther reachable door; normal
        // NPC/waypoint/exit input must not detour to every assigned door.
        if(openDoor(c,true)) {searchDiagnostics["result"]="reachable-closed-door-frontier";return false;}
        // A native client only retains collision for its active rooms. A
        // distant unexplored branch cannot always be reached by clipping its
        // center to that local rectangle: a wall can require backtracking
        // through a previously visited junction. Use the existing RoomEx near
        // graph for guidance, without activating rooms or assuming open edges.
        size_t root=rooms.size();
        for(size_t i=0;i<rooms.size();++i) {
            const auto &r=rooms[i];
            if(r.level==*area && r.loaded && c.position.x>=r.x && c.position.x<r.x+r.width &&
                c.position.y>=r.y && c.position.y<r.y+r.height) {root=i;break;}
        }
        std::vector<size_t> previous(rooms.size(),rooms.size()),depth(rooms.size());
        const auto connections=observedRoomConnections(rooms,*area);
        std::map<size_t,Vec> directLandings;
        std::queue<size_t> pending;
        if(root<rooms.size()) {previous[root]=root;pending.push(root);}
        while(!pending.empty()) {
            const auto index=pending.front();pending.pop();
            if(!rooms[index].loaded && !rooms[index].exploredTiles) continue; // Observe this frontier before extending through it.
            for(const auto next:connections[index]) if(next<rooms.size() && rooms[next].level==*area && previous[next]==rooms.size() &&
                possibleRoomPassage(c.town,rooms[index],rooms[next],canTeleport?mobilityPolicy.distance:0,observedRoomPassages)) {
                previous[next]=index;depth[next]=depth[index]+1;pending.push(next);
            }
            // Nine samples can miss a narrow riverbank. Before declaring the
            // actual player room isolated, check the cells one cast can reach.
            if(index==root && pending.empty() && canTeleport) {
                const auto range=mobilityPolicy.distance;
                for(const auto next:rooms[root].near) {
                    if(next>=rooms.size() || previous[next]!=rooms.size()) continue;
                    const auto &r=rooms[next];if(!r.loaded || r.level!=*area) continue;
                    const Vec center{r.x+r.width*.5f,r.y+r.height*.5f};
                    float bestLanding=std::numeric_limits<float>::max();
                    for(int y=std::max(r.y,int(std::ceil(c.position.y-range)));y<r.y+r.height && y<=c.position.y+range;++y)
                        for(int x=std::max(r.x,int(std::ceil(c.position.x-range)));x<r.x+r.width && x<=c.position.x+range;++x) {
                            const Vec at{float(x),float(y)};
                            const float score=distance(at,center);
                            const float span=distance(at,c.position);
                            if(score>=bestLanding || span>range || span<=3 ||
                                !grid.walkable(x-origin->x,y-origin->y,playerMovement) || frontierCoolingDown(nativePoint(at),3)) continue;
                            directLandings[next]=at;bestLanding=score;
                        }
                    if(directLandings.contains(next)) {previous[next]=root;depth[next]=1;pending.push(next);}
                }
                searchDiagnostics["directNeighborLandings"]=directLandings.size();
            }
        }
        std::map<size_t,size_t> junctions;
        const auto observedCells=c.town.navigationMemory().observedCells;
        std::erase_if(backtrackAttempts,[&](const auto &entry) {
            const auto room=std::find_if(rooms.begin(),rooms.end(),[&](const auto &r) {
                return std::tuple{r.level,r.x,r.y}==entry.first;
            });
            return room==rooms.end() || room->exploredTiles>entry.second.observedCells || Clock::now()>=entry.second.retryAt;
        });
        for(size_t i=0;i<rooms.size();++i) {
            const auto &destination=rooms[i];
            if(i==root || previous[i]==rooms.size() || visited.contains({destination.level,destination.x,destination.y}) ||
                backtrackAttempts.contains({destination.level,destination.x,destination.y})) continue;
            size_t next=i;
            // A remote frontier need not fit in one cast. Keep its room as
            // the destination and advance through the first known room on the route.
            while(next!=root && (!rooms[next].loaded || (canTeleport && previous[next]!=root))) next=previous[next];
            const auto found=junctions.find(next);
            const bool unseen=destination.exploredTiles==0;
            if(found==junctions.end() || (unseen!=(rooms[found->second].exploredTiles==0)?unseen:depth[i]<depth[found->second])) junctions[next]=i;
        }
        std::optional<std::tuple<int,int,int>> backtrackRoom;
        bool backtrackTeleport=false,backtrackUnseen=false;
        const TeleportRooms neighborhood(c);
        struct IndirectViewpoint {float score;OnlinePoint at;size_t destination;};
        std::vector<IndirectViewpoint> indirect;
        for(const auto &[next,destination]:junctions) {
            const auto &r=rooms[next];
            const auto &other=rooms[destination];
            const bool unseen=other.exploredTiles==0;
            if(point && backtrackUnseen && !unseen) continue;
            const Vec onward{float(other.x)+float(other.width)/2,float(other.y)+float(other.height)/2};
            std::optional<IndirectViewpoint> staged;
            // River banks and walls can cover a room center while a side is
            // open. Probe its quarters, including our own room when moving
            // toward its unrevealed neighbor needs a closer observation point.
            std::vector<Vec> samples;
            if(const auto landing=directLandings.find(next);landing!=directLandings.end()) samples.push_back(landing->second);
            for(const float y:{.25f,.5f,.75f}) for(const float x:{.25f,.5f,.75f})
                samples.push_back({float(r.x)+float(r.width)*x,float(r.y)+float(r.height)*y});
            for(const Vec sample:samples) {
                const auto open=grid.nativeSpawn(sample-offset+Vec{.5f,.5f},8,playerMovement);
                if(!open) continue;
                std::deque<Vec> path;
                if(!canTeleport) {++pathSearches;path=c.walk.navigationSearch().path(grid,from,*open,false,playerMovement,true);}
                if(path.empty() && !canTeleport) continue;
                const auto endpoint=nativePoint((path.empty()?*open:path.back())+offset);
                const Vec to{float(endpoint.x),float(endpoint.y)};
                if(to.x<r.x || to.y<r.y || to.x>=r.x+r.width || to.y>=r.y+r.height ||
                    distance(to,sample)>10 || distance(c.position,to)<=3 ||
                    (next==root && distance(to,onward)+2>=distance(c.position,onward))) continue;
                if(frontierCoolingDown(endpoint,3)) continue;
                const float wall=clearing?0.f:DebugNavigationSearch::wallCost(c.walk.navigationSearch().wallClearance(grid,*open));
                const float score=float(depth[destination])*40+distance(to,onward)+.25f*distance(c.position,to)+(path.empty()?20.f:0.f)+wall;
                if(canTeleport && (!neighborhood.contains(to) || distance(c.position,to)>mobilityPolicy.distance)) {
                    if(!staged || score<staged->score) staged=IndirectViewpoint{score,endpoint,destination};
                    continue;
                }
                if((unseen && !backtrackUnseen) || score<best) {
                    best=score;point=endpoint;backtrackRoom={other.level,other.x,other.y};backtrackTeleport=path.empty();backtrackUnseen=unseen;
                    searchDiagnostics["backtrackDestination"]={{"room",{other.level,other.x,other.y}},
                        {"observedTiles",other.exploredTiles},{"roomSteps",depth[destination]},{"loaded",other.loaded},
                        {"viaRoom",{r.level,r.x,r.y,r.width,r.height}}};
                }
            }
            if(staged) indirect.push_back(*staged);
        }
        if(!point && !indirect.empty()) {
            std::stable_sort(indirect.begin(),indirect.end(),[](const auto &a,const auto &b){return a.score<b.score;});
            for(const auto &candidate:indirect) {
                if(!approachPoint(c,candidate.at,"next room toward unseen terrain",3,true)) continue;
                const auto &dest=rooms[candidate.destination];
                if(walkIntent.is_object() && walkIntent.value("goal",std::string{})=="point") {
                    walkIntent["backtrackRoom"]={dest.level,dest.x,dest.y};
                    walkIntent["observedBefore"]=observedCells;
                }
                searchDiagnostics["result"]="staged-room-observation";return false;
            }
        }
        // The hub is a separate route. It remains usable when this arm has
        // no local backtrack point and the other arms are outside the scene.
        const bool arcane=*area==definitions->area("Act 2 - Arcane");
        const bool exhaustedOutdoor=!point && !clearing && observation && observation->outdoor &&
            !c.town.read().town && supplyStage==SupplyStage::None;
        if(!explorationReturn && !questRun.pending && !questRun.worldWait && !portalIssued &&
            !questRun.encounterAnchored && root<rooms.size() && !candidates.empty() &&
            (arcane || exhaustedOutdoor) && knownWaypointAreas.contains(*area)) {
            if(const auto hub=c.town.knownWaypointPosition(*area)) {
                const Vec at{float(hub->x),float(hub->y)};
                const float span=distance(c.position,at);
                const bool towardHub=point && distance({float(point->x),float(point->y)},at)+20<span;
                const auto &source=rooms[root];
                const bool otherRoom=at.x<source.x || at.y<source.y || at.x>=source.x+source.width || at.y>=source.y+source.height;
                const bool useful=exhaustedOutdoor ? otherRoom && span>8 : span>std::max(80.f,float(mobilityPolicy.distance)*4);
                if(useful && (!point || towardHub)) {
                    // Changing dead-end rooms alone does not justify another
                    // outdoor portal. Require new area coverage after a return.
                    const auto key=std::tuple{source.level,arcane?source.x:-1,arcane?source.y:-1};
                    size_t coverage=0;
                    for(const auto &r:rooms) if(r.level==*area) coverage+=r.exploredTiles;
                    const auto prior=hubReturnCoverage.find(key);
                    if(!c.town.read().townPortalSkills.empty() &&
                        (prior==hubReturnCoverage.end() || coverage>prior->second)) {
                        hubReturnCoverage[key]=coverage;
                        explorationReturn=ExplorationReturn{*area,false,Clock::now()+std::chrono::seconds(120),visited};
                        cancelWalk(c);portalAttempted=portalIssued=portalUsed=false;oldPortals.clear();
                        searchDiagnostics["result"]="town-portal-waypoint-shortcut";
                        note("exploration-return-shortcut",point?"Shorter return to hub":"No local route to remaining rooms; returning to hub");
                        return false;
                    }
                    if(arcane && !point && approachPoint(c,*hub,"observed Arcane hub",8,true)) {
                        searchDiagnostics["result"]="approach-exploration-hub";return false;
                    }
                }
            }
        }
        if(point) {
            searchDiagnostics["selected"]={point->x,point->y};searchDiagnostics["result"]="backtrack";
            exploring.reset();interest.reset();
            note("exploration-backtrack",std::to_string(point->x)+","+std::to_string(point->y));
            wait("explore-backtrack","Returning through reconstructed rooms to another frontier");
            const auto &[level,x,y]=*backtrackRoom;
            walk(c,{{"goal","point"},{"x",point->x},{"y",point->y},{"interact",false},{"purpose","exploration-backtrack"},{"exploration",!clearing},
                {"backtrackRoom",{level,x,y}},{"observedBefore",observedCells},{"directTeleport",backtrackTeleport}});
            return false;
        }
        if(exploreLocalPad(c)) {searchDiagnostics["result"]="assigned-local-transport";return false;}
        searchDiagnostics["rooms"]=Json::array();
        for(size_t i=0;i<rooms.size();++i) if(rooms[i].level==*area) {
            const auto &r=rooms[i];
            searchDiagnostics["rooms"].push_back({{"index",i},{"bounds",{r.x,r.y,r.width,r.height}},
                {"loaded",r.loaded},{"observedTiles",r.exploredTiles},{"visited",visited.contains({r.level,r.x,r.y})},
                {"connected",previous[i]!=rooms.size()},{"near",r.near}});
        }
        searchDiagnostics["rootRoom"]=root<rooms.size()?Json(root):Json(nullptr);
        searchDiagnostics["result"]="no-useful-reachable-frontier";
        wait("explore-wait","No reachable unvisited reconstructed room for "+purpose+"; awaiting normal room updates");return false;
    }
    searchDiagnostics["selected"]={point->x,point->y};searchDiagnostics["result"]="explore";
    exploring=chosen;interest=point;wait("explore",std::move(purpose));walkPoint(c,*point,!clearing);return false;
} catch(const DebugSearchPending &) {
    c.walk.navigationSearch().noteTravelWait();throw;
}
bool DebugAi::Impl::approachKnown(Context &c,const Selector &selector) {
    if(!area) return false;
    const auto hint=selector.type==5?c.town.knownConnectionPosition(*area,selector.identity):
        c.town.knownTargetPosition(*area,selector.type,selector.identity);if(!hint) return false;
    const auto key=std::tuple{*area,selector.type,selector.identity,hint->x,hint->y};
    if(checkedHints.contains(key)) return false;
    if(distance(c.position,{float(hint->x),float(hint->y)})<=8) {checkedHints.insert(key);note("known-target-not-reassigned",selector.name);return false;}
    return approachPoint(c,*hint,selector.name);
}
bool DebugAi::Impl::approachPoint(Context &c,OnlinePoint hint,std::string purpose,float vicinity,bool exploration,bool enterUnitRange) try {
    refreshNavigationAttempts(c);
    const Vec wanted{float(hint.x),float(hint.y)};if(distance(c.position,wanted)<=vicinity) return false;
    if(approachDetour && *approachDetour!=hint) approachDetour.reset();
    if(!exploration && ((!walkIntent.is_object() && viewpointRoute) || (walkIntent.is_object() &&
        (walkIntent.value("explorationViewpoint",false) || walkIntent.contains("backtrackRoom"))))) {
        // A recovery route may first move away from the observed goal. Let
        // its owner consume intermediate completion and finish that route;
        // greedy re-approach must not replace it at the river bank every tick.
        if(openDoor(c)) return true;
        explore(c,"retained recovery before approaching "+purpose,true);return true;
    }
    if(approachHint && *approachHint==hint && approachFrontier && walkIntent.is_object() &&
        walkIntent.value("goal",std::string{})=="point" && walkIntent.value("x",0)==approachFrontier->x &&
        walkIntent.value("y",0)==approachFrontier->y) {
        if(c.walk.active()) {wait("known-target-approach","Completing the current partial approach to "+purpose);return true;}
        const auto status=walkState(c);
        if(status=="completed" || status=="failed") {
            if(status=="failed" && walkIntent.value("purpose",std::string{})=="known-target-teleport")
                approachDetour=hint;
            recordFrontierAttempt(c,*approachFrontier,status,c.walk.snapshot().value("reason",std::string{}),hint);
            cancelWalk(c);approachHint.reset();approachFrontier.reset();
            if(status=="failed") return false;
            // Reaching an intermediate approach is not a reason to explore
            // an unrelated room. Re-evaluate the same observed target from
            // the new position; its next legal landing can now be available.
        }
    }
    approachHint.reset();approachFrontier.reset();
    // An unreachable known target can yield a different partial endpoint at
    // every decision. Let the exploration approach finish before probing it
    // again, rather than replacing that route with another wall-edge point.
    if(exploring && interest && c.walk.active() && walkIntent.is_object() &&
        walkIntent.value("goal",std::string{})=="point" && walkIntent.value("x",0)==interest->x &&
        walkIntent.value("y",0)==interest->y) {
        wait("explore","Completing the current reconstructed approach before retrying "+purpose);return true;
    }
    const auto *map=c.town.map();const auto origin=c.town.read().origin;if(!map || !origin) return false;
    if(openDoor(c)) return true;
    const Vec offset{float(origin->x),float(origin->y)},local=wanted-offset+Vec{.5f,.5f};
    const Vec goal{std::clamp(local.x,.5f,float(map->grid.width)-.5f),std::clamp(local.y,.5f,float(map->grid.height)-.5f)};
    const auto &grid=map->grid;
    const auto from=c.position-offset+Vec{.5f,.5f};
    const bool canTeleport=teleportAvailable(c);
    std::deque<Vec> path;
    Vec pathOrigin=from;
    const auto preferredLanding=canTeleport && !approachDetour?teleportLanding(c,wanted,exploration,hint):std::nullopt;
    if(!preferredLanding) {
        auto route=c.walk.navigationSearch().travelRoute(grid,from,goal,true);
        pathOrigin=route.origin;path=std::move(route.points);
    }
    approachDiagnostics={{"decision",decisions},{"area",*area},{"purpose",purpose},
        {"from",{c.position.x,c.position.y}},{"target",{hint.x,hint.y}},{"result","no-useful-route"}};
    const auto endpoint=path.empty()?hint:nativePoint(path.back()+offset);
    const bool partial=path.empty() || distance({float(endpoint.x),float(endpoint.y)},wanted)>vicinity;
    if(!partial) approachDetour.reset();
    approachDiagnostics["endpoint"]={endpoint.x,endpoint.y};approachDiagnostics["partial"]=partial;
    if(enterUnitRange && !path.empty()) {
        const auto stage=townClickApproach(path,pathOrigin,offset,hint,c.town.navigationGrid(c.session.read()));
        if(stage && !frontierCoolingDown(*stage,3,hint)) {
            exploring.reset();interest.reset();
            walk(c,{{"goal","point"},{"x",stage->x},{"y",stage->y},{"interact",false},{"purpose","town-unit-approach"}});
            approachHint=hint;approachFrontier=stage;
            approachDiagnostics["endpoint"]={stage->x,stage->y};approachDiagnostics["result"]="enter-unit-range";
            wait("known-target-approach","Walking into pointer range of the assigned unit");return true;
        }
    }
    const auto backtrack=[&]() {
        // An observed destination may be in a released room. A clipped ray
        // toward it can meet a wall even though a previously explored junction
        // leads there. RoomEx near links guide the return; every actual input
        // still needs a complete path through current client collision.
        const auto &rooms=c.town.navigationRooms();size_t root=rooms.size(),destination=rooms.size();
        for(size_t i=0;i<rooms.size();++i) {
            const auto &r=rooms[i];if(r.level!=*area) continue;
            const auto inside=[&](Vec at){return at.x>=r.x && at.y>=r.y && at.x<r.x+r.width && at.y<r.y+r.height;};
            if(r.loaded && inside(c.position)) root=i;
            if(inside(wanted)) destination=i;
        }
        if(root==rooms.size() || destination==rooms.size() || root==destination) return false;
        std::vector<size_t> previous(rooms.size(),rooms.size());std::queue<size_t> pending;
        const auto connections=observedRoomConnections(rooms,*area);
        previous[root]=root;pending.push(root);
        while(!pending.empty() && previous[destination]==rooms.size()) {
            const auto i=pending.front();pending.pop();
            if(!rooms[i].loaded && !rooms[i].exploredTiles) continue;
            for(const auto next:connections[i]) if(next<rooms.size() && rooms[next].level==*area && previous[next]==rooms.size() &&
                possibleRoomPassage(c.town,rooms[i],rooms[next],canTeleport?mobilityPolicy.distance:0,observedRoomPassages)) {
                previous[next]=i;pending.push(next);
            }
        }
        if(previous[destination]==rooms.size()) return false;
        std::vector<size_t> chain;
        for(size_t i=destination;;i=previous[i]) {chain.push_back(i);if(i==root) break;}
        std::reverse(chain.begin(),chain.end());
        const TeleportRooms neighborhood(c);
        const auto threats=canTeleport?travelLandingThreats(c):std::vector<Vec>{};
        size_t last=0;while(last+1<chain.size() && rooms[chain[last+1]].loaded) ++last;
        for(size_t n=last+1;n>0;--n) {
            const size_t i=n-1;const auto &r=rooms[chain[i]];
            const auto &next=rooms[chain[std::min(i+1,chain.size()-1)]];
            const Vec onward=i+1<chain.size()?Vec{float(next.x)+next.width*.5f,float(next.y)+next.height*.5f}:wanted;
            std::optional<OnlinePoint> selected;float best=std::numeric_limits<float>::max();bool directTeleport=false;
            for(const float y:{.25f,.5f,.75f}) for(const float x:{.25f,.5f,.75f}) {
                const Vec sample{float(r.x)+r.width*x,float(r.y)+r.height*y};
                const auto open=grid.nativeSpawn(sample-offset+Vec{.5f,.5f},8,playerMovement);if(!open) continue;
                const auto point=nativePoint(*open+offset);const Vec at{float(point.x),float(point.y)};
                if(at.x<r.x || at.y<r.y || at.x>=r.x+r.width || at.y>=r.y+r.height ||
                    distance(c.position,at)<=3 || (i==0 && distance(at,onward)+2>=distance(c.position,onward)) ||
                    frontierCoolingDown(point,3,hint)) continue;
                const float wall=exploration?DebugNavigationSearch::wallCost(c.walk.navigationSearch().wallClearance(map->grid,*open)):0.f;
                const float score=distance(at,onward)+.25f*distance(c.position,at)+wall;
                if(score>=best) continue;
                const bool jump=canTeleport && distance(c.position,at)<=mobilityPolicy.distance &&
                    neighborhood.contains(at) && !crowdedTravelLanding(threats,at);
                if(canTeleport && !jump) continue;
                if(!jump) {
                    // Rank junctions on stable terrain. The walker validates the selected
                    // leg against live footprints before sending movement.
                    const auto &component=c.walk.navigationSearch().connectionComponent(map->grid,from);
                    if(!component.cells[size_t(open->y)*grid.width+int(open->x)]) continue;
                }
                best=score;selected=point;directTeleport=jump;
            }
            if(!selected) continue;
            exploring.reset();interest.reset();
            wait("known-target-backtrack","Returning through observed room connections to "+purpose);
            walk(c,{{"goal","point"},{"x",selected->x},{"y",selected->y},{"interact",false},{"purpose","known-target-backtrack"},
                {"exploration",exploration},{"directTeleport",directTeleport}});
            approachHint=hint;approachFrontier=selected;
            approachDetour=hint;
            approachDiagnostics["endpoint"]={selected->x,selected->y};approachDiagnostics["result"]="known-room-backtrack";
            approachDiagnostics["roomSteps"]=chain.size();
            approachDiagnostics["observedRoomPassages"]=observedRoomPassages.size();
            approachDiagnostics["roomChain"]=Json::array();
            for(const auto index:chain) approachDiagnostics["roomChain"].push_back({rooms[index].x,rooms[index].y,rooms[index].loaded});
            return true;
        }
        return false;
    };
    // A detour is still needed after its first intermediate point. Moving
    // closer to the final target again can undo the same river crossing on
    // every leg; retain this choice across combat until a complete route exists.
    if(partial && approachDetour) return backtrack();
    if(!canTeleport && exploration && partial && distance(c.position,wanted)>mobilityPolicy.distance && backtrack()) return true;
    if(preferredLanding) {
        const auto landing=nativePoint(*preferredLanding);
        exploring.reset();interest.reset();
        wait("known-target-teleport","Approaching the observed target across known terrain with normal Teleport");
        walk(c,{{"goal","point"},{"x",landing.x},{"y",landing.y},{"interact",false},{"purpose","known-target-teleport"},{"exploration",exploration}});
        approachHint=hint;approachFrontier=landing;
        approachDiagnostics["endpoint"]={landing.x,landing.y};approachDiagnostics["result"]="known-landing-across-wall";
        return true;
    }
    if(path.empty() || distance(c.position,path.back()+offset)<std::min(4.f,vicinity)) return backtrack();
    if(partial && !debugUnknownFrontier(grid,path.back())) {
        approachDiagnostics["result"]="known-wall-without-unknown-frontier";return backtrack();
    }
    if(frontierCoolingDown(endpoint,partial?8.f:3.f,hint)) {
        approachDiagnostics["result"]="frontier-retry-cooldown";return backtrack();
    }
    if(walkIntent.is_object() && walkState(c)=="failed") {cancelWalk(c);return false;}
    wait("known-target-approach","Approaching the last client-observed position: "+purpose);
    // Keep a submitted approach even when its endpoint reaches the target.
    // Assignment can appear/disappear while moving between active rooms;
    // recomputing that path every tick must not alternate it with exploration.
    exploring.reset();interest.reset();
    walkPoint(c,endpoint,exploration);
    approachHint=hint;approachFrontier=endpoint;
    approachDiagnostics["result"]="approach";
    return true;
} catch(const DebugSearchPending &) {
    c.walk.navigationSearch().noteTravelWait();throw;
}
// WalkExit already owns a resolved walking connection. As its rooms load,
// reconsider the actual floor on the far side before probing another frontier.
bool DebugAi::Impl::tryTeleportAcrossExit(Context &c,uint16_t to) {
    const auto *map=c.town.map();const auto origin=c.town.read().origin;
    if(c.town.read().town || !map || !origin) return false;
    const TeleportRooms neighborhood(c);
    const auto &rooms=neighborhood.rooms;
    const auto destinationRoom=[&](size_t i){return i<rooms.size() && rooms[i].loaded && rooms[i].level==to;};
    if(!neighborhood.current || std::none_of(neighborhood.current->near.begin(),neighborhood.current->near.end(),destinationRoom) ||
        !teleportAvailable(c)) return false;
    bool allowed=false;
    for(size_t row=0;row<definitions->levels.rows().size();++row) if(definitions->levels.number(row,"Id")==to) {
        allowed=definitions->levels.number(row,"Teleport").value_or(0)!=0;break;
    }
    if(!allowed) return false;
    const auto threats=travelLandingThreats(c);
    std::optional<OnlinePoint> landing;float best=std::numeric_limits<float>::max();
    for(const auto next:neighborhood.current->near) {
        if(!destinationRoom(next)) continue;
        const auto &room=rooms[next];
        for(const float y:{.25f,.5f,.75f}) for(const float x:{.25f,.5f,.75f}) {
            const Vec sample{room.x+room.width*x-origin->x+.5f,room.y+room.height*y-origin->y+.5f};
            const auto open=map->grid.nativeSpawn(sample,8,playerMovement);if(!open) continue;
            const auto point=nativePoint(*open+Vec{float(origin->x),float(origin->y)});const Vec at{float(point.x),float(point.y)};
            const float span=distance(at,c.position);
            const auto trial=std::tuple{area.value_or(0),point.x,point.y,point.x,point.y};
            if((at.x<room.x || at.y<room.y || at.x>=room.x+room.width || at.y>=room.y+room.height) || span<2 || span>mobilityPolicy.distance ||
                frontierCoolingDown(point,3) || crowdedTravelLanding(threats,at) ||
                (mobilityTrials.contains(trial) && mobilityTrials.at(trial)>=2)) continue;
            if(span<best) {best=span;landing=point;}
        }
    }
    if(!landing) return false;
    travelDiagnostics["result"]="observed-neighbor-teleport";
    travelDiagnostics["landing"]={landing->x,landing->y};
    wait("travel-teleport","Crossing to currently reconstructed adjacent-level floor with normal Teleport");
    walk(c,{{"goal","point"},{"x",landing->x},{"y",landing->y},{"interact",false},{"purpose","known-target-teleport"}});
    return true;
}
bool DebugAi::Impl::travel(Context &c,uint16_t destination,bool allowWaypoint) {
    if(!area) return false;
    if(*area==destination) {
        cancelWalk(c);travelDestination.reset();travelRoute.clear();travelVisitedAreas.clear();travelPhase=TravelPhase::Route;legDestination.reset();return true;
    }
    const bool awaitingTransfer=legDestination && legDestination!=area &&
        (travelPhase==TravelPhase::WaypointWait || travelPhase==TravelPhase::CrossWait);
    if(!awaitingTransfer && waypointBeforeDeparture(c)) return false;
    if(allowWaypoint && !c.town.read().town) {
        std::optional<int> sourceAct,destinationAct;bool sourceWaypoint=false;
        for(size_t row=0;row<definitions->levels.rows().size();++row) {
            const auto level=definitions->levels.number(row,"Id");
            if(level==area) {
                sourceAct=definitions->levels.number(row,"Act");
                const auto number=definitions->levels.number(row,"Waypoint");
                sourceWaypoint=number && *number>=0 && *number<112;
            }
            if(level==destination) destinationAct=definitions->levels.number(row,"Act");
        }
        if(sourceAct && destinationAct && sourceAct!=destinationAct && !sourceWaypoint && route(c,*area,destination).empty()) {
            // A waypoint journey may begin on a level without a source.
            // Retain reachTown's normal portal/walking continuation before
            // changing travelDestination; an observed direct portal wins.
            reachTown(c);return false;
        }
    }
    if(travelDestination!=destination) {
        boundaryAim.reset();
        unavailableWaypointArea.reset();failedWalkEdges.clear();cancelWalk(c);travelDestination=destination;travelPhase=TravelPhase::Route;legDestination.reset();
        waypointDestination.reset();legUnit.reset();travelRoute.clear();travelVisitedAreas.clear();
    }
    travelVisitedAreas.insert(*area);
    if(legDestination && *area==*legDestination) {
        if(!travelRoute.empty()) failedWalkEdges.erase({travelRoute.front(),*area});
        cancelWalk(c);travelPhase=TravelPhase::Route;legDestination.reset();waypointDestination.reset();legUnit.reset();
    }
    if(!travelRoute.empty() && travelRoute.front()!=*area) {
        const auto reached=std::find(travelRoute.begin(),travelRoute.end(),int(*area));
        if(reached==travelRoute.end()) {
            note("travel-route-deviation","Actual area is outside the retained route; planning from the observed arrival");
            travelRoute.clear();
        } else travelRoute.erase(travelRoute.begin(),reached);
    }
    if(travelRoute.size()>1) {
        const auto &connections=c.town.navigationConnections();
        const auto edge=connections.find(*area);
        const bool assignedPortal=std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &target) {
            return target.destination==travelRoute[1] && ordinaryConnection(target,c.session.read());
        });
        if(!assignedPortal && (edge==connections.end() || std::find(edge->second.begin(),edge->second.end(),travelRoute[1])==edge->second.end())) {
            note("travel-route-invalidated","The next retained connection is absent from the current resolved graph");
            travelRoute.clear();
        }
    }
    if(travelRoute.empty()) {
        travelRoute=route(c,*area,destination);
        if(!travelRoute.empty()) note("travel-route-planned",Json(travelRoute).dump());
    }
    const auto now=Clock::now();
    const auto &targets=c.town.read().mapTargets;
    const auto localWaypoint=std::find_if(targets.begin(),targets.end(),[&](const auto &entry) {
        return entry.interaction==OnlineMapInteraction::Waypoint && c.town.assignedUnitLevel(entry.unit)==area;
    });
    const auto waypointPosition=[&]() -> std::optional<OnlinePoint> {
        if(localWaypoint!=targets.end()) return localWaypoint->position;
        if(area==unavailableWaypointArea) return {};
        return c.town.knownWaypointPosition(*area);
    };
    const auto levelAct=[&](int level) -> std::optional<int> {
        for(size_t row=0;row<definitions->levels.rows().size();++row)
            if(definitions->levels.number(row,"Id")==level) return definitions->levels.number(row,"Act");
        return {};
    };
    const auto sourceAct=levelAct(*area),destinationAct=levelAct(destination);
    const bool anotherAct=sourceAct && destinationAct && sourceAct!=destinationAct;
    const auto canObserveDestinationAct=[&](uint16_t level) {
        return anotherAct && destinationAct==levelAct(level);
    };
    const auto chooseWaypoint=[&]() -> std::optional<uint16_t> {
        const auto source=waypointPosition();if(!source) return {};
        const auto &current=travelRoute;
        float shortest=routeCost(c,current);std::optional<uint16_t> best,observationWaypoint;
        const bool ordinaryAvailable=std::isfinite(shortest);
        const bool recoveringRoute=current.size()>1 && failedWalkEdges.contains({*area,current[1]}) && failedWalkEdges.at({*area,current[1]})>=2;
        const float approach=navigationCost(c,*area,c.position,source);
        travelDiagnostics={{"from",*area},{"destination",destination},{"ordinaryRoute",current},
            {"ordinaryCost",std::isfinite(shortest)?Json(shortest):Json(nullptr)},
            {"waypointApproachCost",approach},{"waypoints",Json::array()},{"selectedWaypoint",nullptr}};
        for(const auto level:knownWaypointAreas) if(level!=*area) {
            // Do not replace a viable committed route with a waypoint back
            // into its own visited prefix on every new estimate. An absent
            // ordinary route still permits every legal recovery waypoint.
            if(ordinaryAvailable && !recoveringRoute && travelVisitedAreas.contains(level)) {
                travelDiagnostics["waypoints"].push_back({{"level",level},{"result","visited-on-current-journey"}});
                continue;
            }
            const auto rest=route(c,level,destination);
            // A newly joined client may not have materialized the other act's
            // connections yet. An owned waypoint is still a legal transfer:
            // observe that act, then replan from its actual scene. This is a
            // fallback, not an invented onward edge or a same-act retry loop.
            if(!ordinaryAvailable && rest.empty() && !observationWaypoint && canObserveDestinationAct(level))
                observationWaypoint=level;
            const auto landing=c.town.knownWaypointPosition(level);
            const auto start=landing?std::optional<Vec>{{float(landing->x),float(landing->y)}}:std::nullopt;
            const float cost=approach+16+routeCost(c,rest,start);
            travelDiagnostics["waypoints"].push_back({{"level",level},{"route",rest},
                {"cost",std::isfinite(cost)?Json(cost):Json(nullptr)}});
            if(cost<shortest) {shortest=cost;best=level;}
        }
        if(!best && observationWaypoint) {
            best=observationWaypoint;
            travelDiagnostics["result"]="observe-destination-act-via-unlocked-waypoint";
        }
        if(best) travelDiagnostics["selectedWaypoint"]=*best;
        return best;
    };
    if(travelPhase==TravelPhase::WaypointWait || travelPhase==TravelPhase::CrossWait) {
        if(travelPhase==TravelPhase::CrossWait && !legUnit && !c.moving &&
            now+std::chrono::seconds(12)>=actionDeadline) {
            // A walking pointer can stop at a newly occupied seam. It has no
            // irreversible service result to replay: choose a fresh ordinary
            // approach from current client collision instead of waiting forever.
            if(legDestination) {
                auto &failures=failedWalkEdges[{*area,*legDestination}];failures=std::min(failures+1,16u);
            }
            cancelWalk(c);travelRoute.clear();travelPhase=TravelPhase::Route;legDestination.reset();exitRetryAt=now+std::chrono::seconds(1);
            note("walking-crossing-stopped","Replanning the ordinary boundary approach from observed occupancy");return false;
        }
        wait("travel-wait","Waiting for the destination's native scene assignment");
        if(now>=actionDeadline) throw std::runtime_error("Travel was submitted but the destination was not observed");
        return false;
    }
    if(travelPhase==TravelPhase::WaypointOpen) {
        const bool open=c.session.read().world.waypointSource && c.session.read().world.waypointHistory &&
            std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &entry) {
                return entry.interaction==OnlineMapInteraction::Waypoint && c.town.assignedUnitLevel(entry.unit)==area &&
                    entry.unit.id==*c.session.read().world.waypointSource && c.session.read().world.units.contains(entry.unit);
            });
        if(!open) {
            if(localWaypoint==targets.end()) {
                if(const auto hint=c.town.knownWaypointPosition(*area);hint && approachPoint(c,*hint,"known waypoint")) return false;
                if(travelRoute.size()>1 && !(explorationReturn && explorationReturn->returning)) {
                    unavailableWaypointArea=area;waypointDestination.reset();travelPhase=TravelPhase::Route;cancelWalk(c);
                    note("waypoint-source-unavailable","Using the ordinary route until the source is assigned");return false;
                }
            }
            wait("waypoint","Opening the actual assigned waypoint");
            if(teleportAvailable(c)) {
                if(localWaypoint==targets.end()) {explore(c,"assignment of the waypoint");return false;}
                if(!walk(c,{{"goal","unit"},{"unitType",localWaypoint->unit.type},{"unitId",localWaypoint->unit.id},
                    {"interact",true},{"confirmMenu",true},{"purpose","travel-waypoint"}})) return false;
            } else if(!walk(c,{{"goal","waypoint"},{"purpose","travel-waypoint"}})) return false;
        }
        if(waypointDestination && !knownWaypointAreas.contains(*waypointDestination)) {
            if(explorationReturn && explorationReturn->returning)
                throw std::runtime_error("The actual waypoint menu did not confirm the exploration return destination");
            note("waypoint-memory-corrected","The actual menu disproved the planned unlock; reselecting from current knowledge");
            waypointDestination.reset();
        }
        if(!waypointDestination) waypointDestination=chooseWaypoint();
        if(!waypointDestination) {
            c.session.use_waypoint(0);cancelWalk(c);travelPhase=TravelPhase::Route;return false;
        }
        const auto entry=std::find_if(c.town.read().waypoints.begin(),c.town.read().waypoints.end(),
            [&](const auto &v){return v.level==waypointDestination && v.unlocked;});
        if(entry==c.town.read().waypoints.end()) throw std::runtime_error("Waypoint destination is not unlocked");
        if(c.session.use_waypoint(entry->level,entry->number)) {
            legDestination=entry->level;travelPhase=TravelPhase::WaypointWait;setActionDeadline(std::chrono::seconds(20));cancelWalk(c);
            note("waypoint-request-submitted","from="+std::to_string(*area)+" to="+std::to_string(entry->level));
        }
        return false;
    }
    if(travelPhase==TravelPhase::WalkExit || travelPhase==TravelPhase::WarpExit) {
        const bool warp=travelPhase==TravelPhase::WarpExit;
        if(!warp && approachTownGate(c,*legDestination)) return false;
        if(warp && legUnit && std::none_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &t){return t.unit==legUnit;})) {
            if(c.walk.active() && walkIntent.is_object() && walkIntent.value("goal",std::string{})=="point") {
                wait("known-target-approach","Continuing the observed exit's position approach while its assignment is outside the client scene");
                return false;
            }
            cancelWalk(c);travelPhase=TravelPhase::Route;legUnit.reset();note("exit-assignment-withdrawn");return false;
        }
        if(!warp && tryTeleportAcrossExit(c,*legDestination)) return false;
        if(!warp && walkIntent.is_object() && walkIntent.value("goal",std::string{})=="exit" && walkState(c)=="failed") {
            auto &failures=failedWalkEdges[{*area,*legDestination}];
            failures=std::min(failures+1,16u);
            note("walking-connection-not-yet-reachable",std::to_string(*area)+"->"+std::to_string(*legDestination)+
                " attempt "+std::to_string(failures)+": "+c.walk.snapshot().value("reason",std::string{}));
            exitRetryAt=now+std::chrono::seconds(5);
            cancelWalk(c);travelPhase=TravelPhase::Route;travelRoute.clear();
            if(failures>=2) {
                travelRoute=route(c,*area,destination);
                if(travelRoute.size()>1 && travelRoute[1]!=*legDestination) {
                    legDestination.reset();note("travel-alternate-route",Json(travelRoute).dump());return false;
                }
            }
            explore(c,"unobserved boundary toward level "+std::to_string(*legDestination));return false;
        }
        wait("travel-exit","Approaching the connection to "+std::to_string(*legDestination));
        Json intent=warp?Json{{"goal","unit"},{"unitType",legUnit->type},{"unitId",legUnit->id},{"interact",true}}:
            Json{{"goal","exit"},{"level",*legDestination},{"interact",false}};
        if(!walk(c,intent)) return false;
        if(warp && legUnit) rememberTravel(c,*legUnit);
        const auto observed=c.walk.snapshot();
        if(!warp) {
            const auto &a=observed.at("boundaryStart"),&b=observed.at("boundaryEnd"),&t=observed.at("target");
            if(a.is_null() || b.is_null() || t.is_null()) throw std::runtime_error("Walking exit has no observed logical boundary");
            int x=t.at("x"),y=t.at("y");
            if(a.at("x")==b.at("x")) {const int plane=a.at("x");x=plane+(x<plane?3:-3);}
            else {const int plane=a.at("y");y=plane+(y<plane?3:-3);}
            if(x<0 || y<0 || x>65535 || y>65535) throw std::runtime_error("Exit crossing outside native coordinates");
            const auto origin=c.town.read().origin;
            if(origin) {
                const Vec offset{float(origin->x),float(origin->y)};
                const auto grid=c.town.navigationGrid(c.session.read());
                const Vec crossing=Vec{float(x),float(y)}-offset+Vec{.5f,.5f};
                const bool known=grid.movementMask(int(crossing.x),int(crossing.y))!=0xffff;
                if(known && !grid.nativeMovementSegment(c.position-offset+Vec{.5f,.5f},crossing,playerMovement)) {
                    cancelWalk(c);
                    note("walking-crossing-occupied","Selecting another opening after new client unit/terrain updates");return false;
                }
            }
            // The ordinary move is still subject to client-map and server gates.
            const auto owner=DebugActionOwner::Navigation;
            const auto action="walking-crossing:"+std::to_string(*area)+"->"+std::to_string(*legDestination)+
                "@"+std::to_string(x)+","+std::to_string(y);
            const auto result=submitInput(c,owner,action,DebugMotion,
                [&]{return c.control.move({uint16_t(x),uint16_t(y)},true,{},c.position);},[&]{return c.control.reason();});
            if(result==DebugSubmitResult::Waiting) return false;
            if(result==DebugSubmitResult::Expired)
                throw std::runtime_error("Exit crossing input was not accepted: "+submissionWaits.at(owner).reason);
            // The approach already completed. Keep the newly accepted crossing pointer alive.
            walkIntent=nullptr;
        } else cancelWalk(c);
        ++interactions;travelPhase=TravelPhase::CrossWait;setActionDeadline(std::chrono::seconds(15));return false;
    }
    // Costs for unknown entry points are estimates. Repricing after every
    // boundary can prefer the area just left and create a two-area loop.
    // Retain the chosen sequence until progress, a real deviation, a resolved
    // missing connection, or an explicit approach failure changes the plan.
    const auto &path=travelRoute;
    const float ordinaryCost=routeCost(c,path);
    travelDiagnostics={{"from",*area},{"destination",destination},{"ordinaryRoute",path},
        {"ordinaryCost",std::isfinite(ordinaryCost)?Json(ordinaryCost):Json(nullptr)},{"selectedWaypoint",nullptr}};
    if(allowWaypoint && !(deferredWaypoint && now<deferredWaypoint->retryAt)) {
        const bool source=waypointPosition().has_value();
        // 63 reports unlocked destinations only after an actual menu is opened.
        // Being in town does not disclose its waypoint's location. Do not
        // replace the requested exit route with an unrelated room search.
        const bool nearby=std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &v) {
            return v.interaction==OnlineMapInteraction::Waypoint && c.town.assignedUnitLevel(v.unit)==area &&
                distance(c.position,{float(v.position.x),float(v.position.y)})<=8;
        });
        bool mayShorten=false;
        const bool noMemory=!waypointMemoryRevision && knownWaypointAreas.empty();
        if(source && noMemory) {
            const float approach=navigationCost(c,*area,c.position,waypointPosition());
            for(size_t row=0;row<definitions->levels.rows().size() && !mayShorten;++row) {
                const auto number=definitions->levels.number(row,"Waypoint"),level=definitions->levels.number(row,"Id");
                if(!number || *number<0 || *number>=112 || !level || *level<=0 || *level==*area) continue;
                const auto rest=route(c,*level,destination);
                mayShorten=approach+16+routeCost(c,rest)<ordinaryCost;
            }
        }
        if((nearby || source) && mayShorten && noMemory) {travelPhase=TravelPhase::WaypointOpen;return false;}
        if(source) waypointDestination=chooseWaypoint();
        if(waypointDestination) {travelPhase=TravelPhase::WaypointOpen;return false;}
    }
    if(path.empty()) {
        bool localWaypoint=false;
        for(size_t row=0;row<definitions->levels.rows().size();++row) if(definitions->levels.number(row,"Id")==area) {
            const auto number=definitions->levels.number(row,"Waypoint");
            localWaypoint=number && *number>=0 && *number<112;break;
        }
        bool possible=!waypointMemoryRevision && knownWaypointAreas.empty();
        if(!possible) for(const auto level:knownWaypointAreas) if(level!=area &&
            (!route(c,level,destination).empty() || canObserveDestinationAct(level))) {possible=true;break;}
        if(allowWaypoint && localWaypoint && possible && !waypointPosition()) {
            // An unresolved ordinary graph must not force the caller to give
            // us the waypoint's coordinates. Only this fallback may search for
            // an unknown source; its actual assignment and 63 remain required.
            travelDiagnostics["result"]="observe-waypoint-source-without-ordinary-route";
            explore(c,"an actual current-level waypoint / travel connection");return false;
        }
        throw std::runtime_error("No observed portal, resolved level route or available unlocked waypoint route; AI does not invent a transfer or unlock");
    }
    if(legDestination!=uint16_t(path.at(1))) boundaryAim.reset();
    legDestination=uint16_t(path.at(1));
    if(approachTownGate(c,*legDestination) || openDoor(c)) return false;
    for(const auto &entry:c.town.read().mapTargets) if(entry.destination==legDestination && ordinaryConnection(entry,c.session.read())) {
        legUnit=entry.unit;travelPhase=TravelPhase::WarpExit;return false;
    }
    if(approachKnown(c,Selector{5,*legDestination,"known connection to "+std::to_string(*legDestination)})) return false;
    if(tombSymbol && *tombSymbol<7 && *legDestination==definitions->area("Act 2 - Tomb Tal "+std::to_string(*tombSymbol+1)))
        if(const auto hint=c.town.fixedCanyonTombPosition(*legDestination);hint &&
            approachPoint(c,*hint,"journal symbol's fixed canyon entrance",6)) return false;
    const auto edges=c.town.walkingBoundaries();
    const bool walkingEdge=std::any_of(edges.begin(),edges.end(),[&](const auto &edge){return edge.destination==legDestination;});
    if(walkingEdge && tryTeleportAcrossExit(c,*legDestination)) return false;
    // A junction or viewpoint detour can require several casts through known
    // rooms. Let exploration consume the completed leg before retrying the seam.
    if(viewpointRoute || (walkIntent.is_object() && walkIntent.contains("backtrackRoom"))) {
        explore(c,"retained exploration toward level "+std::to_string(*legDestination),true);return false;
    }
    if(exploring && c.walk.active()) {explore(c,"connection to level "+std::to_string(*legDestination));return false;}
    if(walkingEdge && teleportAvailable(c)) {
        // A projection on a level boundary is only a long-distance heading.
        // The actual opening can be anywhere along the boundary, off that ray.
        std::optional<OnlinePoint> hint;float nearest=std::numeric_limits<float>::max();bool aimOnBoundary=false;
        for(const auto &edge:edges) {
            if(edge.destination!=legDestination || edge.end-edge.start<3) continue;
            const bool vertical=edge.side%2!=0;
            const int inset=(edge.side==1 || edge.side==2)?3:-3;
            const int along=std::clamp(int(vertical?c.position.y:c.position.x),edge.start+1,edge.end-2);
            const auto at=vertical?OnlinePoint{uint16_t(edge.plane+inset),uint16_t(along)}:
                OnlinePoint{uint16_t(along),uint16_t(edge.plane+inset)};
            const float span=distance(c.position,{float(at.x),float(at.y)});
            if(span<nearest) {nearest=span;hint=at;}
            if(boundaryAim) {
                const int plane=vertical?boundaryAim->x:boundaryAim->y,axis=vertical?boundaryAim->y:boundaryAim->x;
                if(plane==edge.plane+inset && axis>=edge.start+1 && axis<=edge.end-2) aimOnBoundary=true;
            }
        }
        // Reprojecting from a sideways relay would change the goal and
        // discard the room detour that led to this landing.
        if(!aimOnBoundary) boundaryAim=hint;
        if(boundaryAim && nearest>mobilityPolicy.distance && !frontierCoolingDown(*boundaryAim,8) &&
            approachPoint(c,*boundaryAim,"observed level boundary",3)) return false;
    }
    boundaryAim.reset();
    if(now>=exitRetryAt && walkingEdge) {
        travelPhase=TravelPhase::WalkExit;return false;
    }
    explore(c,"connection to level "+std::to_string(*legDestination));return false;
}
}
