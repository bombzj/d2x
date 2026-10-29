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
void DebugAutoWalk::finish(std::string state,std::string reason,RemoteControl &control) {
    state_=std::move(state);reason_=std::move(reason);control.cancelMovement();segment_.reset();
}
void DebugAutoWalk::manualInput(RemoteControl &control) {if(active()) finish("cancelled","Manual input took control",control);}
nlohmann::json DebugAutoWalk::status() const {
    using Json=nlohmann::json;
    return {{"state",state_},{"active",active()},{"reason",reason_},{"gameGeneration",game_},{"areaGeneration",area_},
        {"target",point_?Json{{"x",point_->x},{"y",point_->y}}:Json(nullptr)},
        {"unit",unit_?Json{{"type",unit_->type},{"id",unit_->id}}:Json(nullptr)},
        {"segment",segment_?Json{{"x",segment_->x},{"y",segment_->y}}:Json(nullptr)},
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
        const auto &view=session.read();town.update(view);
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
        moves_=interactions_=searches_=0;progressAt_=next_=Clock::now();deadline_=next_+std::chrono::seconds(timeout);
    }
    return Json{{"ok",true},{"navigation",status()}}.dump();
}
void DebugAutoWalk::tick(net::RealmSession &session,RemoteTown &town,RemoteControl &control,bool blocked) {
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
    next_=now+std::chrono::milliseconds(250);
    const auto player=*view.world.playerPosition;const auto origin=*scene.origin;const auto &map=*town.map();
    const Vec offset{float(origin.x),float(origin.y)},from{float(int(player.x)-origin.x)+.5f,float(int(player.y)-origin.y)+.5f};
    if(previous_!=player) {previous_=player;progressAt_=now;}
    const auto abandonSearchRoom=[&]() {
        if(!point_ || unit_ || landmark_ || (goal_!=Goal::Waypoint && goal_!=Goal::Exit)) return false;
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
        if(best) {unit_=best->unit;point_=best->position;segment_.reset();}
    }
    if(goal_==Goal::Exit && !unit_ && !interact_ && !landmark_) {
        // Walking connections have no clickable warp unit. Use only a real
        // shared edge of reconstructed rooms, stopping inside the current level.
        std::optional<OnlinePoint> candidate;int best=INT32_MAX;
        for(const auto &room:map.terrain.rooms) if(scene.area==room.level) for(const auto index:room.near) {
            if(index>=map.terrain.rooms.size()) continue;
            const auto &other=map.terrain.rooms[index];
            if(other.level==room.level || (destination_ && *destination_!=other.level)) continue;
            const int l=room.x*5,r=(room.x+room.width)*5,t=room.y*5,b=(room.y+room.height)*5;
            const int ol=other.x*5,orr=(other.x+other.width)*5,ot=other.y*5,ob=(other.y+other.height)*5;
            std::optional<Vec> edge;
            if(std::min(b,ob)>std::max(t,ot)) {
                const float y=float((std::min(b,ob)+std::max(t,ot))/2)+.5f;
                if(r==ol) edge=Vec{float(r-3)+.5f,y};
                else if(l==orr) edge=Vec{float(l+2)+.5f,y};
            }
            if(std::min(r,orr)>std::max(l,ol)) {
                const float x=float((std::min(r,orr)+std::max(l,ol))/2)+.5f;
                if(b==ot) edge=Vec{x,float(b-3)+.5f};
                else if(t==ob) edge=Vec{x,float(t+2)+.5f};
            }
            if(!edge) continue;
            const auto open=map.grid.nativeSpawn(*edge,4,playerMovement);if(!open) continue;
            const auto world=point(*open+offset);
            if(!inLevel(world) || distance(player,world)>=best || map.grid.path(from,*open,false,playerMovement,true).empty()) continue;
            candidate=world;best=distance(player,world);
        }
        if(candidate) {point_=candidate;landmark_=true;segment_.reset();}
    }
    if(unit_) {
        const auto target=std::find_if(scene.mapTargets.begin(),scene.mapTargets.end(),[&](const auto &entry){return entry.unit==*unit_;});
        if(target==scene.mapTargets.end()) {finish("failed","Target is no longer assigned in the client scene",control);return;}
        point_=target->position;
        if(interact_ && town.interactionReady(view,*unit_)) {
            if(state_=="interacting" && control.approaching()) return;
            if(interactions_ && now-progressAt_<std::chrono::seconds(1)) return;
            if(interactions_>=3) {finish("failed","Server did not open the requested interaction",control);return;}
            if(!control.interact(*unit_,run_)) {finish("failed",control.reason(),control);return;}
            ++interactions_;progressAt_=now;state_="interacting";
            if(goal_!=Goal::Waypoint) finish("completed","Interaction submitted; verify the server result separately",control);
            return;
        }
    }
    if(segment_) {
        if(distance(player,*segment_)>1 && control.movementGoal()) return;
        if(distance(player,*segment_)>1 && !control.reason().empty()) {
            if(abandonSearchRoom()) return;
            finish("failed",control.reason(),control);return;
        }
        segment_.reset();
    }
    if(!point_) {
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
        point_=candidate;++searches_;state_="searching";
    }
    const bool searching=!unit_ && !landmark_ && (goal_==Goal::Waypoint || goal_==Goal::Exit);
    if(distance(player,*point_)<=(unit_ || landmark_?4:1)) {
        if(searching) {visited_.emplace(point_->x,point_->y);point_.reset();return;}
        if(!interact_) {finish("completed","Reached target vicinity through client movement",control);return;}
    }
    const Vec target{float(int(point_->x)-origin.x)+.5f,float(int(point_->y)-origin.y)+.5f};
    const auto path=map.grid.path(from,target,unit_.has_value(),playerMovement,true);
    if(path.empty() || (!searching && (path.back()-target).length()>6)) {finish("failed","No client collision path to target",control);return;}
    // Look ahead along the planned route, including short corners. Clicking
    // each corner separately can stop inside the native arrival tolerance;
    // normal player pathfinding must traverse those corners towards its goal.
    Vec step=from;float remaining=18;
    for(const auto waypoint:path) {
        const auto delta=waypoint-step;const float span=delta.length();
        if(span>remaining) {step=step+delta*(remaining/span);break;}
        step=waypoint;remaining-=span;
        if(remaining<=0) break;
    }
    const auto next=point(step+offset);
    if(next==player) {finish("failed","Path produced no reachable movement segment",control);return;}
    if(!control.move(next,run_)) {finish("failed",control.reason(),control);return;}
    segment_=next;++moves_;if(!searching) state_="moving";
    } catch(const std::exception &error) {finish("failed",error.what(),control);}
}
}
