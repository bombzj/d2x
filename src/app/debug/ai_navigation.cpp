#include "ai_impl.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>

namespace d2x {
namespace {
OnlinePoint nativePoint(Vec p) {
    if(!std::isfinite(p.x) || !std::isfinite(p.y) || p.x<0 || p.y<0 || p.x>65535 || p.y>65535)
        throw std::runtime_error("AI navigation point outside native coordinates");
    return {uint16_t(std::floor(p.x)),uint16_t(std::floor(p.y))};
}
float distance(Vec a,Vec b) {return (a-b).length();}
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
    if(possibleWalkingPassage(town,a,b)) return true;
    const auto *map=town.map();const auto origin=town.read().origin;
    if(teleportRange<=0 || !a.loaded || !b.loaded || !map || !origin) return false;
    // A visibility-neighbor edge is a Teleport candidate only when actual
    // known standing cells on both sides fit one cast. Water/solid room
    // rectangles must not create an imaginary chain across a river.
    const auto standing=[&](const auto &room) {
        std::vector<Vec> result;
        for(const float y:{.25f,.5f,.75f}) for(const float x:{.25f,.5f,.75f}) {
            const Vec local{room.x+room.width*x-origin->x+.5f,room.y+room.height*y-origin->y+.5f};
            const auto open=map->grid.nativeSpawn(local,8,playerMovement);if(!open) continue;
            const Vec world=*open+Vec{float(origin->x),float(origin->y)}-Vec{.5f,.5f};
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
std::string DebugAi::Impl::walkState(const Context &c) const {return c.walk.snapshot().at("state").get<std::string>();}
void DebugAi::Impl::observeChild(Context &c,const Goal &goal) {
    if(!walkIntent.is_object() || walkIntent.value("goal",std::string{})!="unit" || !walkIntent.value("interact",false)) return;
    const auto child=c.walk.snapshot();
    if(child.value("state",std::string{})!="completed" || !child.value("interactionRequests",0u)) return;
    const OnlineUnitKey unit{uint8_t(walkIntent.at("unitType").get<int>()),walkIntent.at("unitId").get<uint32_t>()};
    const auto now=Clock::now();
    const bool door=walkIntent.value("purpose",std::string{})=="door" || std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &t){
        return t.unit==unit && t.interaction==OnlineMapInteraction::Door;
    });
    // Capture accepted interactions before combat/pause can discard a finished
    // child intent. The goal still waits for its separate real postcondition.
    if(door) {
        const auto found=c.session.read().world.units.find(unit);
        if(found!=c.session.read().world.units.end()) pendingDoor=std::pair{unit,found->second.assignmentRevision};
        doorDeadline=now+std::chrono::seconds(12);
        ++interactions;note("door-interaction-submitted",std::to_string(unit.id));cancelWalk(c);
    } else if(walkIntent.value("purpose",std::string{})=="activate-waypoint") {
        const auto found=c.session.read().world.units.find(unit);
        if(found!=c.session.read().world.units.end() && area)
            pendingWaypoint=WaypointActivation{unit,found->second.assignmentRevision,*area,walkIntent.value("confirmMenu",false)};
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
    } else if(walkIntent.value("purpose",std::string{})=="supply-return" && supplyStage==SupplyStage::Return) {
        supplyPortalUsed=true;supplyPortalDeadline=now+std::chrono::seconds(15);cancelWalk(c);
        note("supply-return-submitted",std::to_string(unit.id));
    } else if(supplyStage==SupplyStage::None && (walkIntent.value("purpose",std::string{})=="quest-object" ||
        walkIntent.value("purpose",std::string{})=="quest-portal" || walkIntent.value("purpose",std::string{})=="quest-orb" || walkIntent.value("purpose",std::string{})=="quest-forge")) {
        const auto intent=walkIntent;observeQuestInteraction(c,intent);
    } else if(supplyStage==SupplyStage::None && goal.kind==Kind::Acquire && goal.sourceObject && target==unit && !objectIssued) {
        objectIssued=true;setActionDeadline(std::chrono::seconds(12));++interactions;
        note("item-source-submitted",goal.item);cancelWalk(c);
    } else if(travelPhase==TravelPhase::WarpExit && legUnit==unit) {
        travelPhase=TravelPhase::CrossWait;setActionDeadline(std::chrono::seconds(15));++interactions;
        note("warp-interaction-submitted",std::to_string(unit.id));cancelWalk(c);
    } else if((goal.kind==Kind::ReturnTown || goal.kind==Kind::Restock || supplyStage==SupplyStage::ToTown) && portalIssued && !portalUsed) {
        portalUsed=true;setActionDeadline(std::chrono::seconds(15));++interactions;
        note("town-portal-interaction-submitted",std::to_string(unit.id));cancelWalk(c);
    }
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
        if(assigned!=c.town.read().mapTargets.end() && assigned->interaction!=OnlineMapInteraction::Door &&
            assigned->interaction!=OnlineMapInteraction::Npc &&
            !continuing && intent.value("interact",false) && distance(c.position,{float(assigned->position.x),float(assigned->position.y)})>8) {
            if(!approachPoint(c,assigned->position,"assigned interaction target",6)) explore(c,"reachable approach to assigned interaction target");
            return false;
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
        cancelWalk(c);walkIntent=intent;c.walk.command(intent,c.session,c.town,c.control,c.combat);++moveGoals;
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
        const auto detail=status.value("reason",std::string{});cancelWalk(c);
        throw std::runtime_error("AI navigation: "+detail);
    }
    return false;
}
bool DebugAi::Impl::walkPoint(Context &c,OnlinePoint point,bool) {
    if(c.walk.active() && walkIntent.is_object() && walkIntent.value("goal",std::string{})=="point") {
        const int x=walkIntent.value("x",0),y=walkIntent.value("y",0);
        if(std::abs(x-int(point.x))<=2 && std::abs(y-int(point.y))<=2) return false;
    }
    return walk(c,{{"goal","point"},{"x",point.x},{"y",point.y},{"interact",false}});
}
std::vector<int> DebugAi::Impl::route(const Context &c,int from,int to) const {
    auto graph=c.town.navigationConnections();
    // Special quest portals (notably Tristram's return red portal) are not
    // Levels.Vis links. Add only actually assigned outgoing connections.
    if(c.town.read().area) for(const auto &target:c.town.read().mapTargets)
        if(target.destination && (target.interaction==OnlineMapInteraction::Portal || target.interaction==OnlineMapInteraction::Exit) &&
            c.session.read().world.units.contains(target.unit)) graph[*c.town.read().area].push_back(*target.destination);
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
            const float candidate=cost+navigationCost(c,current,start,c.town.knownConnectionPosition(uint16_t(current),uint16_t(next)))+8;
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
    if(area==level && c.town.map() && c.town.read().origin) {
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
float DebugAi::Impl::routeCost(const Context &c,const std::vector<int> &path,std::optional<Vec> start) const {
    if(path.empty()) return std::numeric_limits<float>::infinity();
    if(!start && area==path.front()) start=c.position;
    float cost=0;
    for(size_t i=1;i<path.size();++i) {
        cost+=navigationCost(c,path[i-1],start,c.town.knownConnectionPosition(uint16_t(path[i-1]),uint16_t(path[i])))+8;
        const auto entry=c.town.knownConnectionPosition(uint16_t(path[i]),uint16_t(path[i-1]));
        start=entry?std::optional<Vec>{{float(entry->x),float(entry->y)}}:std::nullopt;
    }
    return cost;
}
bool DebugAi::Impl::openDoor(Context &c,bool frontier) {
    // Keep the accepted approach until its child finishes. Re-ranking nearby
    // doors from the moving position can alternate two doors every decision
    // and cancel both paths before either interaction reaches the server.
    if(c.walk.active() && walkIntent.is_object() && walkIntent.value("purpose",std::string{})=="door") {
        wait("open-door","Continuing the current normal door approach");return true;
    }
    std::optional<OnlineMapTarget> best;float nearest=std::numeric_limits<float>::max();
    const auto *map=c.town.map();const auto origin=c.town.read().origin;if(!map || !origin) return false;
    const Vec offset{float(origin->x),float(origin->y)};
    const auto grid=c.town.navigationGrid(c.session.read());
    for(const auto &entry:c.town.read().mapTargets) if(entry.interaction==OnlineMapInteraction::Door) {
        const auto unit=c.session.read().world.units.find(entry.unit);
        if(unit==c.session.read().world.units.end() || unit->second.mode!=0 || operatedDoors.contains({entry.unit,unit->second.assignmentRevision})) continue;
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
bool DebugAi::Impl::activateWaypoint(Context &c) {
    // The object's world animation is shared; only 63 says which waypoints
    // this character owns. A new real menu can also disprove an earlier
    // neutral-operation observation, without granting any local unlock.
    if(c.session.read().world.waypointSource && c.session.read().world.waypointHistory)
        for(const auto &entry:c.town.read().waypoints) if(!entry.unlocked) activatedWaypointAreas.erase(entry.level);
    if(c.town.read().town || c.session.read().world.npcRequested || c.session.read().world.waypointSource) return false;
    if(std::any_of(c.town.read().waypoints.begin(),c.town.read().waypoints.end(),[&](const auto &entry) {
        return entry.unlocked && entry.level==area;
    })) {
        if(walkIntent.is_object() && walkIntent.value("purpose",std::string{})=="activate-waypoint") cancelWalk(c);
        return false;
    }
    if(area && activatedWaypointAreas.contains(*area)) return false;
    if(c.walk.active() && walkIntent.is_object() && walkIntent.value("purpose",std::string{})=="activate-waypoint") {
        wait("activate-waypoint","Continuing the nearby waypoint's normal activation approach");return true;
    }
    const bool knownLocked=c.session.read().world.waypointHistory &&
        std::any_of(c.town.read().waypoints.begin(),c.town.read().waypoints.end(),[&](const auto &entry){return entry.level==area && !entry.unlocked;});
    // A teleport can pass a genuinely assigned waypoint without ever ending
    // inside16. Finish this observed opportunity within the normal pointer
    // range; already-unlocked areas were excluded above, never reactivated.
    const OnlineMapTarget *selected=nullptr;float nearest=50;
    for(const auto &entry:c.town.read().mapTargets) if(entry.interaction==OnlineMapInteraction::Waypoint) {
        const auto unit=c.session.read().world.units.find(entry.unit);
        if(unit==c.session.read().world.units.end() || !unit->second.mode || (*unit->second.mode!=0 && !knownLocked)) continue;
        const float span=distance(c.position,{float(entry.position.x),float(entry.position.y)});
        if(span<nearest) {nearest=span;selected=&entry;}
    }
    if(!selected) return false;
    const bool menu=c.session.read().world.units.at(selected->unit).mode!=0;
    wait("activate-waypoint","Activating the observed nearby waypoint through normal client interaction");
    const Json intent{{"goal","unit"},{"unitType",selected->unit.type},{"unitId",selected->unit.id},
        {"interact",true},{"purpose","activate-waypoint"},{"confirmMenu",menu}};
    if(walk(c,intent)) observeChild(c,goals[cursor]);
    return true;
}
void DebugAi::Impl::refreshNavigationAttempts(const Context &c) {
    const auto now=Clock::now();const auto revision=c.town.navigationRevision();
    std::erase_if(frontierAttempts,[&](const auto &entry) {
        const auto &attempt=entry.second;
        return attempt.geometryRevision!=revision || now>=attempt.retryAt || distance(c.position,attempt.position)>=8;
    });
    if(searchGeometryRevision!=revision || !searchPosition || distance(*searchPosition,c.position)>=8 || now>=searchRetry) {
        searchGeometryRevision=revision;searchPosition=c.position;unavailableRooms.clear();searchRetry=now+std::chrono::seconds(5);
    }
}
void DebugAi::Impl::recordFrontierAttempt(const Context &c,OnlinePoint point,std::string outcome,std::string detail) {
    refreshNavigationAttempts(c);
    // Attempts control short-term replanning only. Neither completion nor
    // failure writes discovery; the rendered viewport is its sole producer.
    if(frontierAttempts.size()>=256 && !frontierAttempts.contains({point.x,point.y}))
        frontierAttempts.erase(std::min_element(frontierAttempts.begin(),frontierAttempts.end(),[](const auto &a,const auto &b) {
            return a.second.retryAt<b.second.retryAt;
        }));
    const bool reached=distance(c.position,{float(point.x),float(point.y)})<=3;
    frontierAttempts[{point.x,point.y}]={c.position,c.town.navigationRevision(),Clock::now()+std::chrono::seconds(5),
        reached,std::move(outcome),std::move(detail)};
    note("navigation-frontier-attempt",std::to_string(point.x)+","+std::to_string(point.y)+(reached?" reached":" not reached"));
}
bool DebugAi::Impl::frontierCoolingDown(OnlinePoint point,float radius) const {
    return std::any_of(frontierAttempts.begin(),frontierAttempts.end(),[&](const auto &entry) {
        return Clock::now()<entry.second.retryAt && distance({float(point.x),float(point.y)},
            {float(entry.first.first),float(entry.first.second)})<radius;
    });
}
bool DebugAi::Impl::explore(Context &c,std::string purpose,bool recoveryOnly) {
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
    if(walkIntent.is_object() && walkIntent.value("explorationViewpoint",false)) {
        if(c.walk.active()) {wait("explore-staging","Completing the known intermediate approach to an observation viewpoint");return false;}
        const auto destination=walkIntent.at("observationTarget");
        const OnlinePoint viewpoint{destination.at(0).get<uint16_t>(),destination.at(1).get<uint16_t>()};
        const auto observedBefore=walkIntent.at("observedBefore").get<size_t>();
        const auto legs=walkIntent.value("viewpointLegs",0u);
        if(walkState(c)=="completed" || walkState(c)=="failed")
            recordFrontierAttempt(c,{walkIntent.at("x").get<uint16_t>(),walkIntent.at("y").get<uint16_t>()},
                walkState(c),c.walk.snapshot().value("reason",std::string{}));
        cancelWalk(c);approachHint.reset();approachFrontier.reset();
        const auto observed=c.town.navigationMemory().observedCells;
        // Retain the ultimate observation target across intermediate casts.
        // Otherwise ranking from each new origin can oscillate between banks.
        if(distance(c.position,{float(viewpoint.x),float(viewpoint.y)})>3 &&
            (observed>observedBefore || legs<8) && approachPoint(c,viewpoint,"reconstructed boundary viewpoint",3)) {
            walkIntent["explorationViewpoint"]=true;walkIntent["observationTarget"]=destination;
            walkIntent["observedBefore"]=observed>observedBefore?observed:observedBefore;
            walkIntent["viewpointLegs"]=observed>observedBefore?0:legs+1;return false;
        }
        if(observed<=observedBefore) {
            if(viewpointAttempts.size()>=256) viewpointAttempts.erase(viewpointAttempts.begin());
            viewpointAttempts[{viewpoint.x,viewpoint.y}]={observed,Clock::now()+std::chrono::seconds(30)};
            note("observation-viewpoint-deferred","No new observation while approaching "+std::to_string(viewpoint.x)+","+std::to_string(viewpoint.y));
        }
    }
    const auto observedCells=c.town.navigationMemory().observedCells;
    std::erase_if(viewpointAttempts,[&](const auto &entry){return entry.second.observedCells!=observedCells || Clock::now()>=entry.second.retryAt;});
    if(walkIntent.is_object() && walkIntent.contains("backtrackRoom")) {
        if(c.walk.active()) {wait("explore-backtrack","Returning through reconstructed rooms to another frontier");return false;}
        const auto completedIntent=walkIntent;const auto outcome=walkState(c);
        const auto detail=c.walk.snapshot().value("reason",std::string{});
        if(walkIntent.contains("backtrackRoom") && (walkState(c)=="completed" || walkState(c)=="failed")) {
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
                if(walkState(c)=="completed" && !entered && destination!=rooms.end()) {
                    const OnlinePoint center{uint16_t(destination->x+destination->width/2),uint16_t(destination->y+destination->height/2)};
                    cancelWalk(c);approachHint.reset();approachFrontier.reset();
                    if(approachPoint(c,center,"retained exploration junction",3)) {
                        if(walkIntent.is_object() && walkIntent.value("goal",std::string{})=="point") {
                            walkIntent["backtrackRoom"]=room;walkIntent["observedBefore"]=observed;
                        }
                        return false;
                    }
                }
                if(backtrackAttempts.size()>=256 && !backtrackAttempts.contains(key))
                    throw std::runtime_error("Backtrack attempt capacity exhausted; retaining exploration scene");
                // Only new coverage in this destination can invalidate its
                // no-discovery result. A few newly seen cells elsewhere must
                // not restart every old dead-end approach.
                backtrackAttempts[key]={destination==rooms.end()?0:destination->exploredTiles,
                    Clock::now()+(entered?std::chrono::seconds(120):std::chrono::seconds(30)),
                    entered?"arrived without new observation":"approach produced no further route"};
                note("exploration-backtrack-no-discovery",std::to_string(std::get<1>(key))+","+std::to_string(std::get<2>(key)));
            }
        }
        if(outcome=="completed" || outcome=="failed")
            recordFrontierAttempt(c,{completedIntent.at("x").get<uint16_t>(),completedIntent.at("y").get<uint16_t>()},outcome,detail);
        note("exploration-backtrack-finished",outcome);cancelWalk(c);
    }
    if(recoveryOnly) return false; // Return control to the original known goal after this recovery completes.
    const bool clearing=supplyStage==SupplyStage::None && cursor<goals.size() && goals[cursor].kind==Kind::Quest &&
        questRun.step<goals[cursor].steps.size() && goals[cursor].steps[questRun.step].action==QuestAction::Clear &&
        goals[cursor].steps[questRun.step].area==area;
    const bool precise=clearing;
    const float arrival=3.f;
    const auto observation=c.town.navigationObservation();
    std::vector<Vec> frontier;
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
            if(tile.removed || tile.type==13 || tile.type==15 || (tile.flags&8) ||
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
            if(observed(x-1,y) || observed(x+1,y) || observed(x,y-1) || observed(x,y+1)) cells.emplace(x,y);
        }
        for(const auto &[x,y]:cells) frontier.push_back({float((x+seen.tileX)*5)+2.5f,float((y+seen.tileY)*5)+2.5f});
    }
    const auto visibleGain=[&](Vec at) {
        size_t gain=0;
        if(observation) for(const auto cell:frontier) {
            const auto delta=cell-at;
            // Leave one tile of margin inside the actual projected viewport;
            // this is an estimate only. The real reveal callback owns seen.
            gain+=std::abs(delta.x-delta.y)<=std::max(0.f,observation->halfExtent.x-5) &&
                std::abs(delta.x+delta.y)<=std::max(0.f,observation->halfExtent.y-5);
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
        if(observed) visited.emplace(room.level,room.x,room.y);
        if(!room.loaded) continue;
        const Vec center{float(room.x)+float(room.width)/2,float(room.y)+float(room.height)/2};
        // Nearby reconstructed rooms are already observed. Do not visit every
        // wall/corner merely to stand on each room's exact center.
        const bool entered=c.position.x>=room.x && c.position.x<room.x+room.width &&
            c.position.y>=room.y && c.position.y<room.y+room.height;
        if(observed || (clearing && (entered || distance(c.position,center)<6.f))) visited.emplace(room.level,room.x,room.y);
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
    if(!clearing && observation) {
        searchDiagnostics["strategy"]="visible-boundary";searchDiagnostics["frontierTiles"]=frontier.size();
        searchDiagnostics["viewHalfExtent"]={observation->halfExtent.x,observation->halfExtent.y};
        struct Viewpoint {Vec local;size_t gain;float score;};
        bool selectedDirect=false;
        const bool canTeleport=teleportAvailable(c);
        std::vector<Viewpoint> viewpoints;
        // Sample standing cells, not room centers. A good viewpoint exposes a
        // long unseen boundary from inside its sight range without hugging it.
        for(int y=2;y<grid.height;y+=5) for(int x=2;x<grid.width;x+=5) {
            const auto standing=grid.nativeSpawn({float(x)+.5f,float(y)+.5f},2,playerMovement);
            if(!standing) continue;
            const Vec local=*standing,world=local+offset-Vec{.5f,.5f};
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
            viewpoints.push_back({local,gain,(span+8)/float(gain)});
        }
        std::stable_sort(viewpoints.begin(),viewpoints.end(),[](const auto &a,const auto &b){return a.score<b.score;});
        searchDiagnostics["candidateViewpoints"]=viewpoints.size();
        unsigned probes=0;
        std::vector<Vec> testedViewpoints;
        for(const auto &candidate:viewpoints) {
            if(std::any_of(testedViewpoints.begin(),testedViewpoints.end(),[&](Vec at){return distance(at,candidate.local)<8;})) continue;
            if(probes++>=24) break;
            testedViewpoints.push_back(candidate.local);
            const auto endpoint=nativePoint(candidate.local+offset);
            auto &trace=searchDiagnostics["candidates"].emplace_back(Json{{"endpoint",{endpoint.x,endpoint.y}},
                {"newBoundaryTiles",candidate.gain},{"score",candidate.score}});
            ++pathSearches;const auto path=c.walk.navigationSearch().path(grid,from,candidate.local,false,playerMovement,true);
            const auto trial=std::tuple{*area,endpoint.x,endpoint.y,endpoint.x,endpoint.y};
            const bool direct=canTeleport && distance(c.position,{float(endpoint.x),float(endpoint.y)})<=mobilityPolicy.distance &&
                (!mobilityTrials.contains(trial) || mobilityTrials.at(trial)<2);
            if(path.empty() && !direct) {trace["result"]="no-complete-known-route";continue;}
            float length=0;Vec previous=from;
            for(const auto node:path) {length+=distance(previous,node);previous=node;}
            if(path.empty()) length=distance(from,candidate.local);
            const float score=(length+8)/float(candidate.gain);
            trace["pathLength"]=length;trace["pathScore"]=score;trace["result"]=path.empty()?"known-floor-teleport":"candidate";
            if(score>=best) continue;
            for(const auto &room:rooms) if(room.level==*area && endpoint.x>=room.x && endpoint.x<room.x+room.width &&
                endpoint.y>=room.y && endpoint.y<room.y+room.height) {
                best=score;point=endpoint;chosen={room.level,room.x,room.y};selectedDirect=path.empty();break;
            }
        }
        if(point) {
            searchDiagnostics["selected"]={point->x,point->y};searchDiagnostics["result"]="observe-boundary";
            exploring=chosen;interest=point;wait("explore-boundary",std::move(purpose));
            walk(c,{{"goal","point"},{"x",point->x},{"y",point->y},{"interact",false},
                {"purpose","exploration-boundary"},{"directTeleport",selectedDirect}});
            return false;
        }
        if(canTeleport) {
            // A useful known viewpoint need not fit in one cast. Approach it
            // through currently reconstructed legal landing cells rather than
            // rejecting an otherwise traversable chain of Teleport inputs.
            unsigned staged=0;
            for(const auto &candidate:viewpoints) {
                if(staged++>=8) break;
                const auto viewpoint=nativePoint(candidate.local+offset-Vec{.5f,.5f});
                if(approachPoint(c,viewpoint,"reconstructed boundary viewpoint",3)) {
                    if(walkIntent.is_object() && walkIntent.value("goal",std::string{})=="point")
                        {walkIntent["explorationViewpoint"]=true;walkIntent["observationTarget"]={viewpoint.x,viewpoint.y};
                         walkIntent["observedBefore"]=observedCells;walkIntent["viewpointLegs"]=0;}
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
        ++pathSearches;const auto path=c.walk.navigationSearch().path(grid,from,goal,!room.loaded,playerMovement,true);
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
        std::queue<size_t> pending;
        if(root<rooms.size()) {previous[root]=root;pending.push(root);}
        while(!pending.empty()) {
            const auto index=pending.front();pending.pop();
            if(!rooms[index].loaded && !rooms[index].exploredTiles) continue; // Observe this frontier before extending through it.
            for(const auto next:connections[index]) if(next<rooms.size() && rooms[next].level==*area && previous[next]==rooms.size() &&
                possibleRoomPassage(c.town,rooms[index],rooms[next],teleportAvailable(c)?mobilityPolicy.distance:0,observedRoomPassages)) {
                previous[next]=index;depth[next]=depth[index]+1;pending.push(next);
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
            while(next!=root && !rooms[next].loaded) next=previous[next];
            const auto found=junctions.find(next);
            const bool unseen=destination.exploredTiles==0;
            if(found==junctions.end() || (unseen!=(rooms[found->second].exploredTiles==0)?unseen:depth[i]<depth[found->second])) junctions[next]=i;
        }
        std::optional<std::tuple<int,int,int>> backtrackRoom;
        bool backtrackTeleport=false,backtrackUnseen=false;const bool canTeleport=teleportAvailable(c);
        for(const auto &[next,destination]:junctions) {
            const auto &r=rooms[next];
            const auto &other=rooms[destination];
            const bool unseen=other.exploredTiles==0;
            if(point && backtrackUnseen && !unseen) continue;
            const Vec onward{float(other.x)+float(other.width)/2,float(other.y)+float(other.height)/2};
            // River banks and walls can cover a room center while a side is
            // open. Probe its quarters, including our own room when moving
            // toward its unrevealed neighbor needs a closer observation point.
            for(const float y:{.25f,.5f,.75f}) for(const float x:{.25f,.5f,.75f}) {
                const Vec sample{float(r.x)+float(r.width)*x,float(r.y)+float(r.height)*y};
                const auto open=grid.nativeSpawn(sample-offset+Vec{.5f,.5f},8,playerMovement);
                if(!open) continue;
                ++pathSearches;const auto path=c.walk.navigationSearch().path(grid,from,*open,false,playerMovement,true);
                if(path.empty() && !canTeleport) continue;
                const auto endpoint=nativePoint((path.empty()?*open:path.back())+offset);
                const Vec to{float(endpoint.x),float(endpoint.y)};
                if(to.x<r.x || to.y<r.y || to.x>=r.x+r.width || to.y>=r.y+r.height ||
                    distance(to,sample)>10 || distance(c.position,to)<=3 ||
                    (next==root && distance(to,onward)+2>=distance(c.position,onward))) continue;
                if(frontierCoolingDown(endpoint,3)) continue;
                const float score=float(depth[destination])*40+distance(to,onward)+.25f*distance(c.position,to)+(path.empty()?20.f:0.f);
                if((unseen && !backtrackUnseen) || score<best) {
                    best=score;point=endpoint;backtrackRoom={other.level,other.x,other.y};backtrackTeleport=path.empty();backtrackUnseen=unseen;
                    searchDiagnostics["backtrackDestination"]={{"room",{other.level,other.x,other.y}},
                        {"observedTiles",other.exploredTiles},{"roomSteps",depth[destination]},{"loaded",other.loaded},
                        {"viaRoom",{r.level,r.x,r.y,r.width,r.height}}};
                }
            }
        }
        if(point) {
            searchDiagnostics["selected"]={point->x,point->y};searchDiagnostics["result"]="backtrack";
            exploring.reset();interest.reset();
            note("exploration-backtrack",std::to_string(point->x)+","+std::to_string(point->y));
            wait("explore-backtrack","Returning through reconstructed rooms to another frontier");
            const auto &[level,x,y]=*backtrackRoom;
            walk(c,{{"goal","point"},{"x",point->x},{"y",point->y},{"interact",false},{"purpose","exploration-backtrack"},
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
    exploring=chosen;interest=point;wait("explore",std::move(purpose));walkPoint(c,*point);return false;
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
bool DebugAi::Impl::approachPoint(Context &c,OnlinePoint hint,std::string purpose,float vicinity) {
    refreshNavigationAttempts(c);
    const Vec wanted{float(hint.x),float(hint.y)};if(distance(c.position,wanted)<=vicinity) return false;
    if(approachDetour && *approachDetour!=hint) approachDetour.reset();
    if(walkIntent.is_object() && (walkIntent.contains("backtrackRoom") || walkIntent.value("explorationViewpoint",false))) {
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
            recordFrontierAttempt(c,*approachFrontier,status,c.walk.snapshot().value("reason",std::string{}));
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
    const auto path=c.walk.navigationSearch().path(grid,c.position-offset+Vec{.5f,.5f},goal,true,playerMovement,true);
    approachDiagnostics={{"decision",decisions},{"area",*area},{"purpose",purpose},
        {"from",{c.position.x,c.position.y}},{"target",{hint.x,hint.y}},{"result","no-useful-route"}};
    const auto endpoint=path.empty()?hint:nativePoint(path.back()+offset);
    const bool partial=path.empty() || distance({float(endpoint.x),float(endpoint.y)},wanted)>vicinity;
    if(!partial) approachDetour.reset();
    approachDiagnostics["endpoint"]={endpoint.x,endpoint.y};approachDiagnostics["partial"]=partial;
    if(partial && !approachDetour && teleportAvailable(c)) {
        // A genuinely observed target can be behind a wall. Teleport does
        // not need a walking connection; only use known standing cells and
        // strictly approach that same target, never a guessed room center.
        std::optional<OnlinePoint> landing;float best=distance(c.position,wanted)-3;
        const int radius=int(std::ceil(mobilityPolicy.distance));
        for(int y=std::max(0,int(c.position.y)-radius);y<=std::min(65535,int(c.position.y)+radius);++y)
            for(int x=std::max(0,int(c.position.x)-radius);x<=std::min(65535,int(c.position.x)+radius);++x) {
                const Vec at{float(x),float(y)},local=at-offset+Vec{.5f,.5f};
                const float span=distance(c.position,at),remaining=distance(at,wanted);
                if(span<6 || span>mobilityPolicy.distance || remaining>=best || frontierCoolingDown({uint16_t(x),uint16_t(y)},3) ||
                    !grid.nativeMovementSegment(local,local,playerMovement)) continue;
                const auto trial=std::tuple{*area,uint16_t(x),uint16_t(y),uint16_t(x),uint16_t(y)};
                if(mobilityTrials.contains(trial) && mobilityTrials.at(trial)>=2) continue;
                best=remaining;landing=OnlinePoint{uint16_t(x),uint16_t(y)};
            }
        if(landing) {
            exploring.reset();interest.reset();
            wait("known-target-teleport","Approaching the observed target across known terrain with normal Teleport");
            walk(c,{{"goal","point"},{"x",landing->x},{"y",landing->y},{"interact",false},{"purpose","known-target-teleport"}});
            approachHint=hint;approachFrontier=landing;
            approachDiagnostics["endpoint"]={landing->x,landing->y};approachDiagnostics["result"]="known-landing-across-wall";
            return true;
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
                possibleRoomPassage(c.town,rooms[i],rooms[next],teleportAvailable(c)?mobilityPolicy.distance:0,observedRoomPassages)) {
                previous[next]=i;pending.push(next);
            }
        }
        if(previous[destination]==rooms.size()) return false;
        std::vector<size_t> chain;
        for(size_t i=destination;;i=previous[i]) {chain.push_back(i);if(i==root) break;}
        std::reverse(chain.begin(),chain.end());
        size_t last=0;while(last+1<chain.size() && rooms[chain[last+1]].loaded) ++last;
        for(size_t n=last+1;n>0;--n) {
            const size_t i=n-1;const auto &r=rooms[chain[i]];
            const auto &next=rooms[chain[std::min(i+1,chain.size()-1)]];
            const Vec onward=i+1<chain.size()?Vec{float(next.x)+next.width*.5f,float(next.y)+next.height*.5f}:wanted;
            std::optional<OnlinePoint> selected;float best=std::numeric_limits<float>::max();
            for(const float y:{.25f,.5f,.75f}) for(const float x:{.25f,.5f,.75f}) {
                const Vec sample{float(r.x)+r.width*x,float(r.y)+r.height*y};
                const auto open=grid.nativeSpawn(sample-offset+Vec{.5f,.5f},8,playerMovement);if(!open) continue;
                const auto point=nativePoint(*open+offset);const Vec at{float(point.x),float(point.y)};
                if(at.x<r.x || at.y<r.y || at.x>=r.x+r.width || at.y>=r.y+r.height ||
                    distance(c.position,at)<=3 || (i==0 && distance(at,onward)+2>=distance(c.position,onward)) ||
                    frontierCoolingDown(point,3)) continue;
                ++pathSearches;if(!c.walk.navigationSearch().reachable(grid,c.position-offset+Vec{.5f,.5f},*open,playerMovement,true)) continue;
                const float score=distance(at,onward)+.25f*distance(c.position,at);
                if(score<best) {best=score;selected=point;}
            }
            if(!selected) continue;
            exploring.reset();interest.reset();
            wait("known-target-backtrack","Returning through observed room connections to "+purpose);
            walk(c,{{"goal","point"},{"x",selected->x},{"y",selected->y},{"interact",false},{"purpose","known-target-backtrack"}});
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
    if(path.empty() || distance(c.position,path.back()+offset)<std::min(4.f,vicinity)) return backtrack();
    if(partial && !debugUnknownFrontier(grid,path.back())) {
        approachDiagnostics["result"]="known-wall-without-unknown-frontier";return backtrack();
    }
    if(frontierCoolingDown(endpoint,partial?8.f:3.f)) {
        approachDiagnostics["result"]="frontier-retry-cooldown";return backtrack();
    }
    if(walkIntent.is_object() && walkState(c)=="failed") {cancelWalk(c);return false;}
    wait("known-target-approach","Approaching the last client-observed position: "+purpose);
    // Keep a submitted approach even when its endpoint reaches the target.
    // Assignment can appear/disappear while moving between active rooms;
    // recomputing that path every tick must not alternate it with exploration.
    exploring.reset();interest.reset();
    walkPoint(c,endpoint);
    approachHint=hint;approachFrontier=endpoint;
    approachDiagnostics["result"]="approach";
    return true;
}
bool DebugAi::Impl::travel(Context &c,uint16_t destination,bool allowWaypoint) {
    if(!area) return false;
    if(*area==destination) {
        cancelWalk(c);travelDestination.reset();travelPhase=TravelPhase::Route;legDestination.reset();return true;
    }
    if(travelDestination!=destination) {
        cancelWalk(c);travelDestination=destination;travelPhase=TravelPhase::Route;legDestination.reset();
        waypointDestination.reset();legUnit.reset();
    }
    if(legDestination && *area==*legDestination) {
        cancelWalk(c);travelPhase=TravelPhase::Route;legDestination.reset();waypointDestination.reset();legUnit.reset();
    }
    const auto now=Clock::now();
    const auto waypointPosition=[&]() -> std::optional<OnlinePoint> {
        if(const auto known=c.town.knownWaypointPosition(*area)) return known;
        for(const auto &entry:c.town.read().mapTargets) if(entry.interaction==OnlineMapInteraction::Waypoint &&
            c.session.read().world.units.contains(entry.unit)) return entry.position;
        return {};
    };
    const auto chooseWaypoint=[&]() -> std::optional<uint16_t> {
        const auto source=waypointPosition();if(!source) return {};
        const auto current=route(c,*area,destination);
        float shortest=routeCost(c,current);std::optional<uint16_t> best;
        const float approach=navigationCost(c,*area,c.position,source);
        travelDiagnostics={{"from",*area},{"destination",destination},{"ordinaryRoute",current},
            {"ordinaryCost",std::isfinite(shortest)?Json(shortest):Json(nullptr)},
            {"waypointApproachCost",approach},{"waypoints",Json::array()},{"selectedWaypoint",nullptr}};
        for(const auto &entry:c.town.read().waypoints) if(entry.unlocked && entry.level!=*area) {
            const auto rest=route(c,entry.level,destination);
            const auto landing=c.town.knownWaypointPosition(entry.level);
            const auto start=landing?std::optional<Vec>{{float(landing->x),float(landing->y)}}:std::nullopt;
            const float cost=approach+16+routeCost(c,rest,start);
            travelDiagnostics["waypoints"].push_back({{"level",entry.level},{"route",rest},
                {"cost",std::isfinite(cost)?Json(cost):Json(nullptr)}});
            if(cost<shortest) {shortest=cost;best=entry.level;}
        }
        if(best) travelDiagnostics["selectedWaypoint"]=*best;
        return best;
    };
    if(travelPhase==TravelPhase::WaypointWait || travelPhase==TravelPhase::CrossWait) {
        if(travelPhase==TravelPhase::CrossWait && legUnit && legUnit->type==5 && legDestination && !warpRetryUsed && !c.moving &&
            now+std::chrono::seconds(12)>=actionDeadline && c.town.interactionReady(c.session.read(),*legUnit) &&
            std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &entry) {
                return entry.unit==legUnit && entry.destination==legDestination;
            })) {
            // A tile approach can settle after the original 13 without a
            // warp. With the same live tile and a confirmed in-range server
            // position, allow one normal new click; never replay item/NPC
            // services or infer a destination before its real scene arrives.
            if(c.control.interact(*legUnit,true,{},c.position)) {
                warpRetryUsed=true;++interactions;setActionDeadline(std::chrono::seconds(15));
                note("tile-interaction-confirmed-position-retry",std::to_string(legUnit->id));return false;
            }
        }
        if(travelPhase==TravelPhase::CrossWait && !legUnit && !c.moving &&
            now+std::chrono::seconds(12)>=actionDeadline) {
            // A walking pointer can stop at a newly occupied seam. It has no
            // irreversible service result to replay: choose a fresh ordinary
            // approach from current client collision instead of waiting forever.
            cancelWalk(c);travelPhase=TravelPhase::Route;legDestination.reset();exitRetryAt=now+std::chrono::seconds(1);
            note("walking-crossing-stopped","Replanning the ordinary boundary approach from observed occupancy");return false;
        }
        wait("travel-wait","Waiting for the destination's native scene assignment");
        if(now>=actionDeadline) throw std::runtime_error("Travel was submitted but the destination was not observed");
        return false;
    }
    if(travelPhase==TravelPhase::WaypointOpen) {
        const bool open=c.session.read().world.waypointSource && c.session.read().world.waypointHistory &&
            std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &entry) {
                return entry.interaction==OnlineMapInteraction::Waypoint && entry.unit.type==2 &&
                    entry.unit.id==*c.session.read().world.waypointSource && c.session.read().world.units.contains(entry.unit);
            });
        if(!open) {
            const bool assigned=std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[](const auto &t){return t.interaction==OnlineMapInteraction::Waypoint;});
            if(!assigned) if(const auto hint=c.town.knownWaypointPosition(*area);hint && approachPoint(c,*hint,"known waypoint")) return false;
            wait("waypoint","Opening the actual assigned waypoint");
            if(!walk(c,{{"goal","waypoint"}})) return false;
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
            note("waypoint-request-submitted",std::to_string(entry->level));
        }
        return false;
    }
    if(travelPhase==TravelPhase::WalkExit || travelPhase==TravelPhase::WarpExit) {
        const bool warp=travelPhase==TravelPhase::WarpExit;
        if(warp && legUnit && std::none_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &t){return t.unit==legUnit;})) {
            if(c.walk.active() && walkIntent.is_object() && walkIntent.value("goal",std::string{})=="point") {
                wait("known-target-approach","Continuing the observed exit's position approach while its assignment is outside the client scene");
                return false;
            }
            cancelWalk(c);travelPhase=TravelPhase::Route;legUnit.reset();note("exit-assignment-withdrawn");return false;
        }
        if(warp && legUnit) {
            const auto entry=std::find_if(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &t){return t.unit==legUnit;});
            const Vec wanted{float(entry->position.x),float(entry->position.y)};
            // Warp units can be assigned before every intervening room is
            // materialized. Approach through known terrain before asking the
            // exact interaction child to reach the actual warp vicinity.
            const bool continuing=c.walk.active() && walkIntent.is_object() && walkIntent.value("goal",std::string{})=="unit" &&
                walkIntent.value("unitType",0)==legUnit->type && walkIntent.value("unitId",0u)==legUnit->id;
            if(distance(c.position,wanted)>8 && !continuing) {
                if(!approachPoint(c,entry->position,"assigned level connection",6))
                    explore(c,"reachable approach to the assigned level connection");
                return false;
            }
        }
        if(!warp && walkIntent.is_object() && walkIntent.value("goal",std::string{})=="exit" && walkState(c)=="failed") {
            note("walking-connection-not-yet-reachable",c.walk.snapshot().value("reason",std::string{}));
            exitRetryAt=now+std::chrono::seconds(5);
            cancelWalk(c);travelPhase=TravelPhase::Route;
            explore(c,"unobserved boundary toward level "+std::to_string(*legDestination));return false;
        }
        wait("travel-exit","Approaching the connection to "+std::to_string(*legDestination));
        Json intent=warp?Json{{"goal","unit"},{"unitType",legUnit->type},{"unitId",legUnit->id},{"interact",true}}:
            Json{{"goal","exit"},{"level",*legDestination},{"interact",false}};
        if(!walk(c,intent)) return false;
        const auto observed=c.walk.snapshot();cancelWalk(c);
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
                    note("walking-crossing-occupied","Selecting another opening after new client unit/terrain updates");return false;
                }
            }
            // The ordinary move is still subject to client-map and server gates.
            if(!c.control.move({uint16_t(x),uint16_t(y)},true,{},c.position))
                throw std::runtime_error("Exit crossing: "+c.control.reason());
        }
        ++interactions;travelPhase=TravelPhase::CrossWait;setActionDeadline(std::chrono::seconds(15));return false;
    }
    const auto path=route(c,*area,destination);
    const float ordinaryCost=routeCost(c,path);
    travelDiagnostics={{"from",*area},{"destination",destination},{"ordinaryRoute",path},
        {"ordinaryCost",std::isfinite(ordinaryCost)?Json(ordinaryCost):Json(nullptr)},{"selectedWaypoint",nullptr}};
    if(allowWaypoint) {
        const bool source=waypointPosition().has_value();
        // 63 reports unlocked destinations only after an actual menu is opened.
        // Being in town does not disclose its waypoint's location. Do not
        // replace the requested exit route with an unrelated room search.
        const bool nearby=std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &v) {
            return v.interaction==OnlineMapInteraction::Waypoint &&
                distance(c.position,{float(v.position.x),float(v.position.y)})<=8;
        });
        bool mayShorten=false;
        if(source && !c.session.read().world.waypointHistory) {
            const float approach=navigationCost(c,*area,c.position,waypointPosition());
            for(size_t row=0;row<definitions->levels.rows().size() && !mayShorten;++row) {
                const auto number=definitions->levels.number(row,"Waypoint"),level=definitions->levels.number(row,"Id");
                if(!number || *number<0 || *number>=112 || !level || *level<=0 || *level==*area) continue;
                const auto rest=route(c,*level,destination);
                mayShorten=approach+16+routeCost(c,rest)<ordinaryCost;
            }
        }
        if((nearby || source) && mayShorten && !c.session.read().world.waypointHistory) {travelPhase=TravelPhase::WaypointOpen;return false;}
        if(source) waypointDestination=chooseWaypoint();
        if(waypointDestination) {travelPhase=TravelPhase::WaypointOpen;return false;}
    }
    if(path.empty()) {
        bool localWaypoint=false;
        for(size_t row=0;row<definitions->levels.rows().size();++row) if(definitions->levels.number(row,"Id")==area) {
            const auto number=definitions->levels.number(row,"Waypoint");
            localWaypoint=number && *number>=0 && *number<112;break;
        }
        bool possible=!c.session.read().world.waypointHistory;
        if(!possible) for(const auto &entry:c.town.read().waypoints) if(entry.unlocked && entry.level!=area &&
            !route(c,entry.level,destination).empty()) {possible=true;break;}
        if(allowWaypoint && localWaypoint && possible && !waypointPosition()) {
            // An unresolved ordinary graph must not force the caller to give
            // us the waypoint's coordinates. Only this fallback may search for
            // an unknown source; its actual assignment and 63 remain required.
            travelDiagnostics["result"]="observe-waypoint-source-without-ordinary-route";
            explore(c,"an actual current-level waypoint / travel connection");return false;
        }
        throw std::runtime_error("No observed portal, resolved level route or available unlocked waypoint route; AI does not invent a transfer or unlock");
    }
    if(openDoor(c)) return false;
    legDestination=uint16_t(path.at(1));
    for(const auto &entry:c.town.read().mapTargets) if(entry.destination==legDestination &&
        (entry.interaction==OnlineMapInteraction::Exit || entry.interaction==OnlineMapInteraction::Portal)) {
        legUnit=entry.unit;warpRetryUsed=false;travelPhase=TravelPhase::WarpExit;return false;
    }
    if(approachKnown(c,Selector{5,*legDestination,"known connection to "+std::to_string(*legDestination)})) return false;
    const auto edges=c.town.walkingBoundaries();
    // A normal Teleport can cross an observed outdoor wall even when no
    // walking opening has been reconstructed. Only active native neighbors
    // on this resolved walking connection supply candidate floor; no unseen
    // rooms, portal prerequisites, or town Teleport restrictions are bypassed.
    bool targetTeleport=false;
    for(size_t row=0;row<definitions->levels.rows().size();++row)
        if(definitions->levels.number(row,"Id")==legDestination)
            targetTeleport=definitions->levels.number(row,"Teleport").value_or(0)!=0;
    if(targetTeleport && teleportAvailable(c) && std::any_of(edges.begin(),edges.end(),[&](const auto &edge){return edge.destination==legDestination;})) {
        const auto &rooms=c.town.navigationRooms();const auto *map=c.town.map();const auto origin=c.town.read().origin;
        const auto inside=[](const auto &room,Vec at) {return at.x>=room.x && at.y>=room.y && at.x<room.x+room.width && at.y<room.y+room.height;};
        const auto current=std::find_if(rooms.begin(),rooms.end(),[&](const auto &room){return room.loaded && room.level==area && inside(room,c.position);});
        std::optional<OnlinePoint> landing;float best=std::numeric_limits<float>::max();
        if(current!=rooms.end() && map && origin) for(const auto next:current->near) {
            if(next>=rooms.size()) continue;
            const auto &room=rooms[next];if(!room.loaded || room.level!=legDestination) continue;
            for(const float y:{.25f,.5f,.75f}) for(const float x:{.25f,.5f,.75f}) {
                const Vec sample{room.x+room.width*x-origin->x+.5f,room.y+room.height*y-origin->y+.5f};
                const auto open=map->grid.nativeSpawn(sample,8,playerMovement);if(!open) continue;
                const auto point=nativePoint(*open+Vec{float(origin->x),float(origin->y)});const Vec at{float(point.x),float(point.y)};
                const float span=distance(at,c.position);
                const auto trial=std::tuple{area.value_or(0),point.x,point.y,point.x,point.y};
                if(!inside(room,at) || span<2 || span>mobilityPolicy.distance ||
                    (mobilityTrials.contains(trial) && mobilityTrials.at(trial)>=2)) continue;
                if(span<best) {best=span;landing=point;}
            }
        }
        if(landing) {
            travelDiagnostics["result"]="observed-neighbor-teleport";
            travelDiagnostics["landing"]={landing->x,landing->y};
            wait("travel-teleport","Crossing to currently reconstructed adjacent-level floor with normal Teleport");
            walk(c,{{"goal","point"},{"x",landing->x},{"y",landing->y},{"interact",false},{"purpose","known-target-teleport"}});
            return false;
        }
    }
    if(exploring && c.walk.active()) {explore(c,"connection to level "+std::to_string(*legDestination));return false;}
    if(now>=exitRetryAt && std::any_of(edges.begin(),edges.end(),[&](const auto &v){return v.destination==legDestination;})) {
        travelPhase=TravelPhase::WalkExit;return false;
    }
    explore(c,"connection to level "+std::to_string(*legDestination));return false;
}
}
