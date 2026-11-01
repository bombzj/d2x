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
        {"interactionApproachRetries",approachRetries_},
        {"clientPosition",client_?Json{{"x",client_->x},{"y",client_->y}}:Json(nullptr)},
        {"serverPosition",server_?Json{{"x",server_->x},{"y",server_->y}}:Json(nullptr)},
        {"targetSource",source_},{"exitLevel",exitLevel_?Json(*exitLevel_):Json(nullptr)},{"navigationSearch",search_.snapshot()},
        {"decisionIntervalMs",cadence_.intervalMs()},{"decisions",decisions_},{"clientMoving",clientMoving_},
        {"scheduler",{{"mode","adaptive"},{"wakeReason",cadence_.reason()},{"eventWakeups",cadence_.eventWakeups()},
            {"timerWakeups",cadence_.timerWakeups()},{"searchContinuations",cadence_.continuations()}}},
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
        lastFrom_.reset();lastRequest_.reset();lastTarget_.reset();closeApproach_=false;approachRetries_=0;clickedAt_={};decisions_=0;clientMoving_=false;
        forceApproach_=request.value("forceApproach",false);
        moves_=interactions_=searches_=0;interactionFrom_.reset();interactionRevision_=view.world.revision;interactionDiscontinuity_=0;
        progressAt_=Clock::now();deadline_=progressAt_+std::chrono::seconds(timeout);
        executionClock_.reset(progressAt_);
        cadence_.reset();
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
    if(unit_ && unit_->type==1 && view.world.npcConversation && view.world.npcConversation->source==unit_->id) {
        finish("completed","Actual NPC conversation opened",control);return;
    }
    const auto &submitted=control.interactionSubmission();
    if(unit_ && unit_->type==2 && source_=="native-unit-approach" && submitted && submitted->target==unit_ &&
        onlineWorldMatches(submitted->context,view) && (submitted->revision>interactionRevision_ ||
            (interactionFrom_ && submitted->revision==interactionRevision_))) {
        // Observe the actual 13 submission even when its resulting menu now
        // blocks movement. The parent owns the object's distinct postcondition.
        interactionFrom_=submitted->position;interactionRevision_=submitted->revision;
        interactionDiscontinuity_=submitted->discontinuity;
        if(goal_==Goal::Waypoint && view.world.waypointSource!=unit_->id) {
            // This goal explicitly needs a menu for travel / the initial
            // history query. Neutral activation alone cannot complete it.
            // Ordinary unit activation has no such second click.
            if(!unavailable && view.world.waypointActivation &&
                view.world.waypointActivation->source==unit_->id &&
                view.world.waypointActivation->generation==view.world.interactionGeneration &&
                !view.world.waypointRequested)
                control.interact(*unit_,run_,{},pathPosition);
            if(now>=deadline_) finish("failed","Waypoint click had no menu result before its deadline",control);
            return;
        }
        finish("completed","Normal unit approach submitted the object interaction",control);return;
    }
    if(unit_ && goal_==Goal::Waypoint && view.world.waypointSource==unit_->id) {
        finish("completed","Server waypoint menu opened",control);return;
    }
    if(unavailable) return;
    const bool nearEnd=segment_ && pathPosition && (Vec{float(segment_->x),float(segment_->y)}-*pathPosition).length()<=6;
    std::array<uint64_t,24> signals{view.world.areaGeneration,town.navigationRevision(),uint64_t(pathMoving),uint64_t(nearEnd),
        view.world.interactionGeneration,view.world.waypointSource.value_or(0),view.world.rooms.size()};
    if(unit_) if(const auto found=view.world.units.find(*unit_);found!=view.world.units.end()) {
        signals[7]=found->second.assignmentRevision;signals[8]=found->second.mode.value_or(0);
        if(found->second.position) signals[9]=uint64_t(found->second.position->x)|(uint64_t(found->second.position->y)<<16);
    }
    const auto mode=nearEnd || !pathMoving?DebugDecisionCadence::Mode::Reaction:DebugDecisionCadence::Mode::Navigation;
    if(!cadence_.due(now,mode,signals,!pathMoving,std::min(deadline_,progressAt_+std::chrono::seconds(15)))) return;
    if(now>=deadline_) {finish("failed","Debug navigation deadline expired",control);return;}
    ++decisions_;clientMoving_=pathMoving;
    if(unit_ && server_ && view.world.playerPosition && *server_!=*view.world.playerPosition) {
        const auto target=view.world.units.find(*unit_);
        if(target!=view.world.units.end() && target->second.position &&
            distance(*view.world.playerPosition,*target->second.position)<distance(*server_,*target->second.position))
            progressAt_=now;
    }
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
    const auto pointerClear=[&](Vec to) {
        // PATH_RayTrace checks entered cells, permitting escape when another
        // unit now overlaps the starting footprint. Every destination and
        // entered cell must still pass the native movement mask.
        if(!pathGrid.walkable(to,playerMovement)) return false;
        const auto clipped=pathGrid.nativeMovementTarget(from,to,playerMovement);
        return point(clipped)==point(to);
    };
    // Account for real local movement before evaluating a pending click.
    // Adjacent-cell jitter is not progress. The wait branch must not hide
    // the movement it is meant to retain.
    if(!previous_ || distance(*previous_,player)>=2) {previous_=player;progressAt_=now;}
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
    if(clickedAt_!=Clock::time_point{} && unit_ && (unit_->type==1 || unit_->type==5)) {
        // RemoteControl first supplements the opening at 100 ms intervals.
        // If that burst has no result, retain this target and choose a closer
        // collision-checked approach, never the old sparse server position.
        const auto settle=std::chrono::milliseconds(std::max<uint64_t>(600,2*view.latencyMilliseconds.value_or(0)+200));
        const bool approachInFlight=(unit_->type==1 && view.world.npcRequested==unit_->id) ||
            (unit_->type==5 && view.world.movementRequest && view.world.movementRequest->unit==unit_ && view.world.movementRequest->interaction);
        const auto receiptWindow=approachInFlight && !town.interactionReady(view,*unit_)?std::chrono::milliseconds(5000):settle;
        // A locally stopped prediction can be ahead of an advancing host.
        // Keep the click while the ordinary native approach makes progress;
        // RemoteControl completes it on the actual position receipt.
        const auto assigned=view.world.units.find(*unit_);
        const bool refreshedOutside=unit_->type==1 && assigned!=view.world.units.end() &&
            assigned->second.positionRevision>interactionRevision_ && !town.interactionReady(view,*unit_);
        if((unit_->type==1 && view.world.npcRequested==unit_->id && !refreshedOutside) || view.world.npcPendingConversation ||
            now-clickedAt_<settle || (approachInFlight && now-progressAt_<receiptWindow)) {
            reason_="Waiting for the actual opening after bounded quick clicks";return;
        }
        if(approachRetries_>=2) {finish("failed","Interaction opening had no result after quick clicks and local approaches",control);return;}
        if(unit_->type==1 && view.world.npcRequested==unit_->id) session.close_npc();
        visited_.emplace(player.x,player.y);++approachRetries_;closeApproach_=true;
        forceApproach_=true;
        source_="unit-collision-detour";
        clickedAt_={};segment_.reset();control.cancelMovement();progressAt_=now;state_="moving";
    }
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
        if(interact_ && (unit_->type==1 || unit_->type==2 || unit_->type==5) && (!interactions_ || forceApproach_ || source_=="unit-opening-stance")) {
            // Stage around collision until a direct ordinary approach is
            // available, then click the real unit. NPCs, doors and entrances
            // share this chain; the client owns its final path and arrival.
            const Vec targetPoint{float(int(point_->x)-origin.x)+.5f,float(int(point_->y)-origin.y)+.5f};
            // A failed unit click can leave local feet beside the target
            // while the host is still on the other side of its collision.
            // Choose an ordinary approach point visible from both positions.
            if(forceApproach_ && town.interactionReady(view,*unit_,*pathPosition) && !town.interactionReady(view,*unit_) && server_) {
                const Vec host{float(int(server_->x)-origin.x)+.5f,float(int(server_->y)-origin.y)+.5f};
                std::optional<OnlinePoint> stance;
                float best=std::numeric_limits<float>::max();
                for(int dy=-4;dy<=4;++dy) for(int dx=-4;dx<=4;++dx) {
                    const Vec at=targetPoint+Vec{float(dx),float(dy)};
                    const auto candidate=point(at+offset);
                    if(distance(candidate,player)<2 || !pointerClear(at) || !ready(at+offset-Vec{.5f,.5f}) ||
                        !pathGrid.nativeMovementSegment(host,at,playerMovement)) continue;
                    const float score=(at-host).length()+(at-from).length();
                    if(score<best) {stance=candidate;best=score;}
                }
                if(!stance) {
                    // Opposite sides of a hut may have no common straight
                    // approach. Follow the first reachable bend from the last
                    // host position after the opening failed, without changing
                    // the displayed player position or the server replica.
                    const auto detour=search_.path(pathGrid,host,targetPoint,true,playerMovement,true);
                    for(const Vec node:detour) {
                        const auto bend=point(node+offset);
                        if(distance(bend,*server_)<2) continue;
                        if(!pathGrid.nativeMovementSegment(host,node,playerMovement)) break;
                        if(distance(bend,player)>=2 && !visited_.contains({bend.x,bend.y})) stance=bend;
                    }
                }
                if(stance && control.move(*stance,run_,{},pathPosition)) {
                    visited_.emplace(stance->x,stance->y);forceApproach_=false;source_="unit-opening-stance";segment_=stance;
                    submittedAt_=now;progressAt_=now;++moves_;return;
                }
            }
            if(source_=="unit-opening-stance" && (pathMoving || now-submittedAt_<std::chrono::milliseconds(600))) return;
            // Leave room for sparse host positions at the native 50-cell
            // request boundary. This staging distance is AI policy only.
            if(ready(*pathPosition) || (distance(player,*point_)<=32 &&
                pathGrid.nativeMovementSegment(from,targetPoint,playerMovement))) {
                const bool follow=forceApproach_ || (ready(*pathPosition) && !town.interactionReady(view,*unit_));
                if(!control.interact(*unit_,run_,{},pathPosition,follow)) {reason_=control.reason();return;}
                forceApproach_=false;
                source_="native-unit-approach";state_="interacting";++interactions_;progressAt_=now;
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
            if(pathMoving || now-progressAt_<std::chrono::milliseconds(600)) {
                reason_="Waiting for the normal unit approach to finish";return;
            }
            if(control.approaching()==unit_ && control.awaitingServerArrival()) {
                reason_="Following the assigned unit until the host confirms interaction range";return;
            }
            const Vec targetPoint{float(int(point_->x)-origin.x)+.5f,float(int(point_->y)-origin.y)+.5f};
            if(control.approaching()==unit_ && now-progressAt_<std::chrono::seconds(5) &&
                pathGrid.nativeMovementSegment(from,targetPoint,playerMovement)) {
                // No observed collision explains the stop. Let the normal
                // controller consume the host's path receipt and continue
                // this click before replacing it with a detour.
                reason_="Waiting for native unit approach progress or its arrival receipt";return;
            }
            // A stopped native approach needs an actual collision detour,
            // not a stale-position backtrack or an unbounded click loop.
            control.cancelApproach();source_="unit-collision-detour";segment_.reset();state_="moving";
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
                if(state_=="interacting" && control.approaching()) return;
                if(interactions_ && now-progressAt_<std::chrono::milliseconds(100)) return;
                if(interactions_>=3) {finish("failed","Server did not open the requested interaction",control);return;}
                const auto submissionRevision=view.world.revision;
                const auto actor=view.load.playerUnitId?view.world.units.find({0,*view.load.playerUnitId}):view.world.units.end();
                const auto discontinuity=actor==view.world.units.end()?0:actor->second.positionDiscontinuity;
                if(!control.interact(*unit_,run_,{},observedInteractionApproach?std::nullopt:pathPosition)) {
                    reason_=control.reason();
                    // A different nearby door can finish before the shared input
                    // throttle expires. This request was not queued; retain the
                    // target and existing deadlines to retry.
                    if(reason_=="Interaction is rate limited") return;
                    finish("failed",reason_,control);return;
                }
                interactionRevision_=submissionRevision;interactionDiscontinuity_=discontinuity;interactionFrom_=observedInteractionApproach?*server_:player;
                ++interactions_;progressAt_=now;state_="interacting";
                if(unit_->type==1 || unit_->type==5) clickedAt_=now;
                else if(goal_!=Goal::Waypoint) finish("completed",observedInteractionApproach?
                    "Unit interaction submitted from the observed server approach; verify the server result separately":
                    "Interaction submitted; verify the server result separately",control);
                return;
            }
        }
    }
    const auto movementTarget=point_;
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
    const auto pathTarget=*point_;
    const Vec target{float(int(pathTarget.x)-origin.x)+.5f,float(int(pathTarget.y)-origin.y)+.5f};
    auto path=search_.path(pathGrid,from,target,unit_.has_value(),playerMovement,true);
    if(unit_ && (unit_->type==1 || unit_->type==2 || unit_->type==5) && interact_ && (path.empty() ||
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
    const bool interactionApproach=unit_ && interact_ && !path.empty() &&
        ready(path.back()+offset-Vec{.5f,.5f});
    if(path.empty() || (!searching && !interactionApproach && (path.back()-target).length()>6)) {
        if(pathMoving) {reason_="Keeping the moving native path until a safe new leg is available";return;}
        const auto staticPath=search_.path(map.grid,from,target,unit_.has_value(),playerMovement,true);
        if(!staticPath.empty() && ((staticPath.back()-target).length()<=6 ||
            (unit_ && interact_ && ready(staticPath.back()+offset-Vec{.5f,.5f})))) {
            reason_="Waiting for a temporarily occupied client route";return;
        }
        // Keep this target active so the parent can select a legal skill
        // landing, or a subsequent room/collision update can provide a path.
        // The existing no-progress and overall deadlines remain in force.
        reason_="No current walking path; waiting for mobility or collision progress";return;
    }
    // Invalid/ending segments replan without waiting for movement to stop.
    // Quantizing a clipped leg can change its collision
    // ray, so recheck the actual integer pointer before submitting it.
    const auto leg=std::find_if(path.begin(),path.end(),[&](Vec p){return point(p+offset)!=player;});
    if(leg==path.end()) {
        if(pathMoving) return;
        finish("failed","Path produced no reachable movement segment",control);return;
    }
    std::optional<OnlinePoint> planned;
    for(auto node=leg;node!=path.end() && !planned;++node) {
        const auto delta=*node-from;const float span=delta.length();
        if(span<=0) continue;
        for(float reach=std::min(18.f,span);reach>0;reach-=1.f) {
            const auto candidate=point(from+delta*(reach/span)+offset);
            if(candidate==player) continue;
            const Vec local{float(int(candidate.x)-origin.x)+.5f,float(int(candidate.y)-origin.y)+.5f};
            if(pointerClear(local)) {planned=candidate;break;}
        }
    }
    if(!planned) {
        // NativePathStep can be between integer cells while moving. A new
        // pointer ray may be invalid at that instant without invalidating the
        // path already in progress; keep judging at the regular cadence.
        if(pathMoving) {reason_="Keeping the moving native path until an integer pointer ray is clear";return;}
        reason_="Waiting for a collision-checked integer pointer or mobility recovery";return;
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
    segment_=next;lastTarget_=point_;submittedAt_=now;++moves_;reason_.clear();if(!searching) state_="moving";
    } catch(const DebugSearchPending &) {
        // Keep the normal path and all real request/progress deadlines. The
        // cached query prefix resumes next frame; nothing was unreachable.
        cadence_.continueNextFrame();reason_="Navigation search budget yielded; resuming candidate queries next frame";
    } catch(const std::exception &error) {finish("failed",error.what(),control);}
}
}
