#include "auto_walk.hpp"
#include "client/online_actions.hpp"
#include "client/remote_town.hpp"
#include "world/interaction_geometry.hpp"
#include "world/navigation_snapshot.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace d2x {
namespace {
int distance(OnlinePoint a,OnlinePoint b) {return std::max(std::abs(int(a.x)-b.x),std::abs(int(a.y)-b.y));}
OnlinePoint point(Vec p) {
    const int x=int(std::floor(p.x)),y=int(std::floor(p.y));
    if(x<0 || y<0 || x>UINT16_MAX || y>UINT16_MAX) throw std::invalid_argument("Navigation point outside native coordinates");
    return {uint16_t(x),uint16_t(y)};
}
}
bool DebugAutoWalk::active() const {return state_==DebugWalkState::Searching || state_==DebugWalkState::Moving || state_==DebugWalkState::Interacting;}
DebugWalkObservation DebugAutoWalk::observe() const {
    DebugWalkObservation result;
    result.state=state_;result.failure=failure_;result.reason=reason_;
    result.gameGeneration=game_;result.areaGeneration=area_;result.interactionRevision=interactionRevision_;
    result.target=point_;result.crossingTarget=crossing_;result.recoveryAnchor=recoveryAnchor_;result.unit=unit_;
    if(interactionFrom_) result.submittedInteraction=DebugWalkSubmission{interactionRevision_,interactionDiscontinuity_,*interactionFrom_};
    result.moveRequests=moves_;result.interactionRequests=interactions_;result.movementRecoveries=movementRecoveries_;
    result.searchContinuations=cadence_.continuations();return result;
}
nlohmann::json DebugAutoWalk::snapshot() const {return status();}
void DebugAutoWalk::finish(DebugWalkState state,std::string reason,IOnlineControl &control,DebugWalkFailure failure) {
    if(state==DebugWalkState::Failed) ++failures_;
    failure_=state==DebugWalkState::Failed?failure:DebugWalkFailure::None;
    state_=state;reason_=std::move(reason);control.cancelMovement();segment_.reset();pointerLane_.reset();recoveryAnchor_.reset();routeDoor_.reset();blockedStep_.reset();
}
void DebugAutoWalk::manualInput(IOnlineControl &control) {if(active()) finish(DebugWalkState::Cancelled,"Manual input took control",control);}
nlohmann::json DebugAutoWalk::status() const {
    using Json=nlohmann::json;
    const auto position=[](OnlinePoint p) {return Json{{"x",p.x},{"y",p.y}};};
    Json selections=Json::array();
    for(const auto &entry:pointerSelections_) selections.push_back({{"request",entry.request},{"from",position(entry.from)},
        {"target",position(entry.target)},{"raw",position(entry.raw)},{"lane",position(entry.lane)},
        {"feet",{{"x",entry.feet.x},{"y",entry.feet.y}}},
        {"submitted",position(entry.submitted)},{"resume",entry.resume?position(*entry.resume):Json(nullptr)},{"branch",entry.branch}});
    Json obstruction=nullptr;
    if(blockedStep_) {
        Json units=Json::array();for(const auto key:blockedStep_->units) units.push_back({{"type",key.type},{"id",key.id}});
        obstruction={{"from",{blockedStep_->from.x,blockedStep_->from.y}},
            {"to",{blockedStep_->to.x,blockedStep_->to.y}},{"units",std::move(units)}};
    }
    return {{"blockedStep",std::move(obstruction)},{"state",debugWalkName(state_)},{"active",active()},{"reason",reason_},{"gameGeneration",game_},{"areaGeneration",area_},
        {"area",level_?Json(*level_):Json(nullptr)},
        {"target",point_?Json{{"x",point_->x},{"y",point_->y}}:Json(nullptr)},
        {"unit",unit_?Json{{"type",unit_->type},{"id",unit_->id}}:Json(nullptr)},
        {"segment",segment_?Json{{"x",segment_->x},{"y",segment_->y}}:Json(nullptr)},
        {"pointerLane",pointerLane_?Json{{"bend",position(pointerLane_->bend)},{"resume",position(pointerLane_->resume)},
            {"target",position(pointerLane_->target)},{"resuming",pointerLane_->resuming},{"town",pointerLane_->town}}:Json(nullptr)},
        {"pointerSelections",std::move(selections)},
        {"interactionApproachRetries",approachRetries_},
        {"movementRecoveries",movementRecoveries_},
        {"recoverableRelay",recoverableRelay_},{"adjustLane",adjustLane_},
        {"relayMovesWithoutProgress",moves_-relayProgressMoves_},
        {"recoveryAnchor",recoveryAnchor_?Json{{"x",recoveryAnchor_->x},{"y",recoveryAnchor_->y}}:Json(nullptr)},
        {"clientPosition",client_?Json{{"x",client_->x},{"y",client_->y}}:Json(nullptr)},
        {"serverPosition",server_?Json{{"x",server_->x},{"y",server_->y}}:Json(nullptr)},
        {"targetSource",source_},{"exactPoint",exactPoint_},{"exitLevel",exitLevel_?Json(*exitLevel_):Json(nullptr)},{"navigationSearch",search_.snapshot()},
        {"decisionIntervalMs",cadence_.intervalMs()},{"decisions",decisions_},{"clientMoving",clientMoving_},
        {"scheduler",{{"mode","adaptive"},{"wakeReason",cadence_.reason()},{"eventWakeups",cadence_.eventWakeups()},
            {"timerWakeups",cadence_.timerWakeups()},{"searchContinuations",cadence_.continuations()}}},
        {"boundaryStart",edgeStart_?Json{{"x",edgeStart_->x},{"y",edgeStart_->y}}:Json(nullptr)},
        {"boundaryEnd",edgeEnd_?Json{{"x",edgeEnd_->x},{"y",edgeEnd_->y}}:Json(nullptr)},
        {"crossingTarget",crossing_?Json{{"x",crossing_->x},{"y",crossing_->y}}:Json(nullptr)},
        {"moveRequests",moves_},{"interactionRequests",interactions_},{"searchRooms",searches_},
        {"interactionRevision",interactionRevision_},
        {"submittedInteraction",interactionFrom_?Json{{"worldRevision",interactionRevision_},{"discontinuity",interactionDiscontinuity_},
            {"position",{{"x",interactionFrom_->x},{"y",interactionFrom_->y}}}}:Json(nullptr)},
        {"executionBlocked",executionClock_.blocked()},{"suspendedExecutionMs",executionClock_.suspendedMs()},
        {"executionRemainingMs",std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(deadline_-Clock::now()).count())},
        {"excludedOpenings",goal_==Goal::Exit?Json(visited_):Json::array()}};
}
std::optional<std::string> DebugAutoWalk::command(const nlohmann::json &request,const OnlineObservation &observation,
        const RemoteTown &town,IOnlineControl &control,IOnlineCombat &combat) {
    using Json=nlohmann::json;
    const auto name=request.at("command").get<std::string>();
    if(name!="debug-walk" && name!="debug-walk-status" && name!="debug-walk-cancel") return {};
    if(name=="debug-walk-cancel") {
        if(active()) {
            finish(DebugWalkState::Cancelled,"Cancelled by debug command",control);
            OnlineCombatCommand stop;stop.action=OnlineCombatCommand::Action::Stop;combat.submit(stop);
        }
    } else if(name=="debug-walk") {
        if(active()) throw std::invalid_argument("Cancel the current debug walk before starting another");
        const auto &view=observation.read();
        // The frontend already bound this scene using the local DynamicPath
        // position. Updating it here without that position can select the
        // other side of a walking boundary from a sparse server sample and
        // make the parent/child cancel and rebind each other indefinitely.
        const auto &scene=town.read();
        if(view.stage!=OnlineStage::ProtocolReady || !scene.movementAvailable || !scene.origin ||
            !view.world.playerPosition || onlinePlayerDead(view.world)) throw std::invalid_argument("Debug walk requires a live player and native map");
        const auto integer=[&](const char *key,int64_t low,int64_t high) {
            const auto &value=request.at(key);
            if(!value.is_number_integer()) throw std::invalid_argument("Debug walk coordinate/id must be an integer");
            const auto n=value.get<int64_t>();
            if(n<low || n>high) throw std::invalid_argument("Debug walk value outside supported range");
            return n;
        };
        const auto goal=request.value("goal",std::string{"waypoint"});
        Goal kind;
        if(goal=="waypoint") kind=Goal::Waypoint;
        else if(goal=="exit") kind=Goal::Exit;
        else if(goal=="point") kind=Goal::Point;
        else if(goal=="unit") kind=Goal::Unit;
        else throw std::invalid_argument("goal must be waypoint, exit, point or unit");
        const auto location=kind==Goal::Point?std::optional{OnlinePoint{uint16_t(integer("x",0,UINT16_MAX)),uint16_t(integer("y",0,UINT16_MAX))}}:std::nullopt;
        const auto target=kind==Goal::Unit?std::optional{OnlineUnitKey{uint8_t(request.contains("unitType")?integer("unitType",0,5):2),uint32_t(integer("unitId",0,UINT32_MAX))}}:std::nullopt;
        const auto destination=request.contains("level")?std::optional{uint16_t(integer("level",1,136))}:std::nullopt;
        const int timeout=int(request.contains("timeoutSeconds")?integer("timeoutSeconds",5,300):120);
        DebugWalkIntent intent;intent.goal=kind;intent.point=location;intent.unit=target;intent.level=destination;intent.timeoutSeconds=timeout;
        if(request.contains("run")) intent.run=request.at("run").get<bool>();
        if(request.contains("interact")) intent.interact=request.at("interact").get<bool>();
        if(kind==Goal::Exit && request.contains("localCrossing")) intent.localCrossing=request.at("localCrossing").get<bool>();
        if(kind==Goal::Point && !intent.interact.value_or(false) && request.contains("exploration")) intent.exploration=request.at("exploration").get<bool>();
        if(kind==Goal::Point && request.contains("exactPoint")) intent.exactPoint=request.at("exactPoint").get<bool>();
        if(kind==Goal::Point && request.contains("recoverableRelay")) intent.recoverableRelay=request.at("recoverableRelay").get<bool>();
        if(request.contains("adjustLane")) intent.adjustLane=request.at("adjustLane").get<bool>();
        if(request.contains("resyncApproach")) intent.resyncApproach=request.at("resyncApproach").get<bool>();
        if(request.contains("changeStance")) intent.changeStance=request.at("changeStance").get<bool>();
        if(request.contains("forceApproach")) intent.forceApproach=request.at("forceApproach").get<bool>();
        if(kind==Goal::Unit && target->type==1 && request.contains("waitQuestAlert")) intent.waitQuestAlert=request.at("waitQuestAlert").get<bool>();
        if(kind!=Goal::Waypoint && request.contains("confirmMenu")) intent.confirmMenu=request.at("confirmMenu").get<bool>();
        if(kind==Goal::Unit && target->type==2 && request.contains("purpose")) intent.purpose=request.at("purpose").get<std::string>();
        start(intent,observation,town,control);
    }
    return Json{{"ok",true},{"navigation",status()}}.dump();
}
void DebugAutoWalk::start(const DebugWalkIntent &request,const OnlineObservation &observation,const RemoteTown &town,IOnlineControl &control) {
    if(active()) throw std::invalid_argument("Cancel the current debug walk before starting another");
    const auto &view=observation.read();
    // The frontend already bound this scene using the local DynamicPath
    // position. Updating it here without that position can select the
    // other side of a walking boundary from a sparse server sample and
    // make the parent/child cancel and rebind each other indefinitely.
    const auto &scene=town.read();
    if(view.stage!=OnlineStage::ProtocolReady || !scene.movementAvailable || !scene.origin ||
        !view.world.playerPosition || onlinePlayerDead(view.world)) throw std::invalid_argument("Debug walk requires a live player and native map");
    const auto kind=request.goal;
    const auto location=request.point;const auto target=request.unit;const auto destination=request.level;
    const int timeout=request.timeoutSeconds.value_or(120);
    if(kind==Goal::None || (kind==Goal::Point && !location) || (kind==Goal::Unit && !target) ||
        (target && target->type>5) || (destination && (*destination<1 || *destination>136)) || timeout<5 || timeout>300)
        throw std::invalid_argument("Debug walk value outside supported range");
    const bool run=request.run.value_or(true),interact=request.interact.value_or(kind==Goal::Waypoint || kind==Goal::Unit);
    const bool retainHostProgress=game_==view.gameGeneration && area_==view.world.areaGeneration && level_==scene.area &&
        hostProgressPosition_==view.world.playerPosition && client_ && view.world.playerPosition &&
        distance(*client_,*view.world.playerPosition)>8;
    if(game_!=view.gameGeneration || area_!=view.world.areaGeneration || level_!=scene.area) {
        passage_.reset();passageTarget_.reset();passageObservationSince_.reset();
    }
    control.cancelMovement();goal_=kind;point_=location;unit_=target;destination_=destination;
    game_=view.gameGeneration;area_=view.world.areaGeneration;level_=scene.area;run_=run;interact_=interact;landmark_=false;
    localCrossing_=kind==Goal::Exit && request.localCrossing.value_or(false);
    exploration_=kind==Goal::Point && !interact && request.exploration.value_or(false);
    exactPoint_=kind==Goal::Point && request.exactPoint.value_or(false);
    recoverableRelay_=kind==Goal::Point && request.recoverableRelay.value_or(false);
    adjustLane_=request.adjustLane.value_or(true);
    relayClient_.reset();relayHost_.reset();relayTerrain_.reset();relayProgressAt_={};relayProgressMoves_=0;
    state_=DebugWalkState::Searching;failure_=DebugWalkFailure::None;reason_.clear();segment_.reset();previous_=view.world.playerPosition;visited_.clear();deferredOpenings_.clear();
    source_=kind==Goal::Point?"explicit-point":kind==Goal::Unit?"assigned-unit":"";
    client_.reset();server_=view.world.playerPosition;edgeStart_.reset();edgeEnd_.reset();crossing_.reset();exitLevel_.reset();
    townTurnSince_={};legDiscontinuity_=0;
    lastFrom_.reset();lastRequest_.reset();lastTarget_.reset();closeApproach_=false;approachRetries_=0;clickedAt_={};decisions_=0;clientMoving_=false;
    pointerLane_.reset();pointerSelections_.clear();lastPointerFeet_.reset();
    resyncApproach_=request.resyncApproach.value_or(false);
    recoveryAnchor_.reset();movementRecoveries_=0;
    routeDoor_.reset();blockedStep_.reset();routeDoorSince_={};routeDoorRequest_={};
    if(!retainHostProgress) {
        hostProgressPosition_=view.world.playerPosition;hostProgressAt_=Clock::now();hostProgressRevision_=view.world.revision;
    }
    changeStance_=request.changeStance.value_or(false);
    forceApproach_=request.forceApproach.value_or(false);
    waitQuestAlert_=kind==Goal::Unit && target->type==1 && request.waitQuestAlert.value_or(false);
    waypointOpening_=kind==Goal::Waypoint || (kind==Goal::Unit && target->type==2 &&
        (request.purpose.value_or(std::string{})=="activate-waypoint" || request.purpose.value_or(std::string{})=="travel-waypoint"));
    waypointMenu_=kind==Goal::Waypoint || request.confirmMenu.value_or(false);
    alertWaitSince_={};
    moves_=interactions_=searches_=0;interactionFrom_.reset();interactionRevision_=view.world.revision;interactionDiscontinuity_=0;
    progressAt_=Clock::now();deadline_=progressAt_+std::chrono::seconds(timeout);
    closestPoint_=std::numeric_limits<float>::max();closestAt_=progressAt_;
    executionClock_.reset(progressAt_);
    cadence_.reset();
}
void DebugAutoWalk::tick(const OnlineObservation &observation,const RemoteTown &town,IOnlineControl &control,bool blocked,std::optional<Vec> pathPosition,bool pathMoving) {
    if(!active()) return;
    try {
    const auto now=Clock::now();const auto &view=observation.read();const auto &scene=town.read();
    const bool unavailable=blocked || !scene.movementAvailable || !scene.origin || !view.world.playerPosition || !town.map() || !pathPosition;
    const auto suspended=executionClock_.sample(now,unavailable);
    deadline_+=suspended;progressAt_+=suspended;hostProgressAt_+=suspended;closestAt_+=suspended;
    if(clickedAt_!=Clock::time_point{}) clickedAt_+=suspended;
    if(alertWaitSince_!=Clock::time_point{}) alertWaitSince_+=suspended;
    if(townTurnSince_!=Clock::time_point{}) townTurnSince_+=suspended;
    if(relayProgressAt_!=Clock::time_point{}) relayProgressAt_+=suspended;
    if(view.gameGeneration!=game_ || view.world.areaGeneration!=area_ || view.stage!=OnlineStage::ProtocolReady ||
        (scene.area && scene.area!=level_)) {
        finish(DebugWalkState::Cancelled,"Game/area changed; start a new navigation intent",control);return;
    }
    if(onlinePlayerDead(view.world)) {finish(DebugWalkState::Failed,"Player died",control);return;}
    if(unit_ && unit_->type==1 && view.world.npcConversation && view.world.npcConversation->source==unit_->id) {
        finish(DebugWalkState::Completed,"Actual NPC conversation opened",control);return;
    }
    const auto &submitted=control.interactionSubmission();
    if(unit_ && unit_->type==2 && source_=="native-unit-approach" && submitted && submitted->target==unit_ &&
        onlineWorldMatches(submitted->context,view) && submitted->revision>interactionRevision_) {
        // Observe the actual 13 submission even when its resulting menu now
        // blocks movement. The parent owns the object's distinct postcondition.
        interactionFrom_=submitted->position;interactionRevision_=submitted->revision;
        interactionDiscontinuity_=submitted->discontinuity;
        if(!waypointOpening_) {finish(DebugWalkState::Completed,"Normal unit approach submitted the object interaction",control);return;}
        clickedAt_=now;
    }
    if(unit_ && waypointOpening_ && view.world.waypointSource==unit_->id) {
        finish(DebugWalkState::Completed,"Server waypoint menu opened",control);return;
    }
    const bool activated=unit_ && waypointOpening_ && clickedAt_!=Clock::time_point{} && view.world.waypointActivation &&
        view.world.waypointActivation->source==unit_->id && view.world.waypointActivation->generation==view.world.interactionGeneration;
    if(activated && !waypointMenu_) {finish(DebugWalkState::Completed,"Server confirmed waypoint activation",control);return;}
    if(unavailable) return;
    const bool nearEnd=segment_ && pathPosition && (Vec{float(segment_->x),float(segment_->y)}-*pathPosition).length()<=6;
    std::array<uint64_t,24> signals{view.world.areaGeneration,town.navigationRevision(),uint64_t(pathMoving),uint64_t(nearEnd),
        view.world.interactionGeneration,view.world.waypointSource.value_or(0),view.world.rooms.size()};
    if(unit_) if(const auto found=view.world.units.find(*unit_);found!=view.world.units.end()) {
        signals[10]=view.world.questAlerts.contains(*unit_);
        signals[11]=view.world.waypointActivation?view.world.waypointActivation->generation:0;
        signals[7]=found->second.assignmentRevision;signals[8]=found->second.mode.value_or(0);
        if(found->second.position) signals[9]=uint64_t(found->second.position->x)|(uint64_t(found->second.position->y)<<16);
    }
    const auto mode=nearEnd || !pathMoving?DebugDecisionCadence::Mode::Reaction:DebugDecisionCadence::Mode::Navigation;
    if(!cadence_.due(now,mode,signals,!pathMoving,std::min(deadline_,progressAt_+std::chrono::seconds(15)),
        timing_.walkDecisionMs,timing_.walkReactionMs)) return;
    if(now>=deadline_) {finish(DebugWalkState::Failed,"Debug navigation deadline expired",control);return;}
    ++decisions_;clientMoving_=pathMoving;blockedStep_.reset();
    if(unit_ && server_ && view.world.playerPosition && *server_!=*view.world.playerPosition) {
        const auto target=view.world.units.find(*unit_);
        if(target!=view.world.units.end() && target->second.position &&
            distance(*view.world.playerPosition,*target->second.position)<distance(*server_,*target->second.position))
            progressAt_=now;
    }
    server_=view.world.playerPosition;
    if(server_ && (!hostProgressPosition_ || distance(*hostProgressPosition_,*server_)>=2)) {
        hostProgressPosition_=server_;hostProgressAt_=now;hostProgressRevision_=view.world.revision;
    }
    if(!pathPosition) {reason_="Waiting for the normal client path position";return;}
    // Use the same local DynamicPath position as ordinary pointer input. The
    // server's sparse 96 samples are observations, not the client's input clock.
    // The local path exposes drawing feet with the half-cell removed, like
    // nativeUnitDistance. Quantize its native subtile, not the drawing floor:
    // otherwise 5089.999 remains one cell behind an authoritative 5090 and
    // repeatedly sends an already-reached 5090 leg instead of progressing.
    const auto feet=DebugExplorationMemory::playerCell(*pathPosition);
    client_=point({float(feet.first),float(feet.second)});
    const auto origin=*scene.origin;const auto &map=*town.map();
    const auto pathGrid=town.navigationGrid(view,unit_);
    // Keep the normal DynamicPath as the input origin even when stopped.
    // A sparse 0x96 sample can stay one cell behind a finished local leg;
    // planning from it resubmits that already reached leg forever instead
    // of selecting the next pointer. Normal position verification owns drift.
    const auto player=*client_;
    const Vec offset{float(origin.x),float(origin.y)},from=*pathPosition-offset+Vec{.5f,.5f};
    const auto &staticRouteGrid=search_.planningGrid(town,view);
    auto *knowledge=search_.observeTerrain(town,view,*pathPosition);
    if(source_=="observed-passage" && knowledge && passage_ && passageTarget_ && !unit_ && !landmark_) {
        const auto *p=knowledge->passage(*passage_);
        if(!p || std::find(p->approaches.begin(),p->approaches.end(),
            DebugExplorationMemory::Cell{passageTarget_->x,passageTarget_->y})==p->approaches.end()) {
            point_.reset();segment_.reset();control.cancelMovement();
            passage_.reset();passageTarget_.reset();passageObservationSince_.reset();
        }
    }
    const auto observedPassage=[&](std::optional<Vec> bearing={}) -> std::optional<OnlinePoint> {
        if(!knowledge || !scene.area) return {};
        if(!bearing && goal_==Goal::Waypoint) if(const auto hint=town.knownWaypointPosition(*scene.area))
            bearing=Vec{float(hint->x),float(hint->y)};
        if(passage_) {
            const auto *p=knowledge->passage(*passage_);
            if(!p || p->evidence!=passageEvidence_ || p->deferred==p->evidence) {passage_.reset();passageTarget_.reset();passageObservationSince_.reset();}
        }
        const auto plan=search_.observedRoute(*scene.area,*pathPosition,0,false,bearing,2,passage_.value_or(0),bearing.has_value(),
            bearing?std::optional{std::vector<DebugExplorationMemory::Cell>{}}:std::nullopt);
        if(plan.points.empty()) {
            if(knowledge->commitment && plan.outcome!=DebugExplorationMemory::Plan::Outcome::ExecutionBlocked)
                DebugExplorationMemory::defer(*knowledge,*knowledge->commitment);
            return {};
        }
        passage_=plan.passage;passageEvidence_=plan.evidence;passageTarget_=plan.points.back();
        if(const auto *p=knowledge->passage(plan.passage)) {knowledge->heading=p->direction;knowledge->commitment=p->id;}
        std::optional<OnlinePoint> next;
        for(size_t i=1;i<plan.points.size() && i<=40;++i) {
            const auto at=plan.points[i];
            if(!staticRouteGrid.walkable({float(int(at.x)-origin.x)+.5f,float(int(at.y)-origin.y)+.5f},playerMovement)) break;
            next=at;
        }
        if(!next || player==*passageTarget_) {
            if(player==*passageTarget_ && DebugNavigationSearch::passageObservationReady(town,view,*knowledge->passage(*passage_),
                {float(passageTarget_->x),float(passageTarget_->y)},passageObservationSince_)) {
                DebugExplorationMemory::defer(*knowledge,*passage_);passage_.reset();passageTarget_.reset();passageObservationSince_.reset();
            }
            return {};
        }
        source_="observed-passage";return next;
    };
    const auto planningCollision=scene.town?search_.planningCollision(town,view,unit_):Grid{};
    const auto &executionPlanning=scene.town?planningCollision:staticRouteGrid;
    Grid routeGrid;routeGrid.width=executionPlanning.width;routeGrid.height=executionPlanning.height;
    // Planning and execution share acquired collision; map art never fills
    // missing pages. Keep live town blockers on top of static knowledge.
    routeGrid.obstacleRevision=(uint64_t{1}<<63)|(knowledge?knowledge->revision:0);
    routeGrid.missileCollisionRevision=executionPlanning.obstacleRevision;
    routeGrid.collisionSample=[knowledge,origin,&executionPlanning](int x,int y,EntityId ignored)->uint16_t {
        if(!knowledge || knowledge->mask(x+origin.x,y+origin.y)==0xffff) return 0xffff;
        return executionPlanning.movementMask(x,y,ignored);
    };
    const auto pointerClear=[&](Vec to) {
        // PATH_RayTrace checks entered cells, permitting escape when another
        // unit now overlaps the starting footprint. Every destination and
        // entered cell must still pass the native movement mask.
        if(!pathGrid.walkable(to,playerMovement)) return false;
        const auto clipped=pathGrid.nativeMovementTarget(from,to,playerMovement);
        return point(clipped)==point(to);
    };
    const auto advancesTownPointer=[&](Vec next) {
        if(!scene.town || !pathMoving || !segment_ || lastRequest_!=segment_ || lastTarget_!=point_ || !lastPointerFeet_) return true;
        const Vec end=Vec{float(segment_->x),float(segment_->y)}-offset+Vec{.5f,.5f};
        if(!pointerClear(end)) return true; // An obstructed leg must be replaceable.
        const Vec start=*lastPointerFeet_-offset+Vec{.5f,.5f};
        const Vec direction=(end-start).unit();
        const Vec remaining=end-from,extension=next-end;
        // Continuous progress along the submitted leg, not a required visit
        // to its integer cell. A perpendicular/diagonal shortcut can advance
        // beyond this plane; a hairpin waits until near the necessary corner.
        return remaining.x*direction.x+remaining.y*direction.y<=.5f ||
            extension.x*direction.x+extension.y*direction.y>=0;
    };
    // Account for real local movement before evaluating a pending click.
    // Adjacent-cell jitter is not progress. The wait branch must not hide
    // the movement it is meant to retain.
    if(!previous_ || distance(*previous_,player)>=2) {previous_=player;progressAt_=now;}
    if(scene.town && recoverableRelay_ && server_ && !recoveryAnchor_) {
        const auto terrain=staticRouteGrid.freezeMovement?staticRouteGrid.freezeMovement():nullptr;
        const bool sameTerrain=terrain && relayTerrain_ && relayClient_ &&
            terrain->sameGlobalRegion(*relayTerrain_,int(relayClient_->x)-8,int(relayClient_->y)-8,
                int(relayClient_->x)+8,int(relayClient_->y)+8);
        // Net displacement, not distance to the destination: a valid detour
        // may initially move away from the goal. Both observations must stall.
        if(!relayClient_ || !relayHost_ || distance(player,*relayClient_)>=4 ||
            distance(*server_,*relayHost_)>=4 || !sameTerrain) {
            relayClient_=player;relayHost_=server_;relayTerrain_=terrain;
            relayProgressAt_=now;relayProgressMoves_=moves_;
        } else if(distance(player,*server_)<=2 && moves_-relayProgressMoves_>=4 &&
            now-relayProgressAt_>=std::chrono::milliseconds(std::max(1200,2*timing_.walkRetryMs))) {
            finish(DebugWalkState::NeedsReplan,"Town relay repeatedly replaced local pointers without confirmed displacement",control);return;
        }
    }
    if(goal_==Goal::Point && point_ && !unit_) {
        const float span=distance(player,*point_);
        if(span+1<=closestPoint_) {closestPoint_=span;closestAt_=now;}
        // Bouncing in front of a blocked point keeps moving but never closes in; long detours get longer.
        else if(now-closestAt_>std::chrono::seconds(closestPoint_<=16?8:25) && !recoveryAnchor_) {
            finish(DebugWalkState::Failed,"No closing progress toward the point (closest "+
                std::to_string(int(closestPoint_))+"): "+(reason_.empty()?control.reason():reason_),control);return;
        }
    }
    const auto ready=[&](Vec at) {
        if(!unit_ || !town.interactionReady(view,*unit_,at)) return false;
        // A later host arrival makes this same standing point useful again.
        // Keep the existing bounded recovery count instead of walking into
        // the unit merely because the local point was clicked earlier.
        if(point(at+Vec{.5f,.5f})==player && town.interactionReady(view,*unit_)) return true;
        return !closeApproach_ || std::none_of(visited_.begin(),visited_.end(),[&](const auto &p) {
            return point(at+Vec{.5f,.5f})==OnlinePoint{uint16_t(p.first),uint16_t(p.second)};
        });
    };
    const auto settle=std::chrono::milliseconds(std::clamp<uint64_t>(2ULL*view.latencyMilliseconds.value_or(0)+200,600,2000));
    const auto townTurnWait=std::chrono::milliseconds(std::clamp<uint64_t>(2ULL*view.latencyMilliseconds.value_or(0)+timing_.townTurnBaseMs,
        timing_.townTurnMinMs,timing_.townTurnMaxMs));
    const auto actor=view.load.playerUnitId?view.world.units.find({0,*view.load.playerUnitId}):view.world.units.end();
    if(scene.town && segment_ && segment_==lastRequest_ && actor!=view.world.units.end() &&
        actor->second.positionDiscontinuity!=legDiscontinuity_) {
        // A 15 supersedes both the predicted corner and its coalesced pointer.
        control.cancelMovement();segment_.reset();pointerLane_.reset();lastRequest_.reset();townTurnSince_={};
    }
    std::optional<Vec> laggingTurnOrigin;
    const auto holdTownTurn=[&](Vec next) {
        laggingTurnOrigin.reset();
        if(scene.town!=pointerLane_->town || !segment_ || lastRequest_!=segment_ || !lastFrom_ || actor==view.world.units.end() ||
            actor->second.positionDiscontinuity!=legDiscontinuity_) return false;
        const Vec bend=Vec{float(segment_->x),float(segment_->y)}-offset+Vec{.5f,.5f};
        const Vec approach=Vec{float(lastFrom_->x),float(lastFrom_->y)}-offset+Vec{.5f,.5f};
        const Vec in=(bend-approach).unit(),out=(next-bend).unit();
        if((next-bend).length()<1 || in.x*out.x+in.y*out.y>.85f ||
            search_.wallClearance(map.grid,bend)>DebugNavigationSearch::townClearance+1) return false;
        // Keep the submitted leg through the inside corner. The latest host
        // sample behind it may need a little longer than the displayed feet.
        const bool early=pathMoving && !pointerClear(next);
        const auto &unit=actor->second;
        const Vec host=Vec{float(server_->x),float(server_->y)}-offset+Vec{.5f,.5f};
        // Pointer updates can outpace native verification. Requiring a sample
        // newer than this leg disables the bend guard on every replacement.
        // An unchanged-area verification still constrains a safe turn; it
        // never becomes a request to walk back to that sampled position.
        const bool behind=unit.pathVerificationRevision && unit.positionRevision==unit.pathVerificationRevision &&
            distance(*server_,*segment_)>1 && distance(*server_,*segment_)<=50 &&
            !map.grid.nativeMovementSegment(host,next,playerMovement);
        if(!early && !behind) return false;
        if(townTurnSince_==Clock::time_point{}) townTurnSince_=now;
        if(now-townTurnSince_>=townTurnWait) {
            if(behind) laggingTurnOrigin=host;
            return false;
        }
        reason_=behind?"Keeping the town bend while the latest host position cannot reach the next leg":
            "Finishing the town bend before turning";return true;
    };
    // Recovery belongs to walking, not to a particular interaction. A fresh
    // verification plus stopped host progress can expose a missed bend before
    // the predicted player reaches an NPC, portal or ordinary ground goal.
    const bool divergent=server_ && distance(player,*server_)>8;
    const bool failedOpening=resyncApproach_ && unit_ && server_ && distance(player,*server_)>2 &&
        town.interactionReady(view,*unit_,*pathPosition) && !town.interactionReady(view,*unit_);
    const bool verifiedStall=divergent && (moves_ || interactions_) && actor!=view.world.units.end() &&
        actor->second.pathVerificationRevision>hostProgressRevision_ &&
        actor->second.positionRevision==actor->second.pathVerificationRevision &&
        actor->second.verifiedDestination && distance(*server_,*actor->second.verifiedDestination)<=1 &&
        !view.world.npcRequested && !view.world.waypointRequested &&
        now-hostProgressAt_>=std::max(2*settle,std::chrono::milliseconds(2000)) &&
        (!pathMoving || !map.grid.nativeMovementSegment(
            Vec{float(server_->x),float(server_->y)}-offset+Vec{.5f,.5f},from,playerMovement));
    if(!recoveryAnchor_ && movementRecoveries_<2 && (failedOpening || verifiedStall)) {
        const Vec host=Vec{float(server_->x),float(server_->y)}-offset+Vec{.5f,.5f};
        if(pathGrid.nativeMovementSegment(host,host,playerMovement)) {
            recoveryAnchor_=server_;resyncApproach_=false;++movementRecoveries_;
            pointerLane_.reset();
            control.cancelMovement();segment_.reset();lastRequest_.reset();townTurnSince_={};
            source_="walk-rejoin-host";state_=DebugWalkState::Moving;clickedAt_={};
        }
    }
    if(!recoveryAnchor_ && movementRecoveries_>=2 && verifiedStall) {
        finish(DebugWalkState::Failed,"No observed player progress on host after bounded walking recovery",control,DebugWalkFailure::NoProgress);return;
    }
    if(recoveryAnchor_) {
        // Keep the original goal. Walk back in collision-checked short legs,
        // including the bends, rather than issuing one long ray to old feet.
        if(distance(player,*recoveryAnchor_)<=2 || (server_ && distance(player,*server_)<=2)) {
            recoveryAnchor_.reset();control.cancelMovement();segment_.reset();lastRequest_.reset();lastFrom_.reset();
            forceApproach_=unit_.has_value();source_="walk-rejoined";townTurnSince_={};
            progressAt_=hostProgressAt_=now;hostProgressRevision_=view.world.revision;
            reason_="Rejoined observed feet; resuming the original walking goal";return;
        }
        if(now-progressAt_>std::chrono::seconds(15)) {
            finish(DebugWalkState::Failed,"No observed player progress while rejoining the blocked walking path",control,DebugWalkFailure::NoProgress);return;
        }
        if(segment_ && pathMoving && distance(player,*segment_)>1) return;
        const Vec anchor=Vec{float(recoveryAnchor_->x),float(recoveryAnchor_->y)}-offset+Vec{.5f,.5f};
        const auto back=search_.path(pathGrid,from,anchor,true,playerMovement,true);
        std::optional<OnlinePoint> next;int rank=-1;
        const Vec host=Vec{float(server_->x),float(server_->y)}-offset+Vec{.5f,.5f};
        for(const Vec node:back) {
            const Vec delta=node-from;const float span=delta.length();
            if(span<=0) continue;
            for(float reach=std::min(18.f,span);reach>0;reach-=1.f) {
                const auto candidate=point(from+delta*(reach/span)+offset);
                const Vec at=Vec{float(candidate.x),float(candidate.y)}-offset+Vec{.5f,.5f};
                if(candidate==player || !pointerClear(at)) continue;
                // Beyond the host's 50-subtile request range the leg still walks back; the host
                // answers an out-of-range request with its ordinary reassignment.
                const bool inRange=distance(candidate,*server_)<=50;
                const int score=inRange?(pathGrid.nativeMovementSegment(host,at,playerMovement)?2:1):0;
                if(!next || score>=rank) {next=candidate;rank=score;}
                break;
            }
            if(span>=18 || !pointerClear(node)) break;
        }
        if(!next) {
            if(now-progressAt_>std::chrono::seconds(3)) {
                finish(DebugWalkState::Failed,"No collision-checked return leg toward the observed host position",control);return;
            }
            reason_="Waiting for a reachable return leg or ordinary server position correction";return;
        }
        if(lastFrom_==player && lastRequest_==next && now-submittedAt_<std::chrono::milliseconds(timing_.walkRetryMs)) return;
        control.cancelMovement();
        if(!control.move(*next,run_,{},pathPosition)) {reason_=control.reason();return;}
        lastFrom_=player;lastRequest_=segment_=next;lastTarget_=recoveryAnchor_;
        legDiscontinuity_=actor==view.world.units.end()?0:actor->second.positionDiscontinuity;
        submittedAt_=now;++moves_;reason_="Returning through the blocked walking bend";return;
    }
    if(clickedAt_!=Clock::time_point{} && unit_ && (unit_->type==1 || unit_->type==5 || waypointOpening_)) {
        // RemoteControl first supplements the opening at 100 ms intervals.
        // If that burst has no result, retain this target and choose a closer
        // collision-checked approach, never the old sparse server position.
        if(activated && !view.world.waypointRequested) {
            // First use activated the pad. Travel needs a separate menu opening.
            if(control.interact(*unit_,run_,{},pathPosition)) {
                source_="native-unit-approach";clickedAt_={};++interactions_;progressAt_=now;return;
            }
        }
        const bool waitingAtTarget=town.interactionReady(view,*unit_) && town.interactionReady(view,*unit_,*pathPosition) &&
            ((unit_->type==1 && view.world.npcRequested==unit_->id) ||
             (waypointOpening_ && view.world.waypointRequested==unit_->id));
        const bool waypointUnanswered=waitingAtTarget && waypointOpening_ && now-clickedAt_>=3*settle;
        if(waypointUnanswered && goal_==Goal::Unit) {
            control.useWaypoint(0);finish(DebugWalkState::Failed,"Waypoint opening remained unanswered in range",control);return;
        }
        if((waitingAtTarget && !waypointUnanswered) || (unit_->type==1 && view.world.npcPendingConversation && view.world.npcRequested==unit_->id) ||
            now-clickedAt_<settle || now-progressAt_<settle) {
            reason_="Waiting for the actual opening after bounded quick clicks";return;
        }
        if(unit_->type==1 && view.world.npcRequested==unit_->id) control.closeNpc();
        if(waypointOpening_ && view.world.waypointRequested==unit_->id) control.useWaypoint(0);
        if(approachRetries_>=2) {finish(DebugWalkState::Failed,"Interaction opening had no result after quick clicks and local approaches",control,DebugWalkFailure::ApproachUnanswered);return;}
        visited_.emplace(player.x,player.y);++approachRetries_;closeApproach_=true;
        forceApproach_=true;
        source_="unit-collision-detour";
        clickedAt_={};segment_.reset();control.cancelMovement();progressAt_=now;state_=DebugWalkState::Moving;
    }
    const auto deferPassage=[&]() {
        if(!point_ || unit_ || landmark_ || !passage_) return false;
        if(knowledge) DebugExplorationMemory::defer(*knowledge,*passage_);
        passage_.reset();passageTarget_.reset();passageObservationSince_.reset();
        visited_.emplace(point_->x,point_->y);point_.reset();segment_.reset();control.cancelMovement();
        progressAt_=now;state_=DebugWalkState::Searching;reason_="Passage approach stalled; retaining this direction until local evidence changes";
        return true;
    };
    const auto openingEvidence=[&](std::pair<int,int> at) {
        size_t signature=1469598103934665603ull;
        if(knowledge) for(int dy=-4;dy<=4;++dy) for(int dx=-4;dx<=4;++dx)
            signature=(signature^knowledge->mask(at.first+dx,at.second+dy))*1099511628211ull;
        return signature;
    };
    if(now-progressAt_>std::chrono::seconds(15)) {
        if(deferPassage()) return;
        if(goal_==Goal::Exit && landmark_ && !unit_ && point_) {
            // A predicted clear seam can still stop on the original host.
            // Try another known opening on this same connection, rather than
            // restarting the entire task or visiting an unrelated room.
            // A distant opening without a path yet is only deferred until more terrain is reconstructed.
            if(distance(player,*point_)>16) deferredOpenings_[{point_->x,point_->y}]=openingEvidence({point_->x,point_->y});
            else visited_.emplace(point_->x,point_->y);
            point_.reset();segment_.reset();landmark_=false;
            control.cancelMovement();progressAt_=now;state_=DebugWalkState::Searching;
            reason_="Stopped at a walking opening; selecting another opening on the same connection";return;
        }
        const auto detail=reason_.empty()?control.reason():reason_;
        finish(DebugWalkState::Failed,"No observed player progress for 15 seconds: "+detail,control,DebugWalkFailure::NoProgress);return;
    }
    const auto inLevel=[&](OnlinePoint p) {
        const int x=int(p.x)-origin.x,y=int(p.y)-origin.y;
        return std::any_of(map.terrain.rooms.begin(),map.terrain.rooms.end(),[&](const auto &room) {
            return scene.area==room.level && x>=room.x*5 && y>=room.y*5 && x<(room.x+room.width)*5 && y<(room.y+room.height)*5;
        });
    };
    if(!unit_ && (goal_==Goal::Waypoint || goal_==Goal::Exit)) {
        const OnlineMapTarget *best=nullptr;
        for(const auto &target:scene.mapTargets) {
            const bool matches=goal_==Goal::Waypoint?(target.interaction==OnlineMapInteraction::Waypoint && town.assignedUnitLevel(target.unit)==scene.area):
                target.interaction==OnlineMapInteraction::Exit && (!destination_ || target.destination==destination_);
            if(matches && inLevel(target.position) && (!best || distance(player,target.position)<distance(player,best->position))) best=&target;
        }
        if(best) {unit_=best->unit;point_=best->position;segment_.reset();source_="assigned-unit";reason_.clear();}
    }
    if(goal_==Goal::Exit && !unit_ && !landmark_ && !point_) {
        const auto connection=search_.walkingConnection(town,*pathPosition,destination_?destination_:exitLevel_);
        if(!connection.bearing) {
            point_=observedPassage();
            if(!point_) {source_="walking-connection";reason_="Waiting for an observed passage or assigned exit";return;}
        } else {
            if(interact_) {finish(DebugWalkState::Failed,"Walking connections support approach only; there is no warp unit to interact with",control);return;}
            std::erase_if(deferredOpenings_,[&](const auto &entry){return openingEvidence(entry.first)!=entry.second;});
            std::vector<DebugExplorationMemory::Cell> approaches;
            for(const auto &opening:connection.openings) {
                const auto at=opening.point;
                if(localCrossing_ && distance(at,player)>8) continue;
                if(std::any_of(visited_.begin(),visited_.end(),[&](auto p) {
                    return distance(at,{uint16_t(p.first),uint16_t(p.second)})<3;
                }) || std::any_of(deferredOpenings_.begin(),deferredOpenings_.end(),[&](const auto &entry) {
                    return distance(at,{uint16_t(entry.first.first),uint16_t(entry.first.second)})<3;
                })) continue;
                approaches.emplace_back(at.x,at.y);
            }
            if(localCrossing_ && approaches.empty()) {
                finish(DebugWalkState::Failed,"Adjacent crossing no longer has a known legal approach",control);return;
            }
            const Vec bearing{float(connection.bearing->x),float(connection.bearing->y)};
            const auto plan=search_.observedRoute(*scene.area,*pathPosition,0,false,bearing,0,0,!localCrossing_,std::move(approaches));
            if(plan.points.empty()) {
                if(plan.outcome==DebugExplorationMemory::Plan::Outcome::ExecutionBlocked) {
                    state_=DebugWalkState::Searching;reason_="Known walking connection is temporarily blocked by live occupancy";return;
                }
                if(pathMoving) {reason_="Keeping the moving native path until a safe approach can be planned";return;}
                finish(DebugWalkState::Failed,"Known walking connection has no reachable acquired approach or pending passage",control);return;
            }
            const auto endpoint=plan.points.back();
            if(plan.passage) {
                crossing_.reset();
                passage_=plan.passage;passageEvidence_=plan.evidence;passageTarget_=endpoint;
                if(const auto *p=knowledge->passage(plan.passage)) {knowledge->heading=p->direction;knowledge->commitment=p->id;}
                source_="observed-passage";
            } else {
                passage_.reset();passageTarget_.reset();passageObservationSince_.reset();
                const auto opening=std::find_if(connection.openings.begin(),connection.openings.end(),
                    [&](const auto &entry){return entry.point==endpoint;});
                if(opening==connection.openings.end()) throw std::logic_error("Connection route lost its checked opening");
                edgeStart_=opening->boundaryStart;edgeEnd_=opening->boundaryEnd;crossing_=opening->crossing;exitLevel_=opening->destination;
                source_="walking-connection-route";
            }
            // Plan in all acquired terrain, execute only a resident local leg.
            // A window edge is a relay boundary, never proof that the exit is unreachable.
            if(player==endpoint) point_=endpoint;
            else for(size_t i=1;i<plan.points.size() && i<=40;++i) {
                const auto at=plan.points[i];
                if(!staticRouteGrid.walkable({float(int(at.x)-origin.x)+.5f,float(int(at.y)-origin.y)+.5f},playerMovement)) break;
                point_=at;
            }
            if(!point_) {state_=DebugWalkState::Searching;reason_="Waiting for resident collision along the acquired connection route";return;}
            landmark_=!plan.passage && crossing_ && *point_==endpoint;
            if(landmark_) source_="walking-connection-opening";
            ++searches_;reason_.clear();
        }
    }
    if(unit_) {
        const auto target=std::find_if(scene.mapTargets.begin(),scene.mapTargets.end(),[&](const auto &entry){return entry.unit==*unit_;});
        if(target==scene.mapTargets.end()) {finish(DebugWalkState::Failed,"Target is no longer assigned in the client scene",control,DebugWalkFailure::TargetWithdrawn);return;}
        point_=target->position;
        if(interact_ && !interactions_ && !forceApproach_ && unit_->type==1 && view.world.npcRequested==unit_->id) {
            // A resumed plan may already have an opening in flight. Give it
            // the same receipt and position checks as a newly submitted click.
            clickedAt_=now;state_=DebugWalkState::Interacting;reason_="Observing the NPC opening already in flight";return;
        }
        const bool awaitingAlert=waitQuestAlert_ && !interactions_ && !view.world.questAlerts.contains(*unit_);
        if(awaitingAlert && ready(*pathPosition)) {
            if(alertWaitSince_==Clock::time_point{}) alertWaitSince_=now;
            if(now-alertWaitSince_<settle) {reason_="In talk range; allowing the NPC quest alert to arrive";return;}
        } else alertWaitSince_={};
        if(interact_ && (unit_->type==1 || unit_->type==2 || unit_->type==5) && (!interactions_ || forceApproach_ || source_=="unit-opening-stance")) {
            // Stage around collision until a direct ordinary approach is
            // available, then click the real unit. NPCs, doors and entrances
            // share this chain; the client owns its final path and arrival.
            const Vec targetPoint{float(int(point_->x)-origin.x)+.5f,float(int(point_->y)-origin.y)+.5f};
            // A failed unit click can leave local feet beside the target
            // while the host is still on the other side of its collision.
            // Choose an ordinary approach point visible from both positions.
            const bool localReady=town.interactionReady(view,*unit_,*pathPosition);
            const bool hostReady=town.interactionReady(view,*unit_);
            if(forceApproach_ && localReady && (!hostReady || changeStance_) && server_) {
                const Vec host{float(int(server_->x)-origin.x)+.5f,float(int(server_->y)-origin.y)+.5f};
                std::optional<OnlinePoint> stance;
                float best=std::numeric_limits<float>::max();
                for(int dy=-4;dy<=4;++dy) for(int dx=-4;dx<=4;++dx) {
                    const Vec at=targetPoint+Vec{float(dx),float(dy)};
                    const auto candidate=point(at+offset);
                    if(distance(candidate,player)<(changeStance_?1:2) || !pointerClear(at) || !ready(at+offset-Vec{.5f,.5f}) ||
                        !pathGrid.nativeMovementSegment(host,at,playerMovement)) continue;
                    const float score=changeStance_?(at-targetPoint).length()+.01f*(at-from).length():(at-host).length()+(at-from).length();
                    if(score<best) {stance=candidate;best=score;}
                }
                if(!stance && !changeStance_) {
                    // Opposite sides of a hut may have no common straight
                    // approach. Follow the first reachable bend from the last
                    // host position after the opening failed, without changing
                    // the displayed player position or the server replica.
                    const auto detour=search_.path(routeGrid,host,targetPoint,true,playerMovement,true);
                    for(const Vec node:detour) {
                        const auto bend=point(node+offset);
                        if(distance(bend,*server_)<2) continue;
                        if(!pathGrid.nativeMovementSegment(host,node,playerMovement)) break;
                        if(distance(bend,player)>=2 && !visited_.contains({bend.x,bend.y})) stance=bend;
                    }
                }
                if(stance && control.move(*stance,run_,{},pathPosition)) {
                    pointerLane_.reset();
                    visited_.emplace(stance->x,stance->y);forceApproach_=false;source_="unit-opening-stance";segment_=stance;
                    submittedAt_=now;progressAt_=now;++moves_;return;
                }
            }
            if(source_=="unit-opening-stance" && (pathMoving || now-submittedAt_<settle)) return;
            if(changeStance_ && localReady) {
                if(source_!="unit-opening-stance" || !segment_ || player!=*segment_) {
                    finish(DebugWalkState::Failed,"No alternate interaction position reached after confirmed transfer failure",control);return;
                }
                changeStance_=false;
            }
            const bool localArrival=ready(*pathPosition);
            const bool waypoint=target->interaction==OnlineMapInteraction::Waypoint;
            const auto directApproach=[&] {
                // Pointer range is not route knowledge, including portals and
                // exits with solid centres; their observed perimeter is planned below.
                if(awaitingAlert) return false;
                if(distance(player,*point_)>32) {
                    // Beyond the conservative staging distance, only hand
                    // off a town click when both observed feet are in range.
                    if(!scene.town || interactions_ || forceApproach_ || (!waypoint && target->interaction!=OnlineMapInteraction::Npc) ||
                        !server_ || std::abs(pathPosition->x-point_->x)>50 || std::abs(pathPosition->y-point_->y)>50 ||
                        std::abs(int(server_->x)-point_->x)>50 || std::abs(int(server_->y)-point_->y)>50) return false;
                }
                if(target->interaction==OnlineMapInteraction::Npc) {
                    if(distance(player,*point_)<=32) return DebugNavigationSearch::directNpcApproach(town,view,*unit_,*pathPosition);
                    return pointerClear(targetPoint);
                }
                if(!waypoint) return routeGrid.nativeMovementSegment(from,targetPoint,playerMovement);
                // The pad itself may be solid. The integer ray locates the
                // last clear cell, but can skip cells crossed from precise feet.
                const auto end=routeGrid.nativeMovementTarget(from,targetPoint,playerMovement);
                return ready(end+offset-Vec{.5f,.5f}) && routeGrid.segment(from,end,{},playerMovement);
            };
            if(localArrival || directApproach()) {
                if(!hostReady && holdTownTurn(pathGrid.nativeMovementTarget(from,targetPoint,playerMovement))) return;
                if(laggingTurnOrigin) {
                    reason_="Keeping the town bend before handing off the unit approach";return;
                }
                const bool follow=forceApproach_ || (!waypoint && localArrival && !hostReady);
                if(!control.interact(*unit_,run_,{},pathPosition,follow)) {reason_=control.reason();return;}
                pointerLane_.reset();
                forceApproach_=false;
                source_="native-unit-approach";state_=DebugWalkState::Interacting;++interactions_;progressAt_=now;
                reason_="Normal unit click submitted; waiting for automatic approach and interaction";
                return;
            }
            source_="unit-collision-detour";
        }
        if(source_=="native-unit-approach" && clickedAt_==Clock::time_point{}) {
            if((unit_->type==1 && view.world.npcRequested==unit_->id) ||
                (unit_->type==5 && view.world.movementRequest && view.world.movementRequest->unit==unit_ && view.world.movementRequest->interaction)) {
                clickedAt_=now;interactionFrom_=player;interactionRevision_=view.world.revision;
                reason_="Normal unit approach submitted its opening click";return;
            }
            if(now-progressAt_<(pathMoving?2*settle:settle)) {
                reason_=control.awaitingServerArrival()?"Waiting for host interaction range":"Waiting for native unit approach progress";return;
            }
            // A stopped native approach needs an actual collision detour,
            // not a stale-position backtrack or an unbounded click loop.
            control.cancelApproach();source_="unit-collision-detour";segment_.reset();state_=DebugWalkState::Moving;
            reason_="Normal unit approach stopped; planning around current collision";
        }
        // Resource/path verification can leave a stopped local path behind
        // an object/tile the server already reached. PlrMsg validates an ordinary
        // unit13 and owns its approach; submit through the same control
        // entry using that observed origin, without relocating prediction or
        // treating the old position sample as an interaction receipt.
        const bool observedInteractionApproach=!closeApproach_ && (unit_->type==2 || unit_->type==5) && !pathMoving &&
            now-progressAt_>=std::chrono::milliseconds(100) && distance(player,*server_)>8 &&
            town.interactionReady(view,*unit_);
        if(interact_ && (ready(*pathPosition) || observedInteractionApproach)) {
            // The local native path is the ordinary pointer's arrival clock.
            // A sparse server sample must not send the player backwards or
            // postpone the first click. RemoteControl retains a short, bounded
            // opening retry; only the actual response establishes success.
            {
                if((unit_->type==1 || unit_->type==2 || unit_->type==5) &&
                    (pathMoving || now-progressAt_<std::chrono::milliseconds(100))) {
                    // Objects have PlrMsg's ordinary server-side approach. A
                    // tile's earlier 02/04 only approaches, without requesting a
                    // warp. Only the actual scene can confirm the travel result.
                    reason_="Completing the local interaction approach";return;
                }
                if(state_==DebugWalkState::Interacting && control.approaching()) return;
                if(interactions_ && now-progressAt_<std::chrono::milliseconds(100)) return;
                if(interactions_>=3) {finish(DebugWalkState::Failed,"Server did not open the requested interaction",control,DebugWalkFailure::InteractionUnanswered);return;}
                const auto submissionRevision=view.world.revision;
                const auto discontinuity=actor==view.world.units.end()?0:actor->second.positionDiscontinuity;
                if(!control.interact(*unit_,run_,{},observedInteractionApproach?std::nullopt:pathPosition)) {
                    reason_=control.reason();
                    // No new operation was queued. Keep observing the original deadline.
                    if(reason_=="Interaction is already pending or open") return;
                    finish(DebugWalkState::Failed,reason_,control);return;
                }
                pointerLane_.reset();
                interactionRevision_=submissionRevision;interactionDiscontinuity_=discontinuity;interactionFrom_=observedInteractionApproach?*server_:player;
                ++interactions_;progressAt_=now;state_=DebugWalkState::Interacting;
                if(unit_->type==1 || unit_->type==5 || waypointOpening_) clickedAt_=now;
                else if(goal_!=Goal::Waypoint) finish(DebugWalkState::Completed,observedInteractionApproach?
                    "Unit interaction submitted from the observed server approach; verify the server result separately":
                    "Interaction submitted; verify the server result separately",control);
                return;
            }
        }
    }
    const auto movementTarget=point_;
    if(pointerLane_) {
        // A lane adjustment is a two-leg route, not a fresh clearance vote
        // at every foot sample. Keep its continuation in world coordinates.
        if(scene.town!=pointerLane_->town || !segment_ || segment_!=lastRequest_ || movementTarget!=pointerLane_->target) pointerLane_.reset();
        else {
            const Vec bend=Vec{float(pointerLane_->bend.x),float(pointerLane_->bend.y)}-offset+Vec{.5f,.5f};
            const Vec resume=Vec{float(pointerLane_->resume.x),float(pointerLane_->resume.y)}-offset+Vec{.5f,.5f};
            // Latch the continuation as soon as a forward shortcut clears
            // the corner. Never demand a visit to the optional offset cell.
            if(pointerClear(resume) && advancesTownPointer(resume)) pointerLane_->resuming=true;
            // At the far end return to ordinary lookahead before stopping
            // on the saved join cell. The same progress guard protects it.
            if((pointerLane_->resuming && (resume-from).length()<=6) ||
                !pointerClear(pointerLane_->resuming?resume:bend) ||
                (!pointerLane_->resuming && !pathGrid.nativeMovementSegment(bend,resume,playerMovement))) pointerLane_.reset();
        }
    }
    if(segment_) {
        if(now-submittedAt_<std::chrono::milliseconds(timing_.walkRequestIntervalMs)) return;
        const Vec oldLocal{float(int(segment_->x)-origin.x)+.5f,float(int(segment_->y)-origin.y)+.5f};
        // Observe the target/interaction above first. A still-moving pointer
        // for that same target needs only its NEXT ray checked, not AStar.
        // Reconsider within the lookahead window while still moving. The
        // forward-progress guard below rejects regressions, not diagonals.
        if(pathMoving && lastRequest_==segment_ && lastTarget_==movementTarget && distance(player,*segment_)>6 &&
            (scene.town?pointerClear(oldLocal):
                pathGrid.segment(from,oldLocal,{},playerMovement) && pathGrid.nativeMovementSegment(from,oldLocal,playerMovement))) {
            search_.reuseSegment();reason_.clear();return;
        }
        // A stopped local path can have a clipped endpoint. Do not leave its
        // stale RemoteControl goal coalescing the next legitimate input away.
        if(!pathMoving && !routeDoor_) control.cancelMovement();
    }
    if(!point_ && goal_!=Goal::Exit) {
        point_=observedPassage();
        if(!point_) {state_=DebugWalkState::Searching;reason_="Waiting for an assigned target or an observed pending passage";return;}
        ++searches_;state_=DebugWalkState::Searching;
    }
    const bool searching=!unit_ && !landmark_ && (goal_==Goal::Waypoint || goal_==Goal::Exit);
    // A walking boundary is a checked opening, not an interaction rectangle.
    // Reaching another cell within four subtiles can aim the crossing ray
    // through its wall corner. Reach the selected native cell before crossing.
    const int arrival=searching?(source_=="observed-passage" || source_=="walking-connection-route"?0:4):goal_==Goal::Exit && landmark_ && !unit_?0:unit_ || landmark_?4:exactPoint_?0:1;
    if(distance(player,*point_)<=arrival) {
        if(searching) {
            if(source_=="observed-passage" && knowledge && passage_ && passageTarget_ && player==*passageTarget_) {
                if(!DebugNavigationSearch::passageObservationReady(town,view,*knowledge->passage(*passage_),
                    {float(passageTarget_->x),float(passageTarget_->y)},passageObservationSince_)) {
                    reason_="Waiting for confirmed arrival and client collision publication at the passage";return;
                }
                const auto *p=knowledge->passage(*passage_);
                if(p && p->evidence==passageEvidence_) DebugExplorationMemory::defer(*knowledge,*passage_);
                passage_.reset();passageTarget_.reset();passageObservationSince_.reset();
            }
            point_.reset();segment_.reset();return;
        }
        if(scene.town && goal_==Goal::Exit && !unit_ && crossing_) {
            const Vec across{float(crossing_->x),float(crossing_->y)};
            if(holdTownTurn(across-offset+Vec{.5f,.5f})) return;
        }
        if(!interact_) {finish(DebugWalkState::Completed,"Reached target vicinity through client movement",control);return;}
    }
    const auto pathTarget=*point_;
    const Vec target{float(int(pathTarget.x)-origin.x)+.5f,float(int(pathTarget.y)-origin.y)+.5f};
    std::deque<Vec> path;
    if(pointerLane_) {
        const auto next=pointerLane_->resuming?pointerLane_->resume:pointerLane_->bend;
        path.push_back(Vec{float(next.x),float(next.y)}-offset+Vec{.5f,.5f});
    }
    if(path.empty() && localCrossing_ && !unit_) {
        if(!pointerClear(target) && !map.grid.nativeMovementSegment(from,target,playerMovement)) {
            finish(DebugWalkState::Failed,"Adjacent crossing became obstructed",control);return;
        }
        path.push_back(target);
    }
    if(path.empty()) path=search_.path(routeGrid,from,target,unit_.has_value(),playerMovement,true);
    if(!pointerLane_ && unit_ && (unit_->type==1 || unit_->type==2 || unit_->type==5) && interact_ && (path.empty() ||
        !ready(path.back()+offset-Vec{.5f,.5f}))) {
        // Clipping a path to an object's center can stop at a rectangle's
        // excluded corner. Search actual interaction positions around it;
        // never treat closeness to that clipped endpoint as an operation.
        std::vector<OnlinePoint> approaches;
        for(const auto &[x,y]:search_.interactionApproaches(town,view,*unit_)) {
            const Vec at{float(x),float(y)},local=at-offset+Vec{.5f,.5f};
            if(pathGrid.nativeMovementSegment(local,local,playerMovement) && ready(at))
                approaches.push_back({uint16_t(x),uint16_t(y)});
        }
        std::stable_sort(approaches.begin(),approaches.end(),[&](auto a,auto b){return distance(player,a)<distance(player,b);});
        std::vector<DebugNavigationSearch::Candidate> interactionCandidates;
        for(const auto candidate:approaches)
            interactionCandidates.push_back({{float(candidate.x)-offset.x+.5f,float(candidate.y)-offset.y+.5f},1,1});
        if(!interactionCandidates.empty()) {
            const auto selected=search_.selectCandidate(routeGrid,from,interactionCandidates,{},0,false,offset,
                DebugNavigationSearch::PlanningPurpose::Interaction);
            if(selected.found) path=selected.route.points;
        }
    }
    std::optional<OnlineUnitKey> nextDoor;
    Vec previousNode=from;
    for(const auto node:path) {
        const Vec delta=node-previousNode;
        const int steps=std::max(1,int(std::ceil(std::max(std::abs(delta.x),std::abs(delta.y)))));
        for(int step=0;step<=steps && !nextDoor;++step) {
            const Vec at=previousNode+delta*(float(step)/steps);
            for(const auto &obstacle:map.grid.obstacles) {
                const OnlineUnitKey key{2,uint32_t(obstacle.id.value-1)};
                const auto door=view.world.units.find(key);
                if((unit_ && key==*unit_) || door==view.world.units.end() || !door->second.classId || door->second.mode!=0 ||
                    !search_.ordinaryDoor(*door->second.classId)) continue;
                for(int dy=-1;dy<=1 && !nextDoor;++dy) for(int dx=-1;dx<=1;++dx) {
                    if(!nativeCollisionSizeCell(playerMovement.size,dx,dy)) continue;
                    const int x=int(std::floor(at.x))+dx,y=int(std::floor(at.y))+dy;
                    if(x>=obstacle.x && y>=obstacle.y && x<obstacle.x+obstacle.width && y<obstacle.y+obstacle.height) {
                        nextDoor=key;break;
                    }
                }
                if(nextDoor) break;
            }
        }
        if(nextDoor) break;
        previousNode=node;
    }
    if(routeDoor_) {
        const auto door=view.world.units.find(*routeDoor_);
        if(door==view.world.units.end() || door->second.mode!=0) {
            routeDoor_.reset();blockedStep_.reset();segment_.reset();lastRequest_.reset();progressAt_=now;
        } else if(now-routeDoorSince_>=std::chrono::seconds(12)) {
            finish(DebugWalkState::Failed,"Door on the planned route remained closed",control);return;
        }
    }
    if(!routeDoor_ && nextDoor) {
        const auto &door=view.world.units.at(*nextDoor);
        if(door.position && distance(player,*door.position)<=10) {
            routeDoor_=nextDoor;routeDoorSince_=now;routeDoorRequest_={};
        }
    }
    if(routeDoor_) {
        if(routeDoorRequest_==Clock::time_point{} || now-routeDoorRequest_>=std::chrono::seconds(1)) {
            if(control.interact(*routeDoor_,run_,{},pathPosition)) routeDoorRequest_=now;
        }
        reason_="Opening the closed door on the planned route";return;
    }
    const bool interactionApproach=unit_ && interact_ && !path.empty() &&
        ready(path.back()+offset-Vec{.5f,.5f});
    if(!pointerLane_ && (path.empty() || (unit_ && interact_ && !interactionApproach) ||
        (!searching && !interactionApproach && (path.back()-target).length()>6))) {
        if(pathMoving) {reason_="Keeping the moving native path until a safe new leg is available";return;}
        auto staticPath=search_.path(routeGrid,from,target,unit_.has_value(),playerMovement,true);
        if(!staticPath.empty() && (unit_ && interact_?ready(staticPath.back()+offset-Vec{.5f,.5f}):
            (staticPath.back()-target).length()<=6)) {
            if(scene.town && goal_==Goal::Point && !interact_) {
                // An occupied distant part of the route need not block the
                // next ground click. The integer ray below still checks live units.
                path=std::move(staticPath);
            } else {reason_="Waiting for a temporarily occupied client route";return;}
        } else {
            if(unit_ && interact_ && !interactionFrom_ && scene.area) {
                const auto observed=search_.observedRoute(*scene.area,*pathPosition,0,false,
                    Vec{float(point_->x),float(point_->y)},0,0,false,search_.interactionApproaches(town,view,*unit_));
                if(observed.outcome==DebugExplorationMemory::Plan::Outcome::ExecutionBlocked) {
                    reason_="Interaction route is temporarily occupied; retaining the assigned target";return;
                }
                if(observed.points.empty()) {
                    // An assigned pointer with unknown approach terrain is not
                    // an execution failure. Return knowledge ownership to AI.
                    finish(DebugWalkState::NeedsObservation,"Target requires further terrain observation before interaction",control);return;
                }
            }
            // The parent owns Teleport; if it has not moved us within a few seconds, let it re-plan.
            if(!moves_ && now-progressAt_>std::chrono::seconds(4)) {
                finish(DebugWalkState::Failed,"No walking path and no mobility progress",control);return;
            }
            // An opening behind unreconstructed rooms: walk frontiers first instead of idling 15s.
            if(goal_==Goal::Exit && landmark_ && !unit_ && distance(player,pathTarget)>16 && now-progressAt_>std::chrono::seconds(2)) {
                deferredOpenings_[{pathTarget.x,pathTarget.y}]=openingEvidence({pathTarget.x,pathTarget.y});
                point_.reset();segment_.reset();landmark_=false;progressAt_=now;state_=DebugWalkState::Searching;
                reason_="Walking opening has no reconstructed path yet; exploring toward it first";return;
            }
            reason_="No current walking path; waiting for mobility or collision progress";return;
        }
    }
    // Invalid/ending segments replan without waiting for movement to stop.
    // Quantizing a clipped leg can change its collision
    // ray, so recheck the actual integer pointer before submitting it.
    auto leg=std::find_if(path.begin(),path.end(),[&](Vec p){return point(p+offset)!=player;});
    if(leg==path.end()) {
        if(pathMoving) return;
        finish(DebugWalkState::Failed,"Path produced no reachable movement segment",control,DebugWalkFailure::NoReachableSegment);return;
    }
    if(scene.town && !pointerLane_) {
        // Skip redundant path corners when their continuation is already
        // visible. Validate the whole shortcut before clipping its input;
        // a clear prefix toward a hidden node is not permission to skip a bend.
        for(auto next=std::next(leg);next!=path.end() && pointerClear(*next);++next) leg=next;
    }
    std::optional<OnlinePoint> planned,blockedSegment;
    std::optional<PointerLane> plannedLane;
    OnlinePoint rawPointer{},lanePointer{};
    std::string pointerBranch=pointerLane_?"town-lane-continuation":"route";
    for(auto node=leg;node!=path.end() && !planned;++node) {
        const auto delta=*node-from;const float span=delta.length();
        if(span<=0) continue;
        for(float reach=std::min(18.f,span);reach>0;reach-=1.f) {
            const auto candidate=point(from+delta*(reach/span)+offset);
            if(candidate==player) continue;
            const Vec local{float(int(candidate.x)-origin.x)+.5f,float(int(candidate.y)-origin.y)+.5f};
            if(!pointerClear(local)) {
                if(!scene.town && map.grid.nativeMovementSegment(from,local,playerMovement) &&
                    !pathGrid.nativeMovementSegment(from,local,playerMovement) && !blockedSegment) blockedSegment=candidate;
                continue;
            }
            planned=candidate;
            rawPointer=lanePointer=candidate;
            if(!pointerLane_ && (scene.town || exploration_)) {
                const auto after=std::next(node);
                const auto onward=(local-*node).length()<1 && after!=path.end()?*after:*node;
                Vec lane=local;
                Vec resume=onward;
                if(scene.town && adjustLane_) {
                    // Clearance may adjust an actual bend, never insert an
                    // elbow on an already traversable straight/diagonal ray.
                    const auto incoming=(local-from).unit(),outgoing=(onward-local).unit();
                    if(after!=path.end() && (local-*node).length()<1 &&
                        incoming.x*outgoing.x+incoming.y*outgoing.y<.98f) {
                        lane=search_.townWalkLane(map.grid,pathGrid,from,local,onward);
                        if((lane-local).length()>0) pointerBranch="town-bend";
                    }
                } else if(!scene.town) lane=search_.explorationLane(map.grid,pathGrid,from,local,onward,true);
                const auto lanePoint=point(lane+offset);
                lanePointer=lanePoint;
                if(lanePoint!=player && pointerClear(lane) && advancesTownPointer(lane)) {
                    planned=lanePoint;
                    if(scene.town && lanePoint!=candidate) plannedLane=PointerLane{lanePoint,point(resume+offset),pathTarget,false,true};
                    else if(!scene.town && lanePoint!=candidate) pointerBranch="exploration-lane";
                }
            }
            break;
        }
    }
    if(blockedSegment && !pointerLane_) {
        const Vec start=from+offset-Vec{.5f,.5f},end{float(blockedSegment->x),float(blockedSegment->y)};
        const Vec endLocal=end-offset+Vec{.5f,.5f};
        auto units=town.navigationBlockers(view,start,end);
        if(!units.empty()) {
            const auto bypass=search_.localObstacleLane(pathGrid,from,endLocal);
            if(bypass) {
                const auto bend=point(*bypass+offset);
                planned=rawPointer=lanePointer=bend;
                plannedLane=PointerLane{bend,*blockedSegment,pathTarget,false,false};
                pointerBranch="local-unit-bypass";
            } else {
                // Publish the upcoming obstruction even when a shorter prefix
                // is walkable. The AI may shoot before entering the crowd.
                blockedStep_=BlockedStep{start,end,std::move(units)};
            }
        }
    }
    if(!planned) {
        if(pathMoving) {reason_="Keeping the moving native path until an integer pointer ray is clear";return;}
        reason_=blockedSegment?"Next movement segment temporarily occupied":"Waiting for a collision-checked integer pointer or mobility recovery";return;
    }
    auto next=*planned;
    const Vec nextLocal=Vec{float(next.x),float(next.y)}-offset+Vec{.5f,.5f};
    if(!advancesTownPointer(nextLocal)) {
        search_.reuseSegment();reason_="Keeping forward progress along the current town leg";return;
    }
    if(holdTownTurn(nextLocal)) return;
    if(laggingTurnOrigin) {
        // After the brief grace prefer a shorter forward input visible from
        // both feet. Never walk back to the packet position to reconcile it.
        const Vec delta=nextLocal-from;
        bool commonPointer=false;
        for(int n=int(delta.length())-1;n>=2;--n) {
            const auto sample=from+delta.unit()*float(n);
            const Vec at{std::floor(sample.x)+.5f,std::floor(sample.y)+.5f};
            if(pointerClear(at) && pathGrid.nativeMovementSegment(*laggingTurnOrigin,at,playerMovement) &&
                pathGrid.nativeMovementSegment(at,nextLocal,playerMovement)) {
                next=point(at+offset);commonPointer=true;break;
            }
        }
        if(!commonPointer) {
            // No safe shared continuation exists yet. Replacing the bend
            // anyway makes the host clip its path against the inside wall.
            reason_="Keeping the town bend until a forward pointer is reachable from both observed positions";
            return;
        }
        if(!advancesTownPointer(Vec{float(next.x),float(next.y)}-offset+Vec{.5f,.5f})) {
            reason_="Waiting for a shared forward continuation around the town corner";return;
        }
    }
    if(segment_ && pathMoving) {
        if(next==*segment_) return;
        const Vec retained{float(int(segment_->x)-origin.x)+.5f,float(int(segment_->y)-origin.y)+.5f};
        if(scene.town && distance(player,*segment_)>0 && pointerClear(retained) &&
            now-submittedAt_<std::chrono::milliseconds(timing_.walkRequestIntervalMs)) {
            reason_="Retaining the checked town segment before replacing its pointer";return;
        }
        const Vec previous{float(segment_->x)-pathPosition->x,float(segment_->y)-pathPosition->y};
        const Vec following{float(next.x)-pathPosition->x,float(next.y)-pathPosition->y};
        const float lengths=previous.length()*following.length();
        const float dot=previous.x*following.x+previous.y*following.y;
        const Vec oldLocal{float(int(segment_->x)-origin.x)+.5f,float(int(segment_->y)-origin.y)+.5f};
        // Keep a still-valid straight leg until near its end. A changed
        // direction or obstruction may replace it immediately while moving.
        if(distance(player,*segment_)>6 && lengths>0 && dot>.98f*lengths &&
            pathGrid.segment(from,oldLocal,{},playerMovement) && pathGrid.nativeMovementSegment(from,oldLocal,playerMovement)) return;
    }
    // Newly hired/following units and moving NPCs can briefly block a normal
    // path. Keep the existing 15-second progress bound rather than declaring
    // failure after a few samples. Identical stopped input uses the configured
    // retry interval; actual progress/direction changes replan normally.
    if(lastFrom_==player && lastRequest_==next && now-submittedAt_<std::chrono::milliseconds(timing_.walkRetryMs)) {
        reason_="Waiting before retrying the same temporarily stopped native path";return;
    }
    if(!control.move(next,run_,{},pathPosition)) {reason_=control.reason();return;}
    // A host-lag guard may have shortened the input before the selected
    // bend. Do not claim that shortened input completed the two-leg plan.
    if(plannedLane && next==plannedLane->bend) pointerLane_=plannedLane;
    lastFrom_=player;lastRequest_=next;lastPointerFeet_=*pathPosition;townTurnSince_={};
    legDiscontinuity_=actor==view.world.units.end()?0:actor->second.positionDiscontinuity;
    segment_=next;lastTarget_=point_;submittedAt_=now;++moves_;reason_.clear();if(!searching) state_=DebugWalkState::Moving;
    if(pointerSelections_.size()>=32) pointerSelections_.pop_front();
    pointerSelections_.push_back({moves_,player,pathTarget,rawPointer,lanePointer,next,*pathPosition,
        pointerLane_?std::optional{pointerLane_->resume}:std::nullopt,std::move(pointerBranch)});
    } catch(const DebugSearchPending &) {
        // Keep the normal path and all real request/progress deadlines. The
        // cached query prefix resumes next frame; nothing was unreachable.
        cadence_.continueNextFrame();reason_="Waiting for background navigation";
    } catch(const std::exception &error) {finish(DebugWalkState::Failed,error.what(),control);}
}
}
