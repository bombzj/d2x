#include "auto_walk.hpp"
#include "client/remote_town.hpp"
#include "client/remote_control.hpp"
#include "client/remote_combat.hpp"
#include "network/realm_session.hpp"
#include "world/interaction_geometry.hpp"
#include <algorithm>
#include <cmath>
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
bool debugUnknownFrontier(const Grid &grid,Vec endpoint) {
    // A known wall is not an observation target. A reachable edge adjacent
    // to unavailable collision can reveal another ordinary client room.
    const int x=int(endpoint.x),y=int(endpoint.y);
    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
        if((!dx && !dy) || (dx && dy)) continue;
        for(int step=1;step<=2;++step) {
            const auto mask=grid.movementMask(x+dx*step,y+dy*step);
            if(mask==0xffff) return true;
            if(mask&playerMovement.mask) break;
        }
    }
    return false;
}
bool DebugAutoWalk::active() const {return state_=="searching" || state_=="moving" || state_=="interacting";}
nlohmann::json DebugAutoWalk::snapshot() const {return status();}
void DebugAutoWalk::finish(std::string state,std::string reason,RemoteControl &control) {
    state_=std::move(state);reason_=std::move(reason);control.cancelMovement();segment_.reset();
}
void DebugAutoWalk::manualInput(RemoteControl &control) {if(active()) finish("cancelled","Manual input took control",control);}
nlohmann::json DebugAutoWalk::status() const {
    using Json=nlohmann::json;
    return {{"state",state_},{"active",active()},{"reason",reason_},{"gameGeneration",game_},{"areaGeneration",area_},
        {"area",level_?Json(*level_):Json(nullptr)},
        {"target",point_?Json{{"x",point_->x},{"y",point_->y}}:Json(nullptr)},
        {"unit",unit_?Json{{"type",unit_->type},{"id",unit_->id}}:Json(nullptr)},
        {"segment",segment_?Json{{"x",segment_->x},{"y",segment_->y}}:Json(nullptr)},
        {"observedPositionBacktracks",observedBacktracks_},
        {"observedPositionBacktrack",observedBacktrack_?Json{{"x",observedBacktrack_->x},{"y",observedBacktrack_->y}}:Json(nullptr)},
        {"clientPosition",client_?Json{{"x",client_->x},{"y",client_->y}}:Json(nullptr)},
        {"serverPosition",server_?Json{{"x",server_->x},{"y",server_->y}}:Json(nullptr)},
        {"targetSource",source_},{"exitLevel",exitLevel_?Json(*exitLevel_):Json(nullptr)},{"navigationSearch",search_.snapshot()},
        {"decisionIntervalMs",debugDecisionInterval.count()},{"decisions",decisions_},{"clientMoving",clientMoving_},
        {"boundaryStart",edgeStart_?Json{{"x",edgeStart_->x},{"y",edgeStart_->y}}:Json(nullptr)},
        {"boundaryEnd",edgeEnd_?Json{{"x",edgeEnd_->x},{"y",edgeEnd_->y}}:Json(nullptr)},
        {"moveRequests",moves_},{"interactionRequests",interactions_},{"searchRooms",searches_},
        {"submittedInteraction",interactionFrom_?Json{{"worldRevision",interactionRevision_},{"discontinuity",interactionDiscontinuity_},
            {"position",{{"x",interactionFrom_->x},{"y",interactionFrom_->y}}}}:Json(nullptr)},
        {"executionBlocked",executionClock_.blocked()},{"suspendedExecutionMs",executionClock_.suspendedMs()},
        {"executionRemainingMs",std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(deadline_-Clock::now()).count())},
        {"excludedOpenings",goal_==Goal::Exit?Json(visited_):Json::array()}};
}
std::optional<std::string> DebugAutoWalk::command(const nlohmann::json &request,net::RealmSession &session,
        RemoteTown &town,RemoteControl &control,RemoteCombat &combat) {
    using Json=nlohmann::json;
    const auto name=request.at("command").get<std::string>();
    if(name!="debug-walk" && name!="debug-walk-status" && name!="debug-walk-cancel") return {};
    if(name=="debug-walk-cancel") {
        if(active()) {
            finish("cancelled","Cancelled by debug command",control);
            OnlineCombatCommand stop;stop.action=OnlineCombatCommand::Action::Stop;combat.submit(stop);
        }
    } else if(name=="debug-walk") {
        if(active()) throw std::invalid_argument("Cancel the current debug walk before starting another");
        const auto &view=session.read();
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
        const bool run=request.value("run",true),interact=request.value("interact",kind==Goal::Waypoint || kind==Goal::Unit);
        control.cancelMovement();goal_=kind;point_=location;unit_=target;destination_=destination;
        game_=view.gameGeneration;area_=view.world.areaGeneration;level_=scene.area;run_=run;interact_=interact;landmark_=false;
        state_="searching";reason_.clear();segment_.reset();previous_=view.world.playerPosition;visited_.clear();
        source_=kind==Goal::Point?"explicit-point":kind==Goal::Unit?"assigned-unit":"";
        client_.reset();server_=view.world.playerPosition;edgeStart_.reset();edgeEnd_.reset();exitLevel_.reset();
        lastFrom_.reset();lastRequest_.reset();lastTarget_.reset();observedBacktrack_.reset();observedBacktracks_=0;decisions_=0;clientMoving_=false;
        moves_=interactions_=searches_=0;interactionFrom_.reset();interactionRevision_=interactionDiscontinuity_=0;
        progressAt_=next_=Clock::now();deadline_=next_+std::chrono::seconds(timeout);
        executionClock_.reset(next_);
    }
    return Json{{"ok",true},{"navigation",status()}}.dump();
}
void DebugAutoWalk::tick(net::RealmSession &session,RemoteTown &town,RemoteControl &control,bool blocked,std::optional<Vec> pathPosition,bool pathMoving) {
    if(!active()) return;
    try {
    const auto now=Clock::now();const auto &view=session.read();const auto &scene=town.read();
    const bool unavailable=blocked || !scene.movementAvailable || !scene.origin || !view.world.playerPosition || !town.map() || !pathPosition;
    const auto suspended=executionClock_.sample(now,unavailable);
    deadline_+=suspended;progressAt_+=suspended;
    if(view.gameGeneration!=game_ || view.world.areaGeneration!=area_ || view.stage!=OnlineStage::ProtocolReady ||
        (scene.area && scene.area!=level_)) {
        finish("cancelled","Game/area changed; start a new navigation intent",control);return;
    }
    if(onlinePlayerDead(view.world)) {finish("failed","Player died",control);return;}
    if(unit_ && goal_==Goal::Waypoint && view.world.waypointSource==unit_->id) {
        finish("completed","Server waypoint menu opened",control);return;
    }
    if(unavailable || now<next_) return;
    if(now>=deadline_) {finish("failed","Debug navigation deadline expired",control);return;}
    next_=now+debugDecisionInterval;
    ++decisions_;clientMoving_=pathMoving;
    server_=view.world.playerPosition;
    if(!pathPosition) {reason_="Waiting for the normal client path position";return;}
    // Use the same local DynamicPath position as ordinary pointer input. The
    // server's sparse 96 samples are observations, not the client's input clock.
    // The local path exposes drawing feet with the half-cell removed, like
    // nativeUnitDistance. Quantize its native subtile, not the drawing floor:
    // otherwise 5089.999 remains one cell behind an authoritative 5090 and
    // repeatedly sends an already-reached 5090 leg instead of progressing.
    client_=point(*pathPosition+Vec{.5f,.5f});
    const auto origin=*scene.origin;const auto &map=*town.map();
    const auto pathGrid=town.navigationGrid(view,unit_);
    // Keep the normal DynamicPath as the input origin even when stopped.
    // A sparse 0x96 sample can stay one cell behind a finished local leg;
    // planning from it resubmits that already reached leg forever instead
    // of selecting the next pointer. Normal position verification owns drift.
    const auto player=point(*pathPosition+Vec{.5f,.5f});
    const Vec offset{float(origin.x),float(origin.y)},from=*pathPosition-offset+Vec{.5f,.5f};
    const auto ready=[&](Vec at) {
        if(!unit_ || !town.interactionReady(view,*unit_,at)) return false;
        // PlrMsg UNIT_TILE permits distance <=4. Prefer its inner range for
        // debug input: a marginal side approach was ignored by the original
        // host, while a normal closer walk entered the same assigned warp.
        return unit_->type!=5 || (point_ && nativeUnitDistance(at,2,
            {float(point_->x),float(point_->y)},0)<=2);
    };
    // Prediction and sparse position receipts can alternate adjacent cells
    // at a blocked corner. Such jitter is not forward progress and must not
    // keep an identical stopped pointer alive until the overall deadline.
    if(!previous_ || distance(*previous_,player)>=2) {previous_=player;progressAt_=now;}
    const auto abandonSearchRoom=[&]() {
        if(!point_ || unit_ || landmark_ || goal_!=Goal::Waypoint) return false;
        visited_.emplace(point_->x,point_->y);point_.reset();segment_.reset();control.cancelMovement();
        progressAt_=now;state_="searching";reason_="Exploration approach stalled; checking another reconstructed room";
        return true;
    };
    if(now-progressAt_>std::chrono::seconds(15)) {
        if(abandonSearchRoom()) return;
        if(goal_==Goal::Exit && landmark_ && !unit_ && point_) {
            // A predicted clear seam can still stop on the original host.
            // Try another known opening on this same connection, rather than
            // restarting the entire task or visiting an unrelated room.
            visited_.emplace(point_->x,point_->y);point_.reset();segment_.reset();landmark_=false;
            control.cancelMovement();progressAt_=now;state_="searching";
            reason_="Stopped at a walking opening; selecting another opening on the same connection";return;
        }
        finish("failed","No observed player progress for 15 seconds: "+control.reason(),control);return;
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
            const bool matches=goal_==Goal::Waypoint?target.interaction==OnlineMapInteraction::Waypoint:
                target.interaction==OnlineMapInteraction::Exit && (!destination_ || target.destination==destination_);
            if(matches && inLevel(target.position) && (!best || distance(player,target.position)<distance(player,best->position))) best=&target;
        }
        if(best) {unit_=best->unit;point_=best->position;segment_.reset();source_="assigned-unit";reason_.clear();}
    }
    if(goal_==Goal::Exit && !unit_ && !landmark_ && !point_) {
        // Render snapshots retain activeNear only. Read the client's already
        // reconstructed logical near links instead; the adjoining room need
        // not be visible yet. Never replace an unknown exit with a room patrol.
        const auto edges=town.walkingBoundaries();
        std::vector<const RemoteTown::WalkingBoundary *> matching;
        const auto edgePoint=[](const auto &e,int along,int inset) {
            return e.side%2?OnlinePoint{uint16_t(e.plane+(e.side==1?inset:-inset)),uint16_t(along)}:
                OnlinePoint{uint16_t(along),uint16_t(e.plane+(e.side==2?inset:-inset))};
        };
        for(const auto &entry:edges) {
            if((destination_ && entry.destination!=destination_) || (exitLevel_ && entry.destination!=exitLevel_)) continue;
            matching.push_back(&entry);
        }
        if(matching.empty()) {source_="walking-connection";reason_="Waiting for a reconstructed logical walking connection";return;}
        if(interact_) {finish("failed","Walking connections support approach only; there is no warp unit to interact with",control);return;}
        std::stable_sort(matching.begin(),matching.end(),[&](const auto *a,const auto *b) {
            const auto projected=[&](const auto &e) {
                return edgePoint(e,std::clamp(e.side%2?int(player.y):int(player.x),e.start,e.end-1),3);
            };
            return distance(player,projected(*a))<distance(player,projected(*b));
        });
        const auto selectEdge=[&](const auto &edge) {
            exitLevel_=edge.destination;edgeStart_=edgePoint(edge,edge.start,0);edgeEnd_=edgePoint(edge,edge.end,0);
        };
        selectEdge(*matching.front());
        struct Opening {OnlinePoint point;const RemoteTown::WalkingBoundary *edge;};
        std::vector<Opening> candidates;
        for(const auto *edge:matching) for(int along=edge->start+1;along<edge->end-1;++along) {
            const auto approach=edgePoint(*edge,along,3),lip=edgePoint(*edge,along,1);
            if(std::any_of(visited_.begin(),visited_.end(),[&](auto p) {
                return distance(approach,{uint16_t(p.first),uint16_t(p.second)})<3;
            })) continue;
            const Vec a{float(int(approach.x)-origin.x)+.5f,float(int(approach.y)-origin.y)+.5f};
            const Vec b{float(int(lip.x)-origin.x)+.5f,float(int(lip.y)-origin.y)+.5f};
            const auto crossing=edgePoint(*edge,along,-3);
            const Vec beyond{float(int(crossing.x)-origin.x)+.5f,float(int(crossing.y)-origin.y)+.5f};
            // A clear lip on this side can still face the next level's wall
            // corner. Check the ordinary crossing too when its collision is
            // already reconstructed; unknown rooms remain observation targets.
            const bool crossingKnown=pathGrid.movementMask(int(beyond.x),int(beyond.y))!=0xffff;
            if(inLevel(approach) && pathGrid.nativeMovementSegment(a,b,playerMovement) &&
                (!crossingKnown || pathGrid.nativeMovementSegment(a,beyond,playerMovement))) candidates.push_back({approach,edge});
        }
        std::stable_sort(candidates.begin(),candidates.end(),[&](const auto &a,const auto &b){return distance(player,a.point)<distance(player,b.point);});
        for(const auto &opening:candidates) {
            const auto candidate=opening.point;
            const Vec local{float(int(candidate.x)-origin.x)+.5f,float(int(candidate.y)-origin.y)+.5f};
            if(search_.reachable(pathGrid,from,local,playerMovement,true)) {
                selectEdge(*opening.edge);point_=candidate;landmark_=true;source_="walking-connection-opening";reason_.clear();break;
            }
        }
        if(!point_) {
            // The known connection guides the frontier approach. Partial AStar
            // may use only currently reconstructed collision, never unseen floor.
            int frontierScore=INT32_MAX;
            for(const auto *edge:matching) for(int along=edge->start+1;along<edge->end-1;along+=8) {
                const auto goal=edgePoint(*edge,along,3);
                const Vec local{std::clamp(float(int(goal.x)-origin.x)+.5f,.5f,float(pathGrid.width)-.5f),
                    std::clamp(float(int(goal.y)-origin.y)+.5f,.5f,float(pathGrid.height)-.5f)};
                const auto path=search_.path(pathGrid,from,local,true,playerMovement,true);if(path.empty()) continue;
                if(!debugUnknownFrontier(pathGrid,path.back())) continue;
                const auto frontier=point(path.back()+offset);
                if(distance(player,frontier)<=2 || distance(frontier,goal)>=distance(player,goal)) continue;
                if(std::any_of(visited_.begin(),visited_.end(),[&](auto p){return distance(frontier,{uint16_t(p.first),uint16_t(p.second)})<8;})) continue;
                const int score=distance(player,frontier)+distance(frontier,goal)*2;
                if(score<frontierScore) {
                    selectEdge(*edge);frontierScore=score;point_=frontier;source_="walking-connection-frontier";reason_.clear();
                }
            }
            if(point_) ++searches_;
            if(!point_) {
                if(pathMoving) {reason_="Keeping the moving native path until a safe approach can be planned";return;}
                finish("failed","Known walking connection has no reachable reconstructed approach",control);return;
            }
        }
    }
    if(unit_) {
        const auto target=std::find_if(scene.mapTargets.begin(),scene.mapTargets.end(),[&](const auto &entry){return entry.unit==*unit_;});
        if(target==scene.mapTargets.end()) {finish("failed","Target is no longer assigned in the client scene",control);return;}
        point_=target->position;
        if(observedBacktrack_ && distance(player,*observedBacktrack_)<=1) {
            observedBacktrack_.reset();segment_.reset();control.cancelMovement();
            reason_="Observed-position backtrack reached through normal movement; resuming the assigned target";return;
        }
        if(interact_ && !observedBacktrack_ && ready(*pathPosition)) {
            // Match an ordinary NPC click: local interaction geometry permits
            // the13 request, and the server still adjudicates its <9 range.
            // A stale resource sample cannot veto that input indefinitely;
            // only a real conversation confirms success. UNIT_TILE can begin
            // a native approach at5..50; settle our last corner before it.
            const bool waitingSample=unit_->type==5 && !town.interactionReady(view,*unit_);
            if(waitingSample && !pathMoving && now-progressAt_>=std::chrono::seconds(3) &&
                distance(player,*server_)>8 && observedBacktracks_<2) {
                // A sparse resource sample is NOT a relocation command or
                // proof that the server is still there. Backtrack to that
                // genuinely observed point by ordinary pointer inputs, then
                // retry the route from a shared observation, never by writing
                // prediction/replica coordinates or replaying a submitted13.
                observedBacktrack_=server_;++observedBacktracks_;segment_.reset();control.cancelMovement();
                reason_="Backing out of a stalled local approach through the last observed position";
            } else {
                if((unit_->type==1 || unit_->type==2 || unit_->type==5) &&
                    (pathMoving || now-progressAt_<debugDecisionInterval || waitingSample)) {
                    // Objects have PlrMsg's ordinary server-side approach. A
                    // tile's earlier 02/04 only approaches, without requesting a
                    // warp. Only the actual scene can confirm the travel result.
                    reason_="Waiting for the local approach or NPC interaction position confirmation";return;
                }
                if(state_=="interacting" && control.approaching()) return;
                if(interactions_ && now-progressAt_<std::chrono::seconds(1)) return;
                if(interactions_>=3) {finish("failed","Server did not open the requested interaction",control);return;}
                const auto submissionRevision=view.world.revision;
                const auto actor=view.load.playerUnitId?view.world.units.find({0,*view.load.playerUnitId}):view.world.units.end();
                const auto discontinuity=actor==view.world.units.end()?0:actor->second.positionDiscontinuity;
                if(!control.interact(*unit_,run_,{},pathPosition)) {
                    reason_=control.reason();
                    // A different nearby door can finish before the shared input
                    // throttle expires. This request was not queued; retain the
                    // target and existing deadlines to retry.
                    if(reason_=="Interaction is rate limited") return;
                    finish("failed",reason_,control);return;
                }
                interactionRevision_=submissionRevision;interactionDiscontinuity_=discontinuity;interactionFrom_=player;
                ++interactions_;progressAt_=now;state_="interacting";
                if(goal_!=Goal::Waypoint) finish("completed","Interaction submitted; verify the server result separately",control);
                return;
            }
        }
    }
    const auto movementTarget=observedBacktrack_?observedBacktrack_:point_;
    if(segment_) {
        if(now-submittedAt_<std::chrono::milliseconds(200)) return;
        const Vec oldLocal{float(int(segment_->x)-origin.x)+.5f,float(int(segment_->y)-origin.y)+.5f};
        // Observe the target/interaction above first. A still-moving pointer
        // for that same target needs only its NEXT ray checked, not AStar.
        // Near the leg end, a moved unit, or changed collision replans now.
        if(pathMoving && lastRequest_==segment_ && lastTarget_==movementTarget && distance(player,*segment_)>6 &&
            pathGrid.segment(from,oldLocal,{},playerMovement) && pathGrid.nativeMovementSegment(from,oldLocal,playerMovement)) {
            search_.reuseSegment();reason_.clear();return;
        }
        // A stopped local path can have a clipped endpoint. Do not leave its
        // stale RemoteControl goal coalescing the next legitimate input away.
        if(!pathMoving) control.cancelMovement();
    }
    if(!point_ && goal_!=Goal::Exit) {
        // Search only reconstructed rooms in this level. Walking exposes more
        // server rooms; their unit assignments can then reveal the actual target.
        std::optional<OnlinePoint> candidate;int best=INT32_MAX;
        const auto remembered=scene.area?town.knownWaypointPosition(*scene.area):std::nullopt;
        if(remembered) {
            const Vec wanted{float(int(remembered->x)-origin.x)+.5f,float(int(remembered->y)-origin.y)+.5f};
            const Vec goal{std::clamp(wanted.x,.5f,float(pathGrid.width)-.5f),std::clamp(wanted.y,.5f,float(pathGrid.height)-.5f)};
            const auto path=search_.path(pathGrid,from,goal,true,playerMovement,true);
            if(!path.empty()) {
                const auto endpoint=point(path.back()+offset);
                if(distance(player,endpoint)>2 &&
                    (distance(endpoint,*remembered)<=6 || debugUnknownFrontier(pathGrid,path.back()))) candidate=endpoint;
            }
            if(!candidate) {state_="searching";source_="remembered-waypoint-position";reason_="Waiting for a reachable approach or real assignment at the remembered waypoint";return;}
        }
        const auto &knownRooms=town.navigationRooms();
        if(!remembered) for(const auto &room:map.terrain.rooms) if(scene.area==room.level) {
            const auto known=std::find_if(knownRooms.begin(),knownRooms.end(),[&](const auto &entry) {
                return entry.level==room.level && entry.x==(room.x+origin.x/5)*5 && entry.y==(room.y+origin.y/5)*5;
            });
            if(known!=knownRooms.end() && known->exploredTiles>=size_t(room.width*room.height)) continue;
            const auto cell=pathGrid.nativeSpawn({float((room.x*5)+(room.width*5)/2)+.5f,float((room.y*5)+(room.height*5)/2)+.5f},6,playerMovement);
            if(!cell) continue;
            const auto world=point(*cell+offset);
            if(visited_.contains({world.x,world.y})) continue;
            if(distance(player,world)<=16) {visited_.emplace(world.x,world.y);continue;}
            const auto path=search_.path(pathGrid,from,*cell,false,playerMovement,true);
            if(!path.empty() && distance(player,world)<best) {candidate=world;best=distance(player,world);}
        }
        // Room reconstruction and unit assignment arrive independently. Keep
        // observing normal client updates until the existing progress deadline;
        // a ready player/map alone does not imply all landmarks have arrived.
        if(!candidate) {state_="searching";reason_="Waiting for an assigned target or reachable client room";return;}
        point_=candidate;++searches_;state_="searching";source_=remembered?"remembered-waypoint-position":"waypoint-room-search";
    }
    const bool searching=!unit_ && !landmark_ && (goal_==Goal::Waypoint || goal_==Goal::Exit);
    // A walking boundary is a checked opening, not an interaction rectangle.
    // Reaching another cell within four subtiles can aim the crossing ray
    // through its wall corner. Reach the selected native cell before crossing.
    const int arrival=searching?(source_=="remembered-waypoint-position"?4:16):goal_==Goal::Exit && landmark_ && !unit_?0:unit_ || landmark_?4:1;
    if(distance(player,*point_)<=arrival) {
        if(searching) {visited_.emplace(point_->x,point_->y);point_.reset();segment_.reset();return;}
        if(!interact_) {finish("completed","Reached target vicinity through client movement",control);return;}
    }
    const auto pathTarget=observedBacktrack_.value_or(*point_);
    const Vec target{float(int(pathTarget.x)-origin.x)+.5f,float(int(pathTarget.y)-origin.y)+.5f};
    auto path=search_.path(pathGrid,from,target,unit_.has_value() && !observedBacktrack_,playerMovement,true);
    if(!observedBacktrack_ && unit_ && (unit_->type==2 || unit_->type==5) && interact_ && (path.empty() ||
        !ready(path.back()+offset-Vec{.5f,.5f}))) {
        // Clipping a path to an object's center can stop at a rectangle's
        // excluded corner. Search actual interaction positions around it;
        // never treat closeness to that clipped endpoint as an operation.
        const auto entry=std::find_if(scene.mapTargets.begin(),scene.mapTargets.end(),[&](const auto &t){return t.unit==*unit_;});
        std::vector<OnlinePoint> approaches;
        if(entry!=scene.mapTargets.end()) {
            const int width=std::clamp(entry->collisionWidth/2+4,4,16),height=std::clamp(entry->collisionHeight/2+4,4,16);
            for(int y=std::max(0,int(point_->y)-height);y<=std::min(65535,int(point_->y)+height);++y)
                for(int x=std::max(0,int(point_->x)-width);x<=std::min(65535,int(point_->x)+width);++x) {
                    const Vec at{float(x),float(y)},local=at-offset+Vec{.5f,.5f};
                    if(pathGrid.nativeMovementSegment(local,local,playerMovement) && ready(at))
                        approaches.push_back({uint16_t(x),uint16_t(y)});
                }
        }
        std::stable_sort(approaches.begin(),approaches.end(),[&](auto a,auto b){return distance(player,a)<distance(player,b);});
        for(const auto candidate:approaches) {
            const Vec at{float(candidate.x)-offset.x+.5f,float(candidate.y)-offset.y+.5f};
            auto alternative=search_.path(pathGrid,from,at,false,playerMovement,true);
            if(!alternative.empty()) {path=std::move(alternative);break;}
        }
    }
    const bool interactionApproach=!observedBacktrack_ && unit_ && interact_ && !path.empty() &&
        ready(path.back()+offset-Vec{.5f,.5f});
    if(path.empty() || (!searching && !interactionApproach && (path.back()-target).length()>6)) {
        if(pathMoving) {reason_="Keeping the moving native path until a safe new leg is available";return;}
        if(!search_.path(map.grid,from,target,true,playerMovement,true).empty()) {
            reason_="Waiting for a temporarily occupied client route";return;
        }
        finish("failed","No client collision path to target",control);return;
    }
    // Invalid/ending segments replan without waiting for movement to stop.
    // Quantizing a clipped leg can change its collision
    // ray, so recheck the actual integer pointer before submitting it.
    const auto leg=std::find_if(path.begin(),path.end(),[&](Vec p){return point(p+offset)!=player;});
    if(leg==path.end()) {
        if(pathMoving) return;
        finish("failed","Path produced no reachable movement segment",control);return;
    }
    const auto delta=*leg-from;const float span=delta.length();
    std::optional<OnlinePoint> planned;
    for(float reach=std::min(18.f,span);reach>0;reach-=1.f) {
        const auto candidate=point(from+delta*(reach/span)+offset);
        if(candidate==player) continue;
        const Vec local{float(int(candidate.x)-origin.x)+.5f,float(int(candidate.y)-origin.y)+.5f};
        if(pathGrid.segment(from,local,{},playerMovement) && pathGrid.nativeMovementSegment(from,local,playerMovement)) {
            planned=candidate;break;
        }
    }
    if(!planned) {
        // NativePathStep can be between integer cells while moving. A new
        // pointer ray may be invalid at that instant without invalidating the
        // path already in progress; keep judging at the regular cadence.
        if(pathMoving) {reason_="Keeping the moving native path until an integer pointer ray is clear";return;}
        finish("failed","No integer input target on the collision-checked leg",control);return;
    }
    const auto next=*planned;
    if(segment_ && pathMoving) {
        if(next==*segment_) return;
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
    // failure after three 200-ms samples. Identical stopped input is bounded
    // to once per second; actual progress/direction changes replan normally.
    if(lastFrom_==player && lastRequest_==next && now-submittedAt_<std::chrono::seconds(1)) {
        reason_="Waiting before retrying the same temporarily stopped native path";return;
    }
    lastFrom_=player;lastRequest_=next;
    if(!control.move(next,run_,{},pathPosition)) {reason_=control.reason();return;}
    segment_=next;lastTarget_=observedBacktrack_?observedBacktrack_:point_;submittedAt_=now;++moves_;reason_.clear();if(!searching) state_="moving";
    } catch(const DebugSearchPending &) {
        // Keep the normal path and all real request/progress deadlines. The
        // cached query prefix resumes next frame; nothing was unreachable.
        next_=Clock::now();reason_="Navigation search budget yielded; resuming candidate queries next frame";
    } catch(const std::exception &error) {finish("failed",error.what(),control);}
}
}
