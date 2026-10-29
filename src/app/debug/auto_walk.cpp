#include "auto_walk.hpp"
#include "client/remote_town.hpp"
#include "client/remote_control.hpp"
#include "client/remote_combat.hpp"
#include "network/realm_session.hpp"
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
        {"clientPosition",client_?Json{{"x",client_->x},{"y",client_->y}}:Json(nullptr)},
        {"serverPosition",server_?Json{{"x",server_->x},{"y",server_->y}}:Json(nullptr)},
        {"targetSource",source_},{"exitLevel",exitLevel_?Json(*exitLevel_):Json(nullptr)},
        {"decisionIntervalMs",debugDecisionInterval.count()},{"decisions",decisions_},{"clientMoving",clientMoving_},
        {"boundaryStart",edgeStart_?Json{{"x",edgeStart_->x},{"y",edgeStart_->y}}:Json(nullptr)},
        {"boundaryEnd",edgeEnd_?Json{{"x",edgeEnd_->x},{"y",edgeEnd_->y}}:Json(nullptr)},
        {"moveRequests",moves_},{"interactionRequests",interactions_},{"searchRooms",searches_}};
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
        lastFrom_.reset();lastRequest_.reset();decisions_=0;clientMoving_=false;
        moves_=interactions_=searches_=0;progressAt_=next_=Clock::now();deadline_=next_+std::chrono::seconds(timeout);
    }
    return Json{{"ok",true},{"navigation",status()}}.dump();
}
void DebugAutoWalk::tick(net::RealmSession &session,RemoteTown &town,RemoteControl &control,bool blocked,std::optional<Vec> pathPosition,bool pathMoving) {
    if(!active()) return;
    try {
    const auto now=Clock::now();const auto &view=session.read();const auto &scene=town.read();
    if(view.gameGeneration!=game_ || view.world.areaGeneration!=area_ || view.stage!=OnlineStage::ProtocolReady ||
        (scene.area && scene.area!=level_)) {
        finish("cancelled","Game/area changed; start a new navigation intent",control);return;
    }
    if(onlinePlayerDead(view.world)) {finish("failed","Player died",control);return;}
    if(now>=deadline_) {finish("failed","Debug navigation deadline expired",control);return;}
    if(unit_ && goal_==Goal::Waypoint && view.world.waypointSource==unit_->id) {
        finish("completed","Server waypoint menu opened",control);return;
    }
    if(blocked || !scene.movementAvailable || !scene.origin || !view.world.playerPosition || !town.map() || now<next_) return;
    next_=now+debugDecisionInterval;
    ++decisions_;clientMoving_=pathMoving;
    server_=view.world.playerPosition;
    if(!pathPosition) {reason_="Waiting for the normal client path position";return;}
    // Use the same local DynamicPath position as ordinary pointer input. The
    // server's sparse 96 samples are observations, not the client's input clock.
    const auto player=point(*pathPosition);client_=player;
    const auto origin=*scene.origin;const auto &map=*town.map();
    const Vec offset{float(origin.x),float(origin.y)},from=*pathPosition-offset+Vec{.5f,.5f};
    if(previous_!=player) {previous_=player;progressAt_=now;}
    const auto abandonSearchRoom=[&]() {
        if(!point_ || unit_ || landmark_ || goal_!=Goal::Waypoint) return false;
        visited_.emplace(point_->x,point_->y);point_.reset();segment_.reset();control.cancelMovement();
        progressAt_=now;state_="searching";reason_="Exploration approach stalled; checking another reconstructed room";
        return true;
    };
    if(now-progressAt_>std::chrono::seconds(15)) {
        if(abandonSearchRoom()) return;
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
        const RemoteTown::WalkingBoundary *edge=nullptr;int nearest=INT32_MAX;
        const auto edgePoint=[](const auto &e,int along,int inset) {
            return e.side%2?OnlinePoint{uint16_t(e.plane+(e.side==1?inset:-inset)),uint16_t(along)}:
                OnlinePoint{uint16_t(along),uint16_t(e.plane+(e.side==2?inset:-inset))};
        };
        for(const auto &entry:edges) {
            if((destination_ && entry.destination!=destination_) || (exitLevel_ && entry.destination!=exitLevel_)) continue;
            const auto projected=edgePoint(entry,std::clamp(entry.side%2?int(player.y):int(player.x),entry.start,entry.end-1),3);
            if(distance(player,projected)<nearest) {nearest=distance(player,projected);edge=&entry;}
        }
        if(!edge) {source_="walking-connection";reason_="Waiting for a reconstructed logical walking connection";return;}
        if(interact_) {finish("failed","Walking connections support approach only; there is no warp unit to interact with",control);return;}
        exitLevel_=edge->destination;edgeStart_=edgePoint(*edge,edge->start,0);edgeEnd_=edgePoint(*edge,edge->end,0);
        std::vector<OnlinePoint> candidates;
        for(int along=edge->start+1;along<edge->end-1;++along) {
            const auto approach=edgePoint(*edge,along,3),lip=edgePoint(*edge,along,1);
            const Vec a{float(int(approach.x)-origin.x)+.5f,float(int(approach.y)-origin.y)+.5f};
            const Vec b{float(int(lip.x)-origin.x)+.5f,float(int(lip.y)-origin.y)+.5f};
            if(inLevel(approach) && map.grid.nativeMovementSegment(a,b,playerMovement)) candidates.push_back(approach);
        }
        std::stable_sort(candidates.begin(),candidates.end(),[&](auto a,auto b){return distance(player,a)<distance(player,b);});
        for(const auto candidate:candidates) {
            const Vec local{float(int(candidate.x)-origin.x)+.5f,float(int(candidate.y)-origin.y)+.5f};
            if(!map.grid.path(from,local,false,playerMovement,true).empty()) {
                point_=candidate;landmark_=true;source_="walking-connection-opening";reason_.clear();break;
            }
        }
        if(!point_) {
            // The known connection guides the frontier approach. Partial AStar
            // may use only currently reconstructed collision, never unseen floor.
            int frontierScore=INT32_MAX;
            for(int along=edge->start+1;along<edge->end-1;along+=8) {
                const auto goal=edgePoint(*edge,along,3);
                const Vec local{std::clamp(float(int(goal.x)-origin.x)+.5f,.5f,float(map.grid.width)-.5f),
                    std::clamp(float(int(goal.y)-origin.y)+.5f,.5f,float(map.grid.height)-.5f)};
                const auto path=map.grid.path(from,local,true,playerMovement,true);if(path.empty()) continue;
                const auto frontier=point(path.back()+offset);
                if(distance(player,frontier)<=2 || distance(frontier,goal)>=distance(player,goal)) continue;
                if(std::any_of(visited_.begin(),visited_.end(),[&](auto p){return distance(frontier,{uint16_t(p.first),uint16_t(p.second)})<8;})) continue;
                const int score=distance(player,frontier)+distance(frontier,goal)*2;
                if(score<frontierScore) {
                    frontierScore=score;point_=frontier;source_="walking-connection-frontier";reason_.clear();
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
        if(interact_ && town.interactionReady(view,*unit_,pathPosition)) {
            // An NPC 13 outside the server's <9 distance window is ignored
            // (PlrMsg::sub_6FC828D0), while the local path can already be near.
            // This debug input strategy waits for its ordinary approach to
            // settle; it neither changes native eligibility nor retries 13.
            if(unit_->type==1 && (pathMoving || now-progressAt_<debugDecisionInterval || !town.interactionReady(view,*unit_))) {
                reason_="Waiting for the local NPC approach and native position confirmation";return;
            }
            if(state_=="interacting" && control.approaching()) return;
            if(interactions_ && now-progressAt_<std::chrono::seconds(1)) return;
            if(interactions_>=3) {finish("failed","Server did not open the requested interaction",control);return;}
            if(!control.interact(*unit_,run_,{},pathPosition)) {finish("failed",control.reason(),control);return;}
            ++interactions_;progressAt_=now;state_="interacting";
            if(goal_!=Goal::Waypoint) finish("completed","Interaction submitted; verify the server result separately",control);
            return;
        }
    }
    if(segment_) {
        if(now-submittedAt_<std::chrono::milliseconds(200)) return;
        // A stopped local path can have a clipped endpoint. Do not leave its
        // stale RemoteControl goal coalescing the next legitimate input away.
        if(!pathMoving) control.cancelMovement();
    }
    if(!point_ && goal_!=Goal::Exit) {
        // Search only reconstructed rooms in this level. Walking exposes more
        // server rooms; their unit assignments can then reveal the actual target.
        std::optional<OnlinePoint> candidate;int best=INT32_MAX;
        for(const auto &room:map.terrain.rooms) if(scene.area==room.level) {
            const auto cell=map.grid.nativeSpawn({float((room.x*5)+(room.width*5)/2)+.5f,float((room.y*5)+(room.height*5)/2)+.5f},6,playerMovement);
            if(!cell) continue;
            const auto world=point(*cell+offset);
            if(visited_.contains({world.x,world.y})) continue;
            if(distance(player,world)<3) {visited_.emplace(world.x,world.y);continue;}
            const auto path=map.grid.path(from,*cell,false,playerMovement,true);
            if(!path.empty() && distance(player,world)<best) {candidate=world;best=distance(player,world);}
        }
        // Room reconstruction and unit assignment arrive independently. Keep
        // observing normal client updates until the existing progress deadline;
        // a ready player/map alone does not imply all landmarks have arrived.
        if(!candidate) {state_="searching";reason_="Waiting for an assigned target or reachable client room";return;}
        point_=candidate;++searches_;state_="searching";source_="waypoint-room-search";
    }
    const bool searching=!unit_ && !landmark_ && (goal_==Goal::Waypoint || goal_==Goal::Exit);
    if(distance(player,*point_)<=(unit_ || landmark_?4:1)) {
        if(searching) {visited_.emplace(point_->x,point_->y);point_.reset();segment_.reset();return;}
        if(!interact_) {finish("completed","Reached target vicinity through client movement",control);return;}
    }
    const Vec target{float(int(point_->x)-origin.x)+.5f,float(int(point_->y)-origin.y)+.5f};
    const auto path=map.grid.path(from,target,unit_.has_value(),playerMovement,true);
    if(path.empty() || (!searching && (path.back()-target).length()>6)) {
        if(pathMoving) {reason_="Keeping the moving native path until a safe new leg is available";return;}
        finish("failed","No client collision path to target",control);return;
    }
    // Replan every decision, including while moving, but never cut across an
    // untraversed corner. Quantizing a clipped leg can change its collision
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
        if(map.grid.segment(from,local,{},playerMovement) && map.grid.nativeMovementSegment(from,local,playerMovement)) {
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
            map.grid.segment(from,oldLocal,{},playerMovement) && map.grid.nativeMovementSegment(from,oldLocal,playerMovement)) return;
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
    segment_=next;submittedAt_=now;++moves_;reason_.clear();if(!searching) state_="moving";
    } catch(const std::exception &error) {finish("failed",error.what(),control);}
}
}
