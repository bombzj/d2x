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
    } else if(walkIntent.value("purpose",std::string{})=="supply-return" && supplyStage==SupplyStage::Return) {
        supplyPortalUsed=true;supplyPortalDeadline=now+std::chrono::seconds(15);cancelWalk(c);
        note("supply-return-submitted",std::to_string(unit.id));
    } else if(supplyStage==SupplyStage::None && (walkIntent.value("purpose",std::string{})=="quest-object" ||
        walkIntent.value("purpose",std::string{})=="quest-portal")) {
        const auto intent=walkIntent;observeQuestInteraction(c,intent);
    } else if(supplyStage==SupplyStage::None && goal.kind==Kind::Acquire && goal.sourceObject && target==unit && !objectIssued) {
        objectIssued=true;actionDeadline=now+std::chrono::seconds(12);++interactions;
        note("item-source-submitted",goal.item);cancelWalk(c);
    } else if(travelPhase==TravelPhase::WarpExit && legUnit==unit) {
        travelPhase=TravelPhase::CrossWait;actionDeadline=now+std::chrono::seconds(15);++interactions;
        note("warp-interaction-submitted",std::to_string(unit.id));cancelWalk(c);
    } else if((goal.kind==Kind::ReturnTown || goal.kind==Kind::Restock || supplyStage==SupplyStage::ToTown) && portalIssued && !portalUsed) {
        portalUsed=true;actionDeadline=now+std::chrono::seconds(15);++interactions;
        note("town-portal-interaction-submitted",std::to_string(unit.id));cancelWalk(c);
    }
}
bool DebugAi::Impl::walk(Context &c,Json intent) {
    bool nearbyInteraction=false;
    if(intent.value("goal",std::string{})=="unit") {
        const OnlineUnitKey key{intent.at("unitType").get<uint8_t>(),intent.at("unitId").get<uint32_t>()};
        for(const auto &entry:c.town.read().mapTargets) if(entry.unit==key)
            nearbyInteraction=intent.value("interact",false) &&
                distance(c.position,{float(entry.position.x),float(entry.position.y)})<=6;
        const auto assigned=std::find_if(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &entry){return entry.unit==key;});
        if(assigned!=c.town.read().mapTargets.end() && assigned->interaction!=OnlineMapInteraction::Door &&
            assigned->interaction!=OnlineMapInteraction::Npc &&
            intent.value("interact",false) && distance(c.position,{float(assigned->position.x),float(assigned->position.y)})>8) {
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
    intent["command"]="debug-walk";intent["timeoutSeconds"]=120;
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
    std::map<int,int> previous{{from,from}};
    std::queue<int> queue;queue.push(from);
    while(!queue.empty() && !previous.contains(to)) {
        const auto current=queue.front();queue.pop();const auto edges=graph.find(current);if(edges==graph.end()) continue;
        for(int next:edges->second) if(!previous.contains(next)) {previous[next]=current;queue.push(next);}
    }
    if(!previous.contains(to)) return {};
    std::vector<int> result;for(int p=to;;p=previous.at(p)) {result.push_back(p);if(p==from) break;}
    std::reverse(result.begin(),result.end());return result;
}
bool DebugAi::Impl::openDoor(Context &c) {
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
        if(d>=nearest) continue;
        if(c.town.interactionReady(c.session.read(),entry.unit,c.position)) {
            nearest=d;best=entry;continue;
        }
        const Vec wanted{float(entry.position.x),float(entry.position.y)};
        const auto path=grid.path(c.position-offset+Vec{.5f,.5f},wanted-offset+Vec{.5f,.5f},true,playerMovement,true);
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
            for(const auto at:approaches) if(!grid.path(c.position-offset+Vec{.5f,.5f},at,false,playerMovement,true).empty()) {
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
bool DebugAi::Impl::explore(Context &c,std::string purpose) {
    const auto *map=c.town.map();const auto origin=c.town.read().origin;if(!map || !origin || !area) return false;
    const Vec offset{float(origin->x),float(origin->y)},from=c.position-offset+Vec{.5f,.5f};
    const auto grid=c.town.navigationGrid(c.session.read());
    const auto &rooms=c.town.navigationRooms();mapCacheBuilds=c.town.navigationCacheBuilds();
    const auto revision=c.town.navigationRevision();
    // Partial native paths depend on the current starting point. A failed
    // frontier is not proof that its room stays unreachable after movement
    // or after a dynamic obstruction clears, even with unchanged terrain.
    if(searchGeometryRevision!=revision || !searchPosition || distance(*searchPosition,c.position)>=8 || Clock::now()>=searchRetry) {
        searchGeometryRevision=revision;searchPosition=c.position;unavailableRooms.clear();searchRetry=Clock::now()+std::chrono::seconds(5);
    }
    knownExploredRooms=size_t(std::count_if(rooms.begin(),rooms.end(),[](const auto &room){return room.exploredTiles!=0;}));
    if(walkIntent.is_object() && walkIntent.value("purpose",std::string{})=="exploration-backtrack") {
        if(c.walk.active()) {wait("explore-backtrack","Returning through reconstructed rooms to another frontier");return false;}
        if(walkState(c)=="completed")
            exploredFrontiers.emplace(walkIntent.at("x").get<uint16_t>(),walkIntent.at("y").get<uint16_t>());
        note("exploration-backtrack-finished",walkState(c));cancelWalk(c);
    }
    const bool clearing=supplyStage==SupplyStage::None && cursor<goals.size() && goals[cursor].kind==Kind::Quest &&
        questRun.step<goals[cursor].steps.size() && goals[cursor].steps[questRun.step].action==QuestAction::Clear &&
        goals[cursor].steps[questRun.step].area==area;
    bool precise=clearing;
    for(size_t row=0;row<definitions->levels.rows().size();++row) if(definitions->levels.number(row,"Id")==area) {
        precise=precise || definitions->levels.number(row,"IsInside").value_or(0)!=0;break;
    }
    const float arrival=precise?3.f:16.f;
    for(const auto &room:rooms) if(room.level==*area && room.loaded) {
        const Vec center{float(room.x)+float(room.width)/2,float(room.y)+float(room.height)/2};
        // Nearby reconstructed rooms are already observed. Do not visit every
        // wall/corner merely to stand on each room's exact center.
        const bool entered=c.position.x>=room.x && c.position.x<room.x+room.width &&
            c.position.y>=room.y && c.position.y<room.y+room.height;
        if(entered || distance(c.position,center)<(precise?6.f:30.f)) visited.emplace(room.level,room.x,room.y);
    }
    const bool ownApproach=exploring && interest && walkIntent.is_object() && walkIntent.value("goal",std::string{})=="point" &&
        walkIntent.value("x",0)==interest->x && walkIntent.value("y",0)==interest->y;
    if(exploring && interest && (visited.contains(*exploring) || distance(c.position,{float(interest->x),float(interest->y)})<=arrival)) {
        // Exploration needs an observation position, not an exact arrival at
        // a clipped wall/frontier pixel. Interaction goals keep their precision.
        exploredFrontiers.emplace(interest->x,interest->y);
        note("exploration-vicinity-observed",std::to_string(interest->x)+","+std::to_string(interest->y));
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
            // Reaching a partial path's frontier does not mean reaching its
            // unknown room. Remember the actual observed approach so a blocked
            // outer wall cannot keep winning the nearest-room score forever.
            exploredFrontiers.emplace(interest->x,interest->y);
            note("exploration-frontier-observed",std::to_string(interest->x)+","+std::to_string(interest->y));
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
    unsigned probes=0;
    for(const auto *candidate:candidates) {
        if(probes++>=8) break;
        const auto &room=*candidate;const auto key=std::tuple{room.level,room.x,room.y};
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
                else {if(complete) visited.insert(key);unavailableRooms.insert(key);continue;}
            }
        }
        ++pathSearches;const auto path=grid.path(from,goal,true,playerMovement,true);
        if(path.empty()) {unavailableRooms.insert(key);continue;}
        const auto endpoint=nativePoint(path.back()+offset);
        const Vec to{float(endpoint.x),float(endpoint.y)};const auto progress=distance(c.position,to);
        if(progress<=arrival || std::any_of(exploredFrontiers.begin(),exploredFrontiers.end(),[&](auto p){
            return distance(to,{float(p.first),float(p.second)})<(precise?3.f:12.f);
        })) {unavailableRooms.insert(key);continue;}
        const float score=progress+.25f*distance(to,center)+(room.exploredTiles?0.f:8.f);
        if(score<best) {best=score;chosen={room.level,room.x,room.y};point=endpoint;}
    }
    if(!point) {
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
        std::queue<size_t> pending;
        if(root<rooms.size()) {previous[root]=root;pending.push(root);}
        while(!pending.empty()) {
            const auto index=pending.front();pending.pop();
            for(const auto next:rooms[index].near) if(next<rooms.size() && rooms[next].level==*area && previous[next]==rooms.size()) {
                previous[next]=index;depth[next]=depth[index]+1;pending.push(next);
            }
        }
        std::map<size_t,size_t> junctions;
        for(size_t i=0;i<rooms.size();++i) {
            const auto &destination=rooms[i];
            if(i==root || previous[i]==rooms.size() || visited.contains({destination.level,destination.x,destination.y})) continue;
            size_t next=i;
            while(next!=root && !rooms[next].loaded) next=previous[next];
            const auto found=junctions.find(next);
            if(found==junctions.end() || depth[i]<depth[found->second]) junctions[next]=i;
        }
        for(const auto &[next,destination]:junctions) {
            const auto &r=rooms[next];
            const auto &other=rooms[destination];
            const Vec onward{float(other.x)+float(other.width)/2,float(other.y)+float(other.height)/2};
            // River banks and walls can cover a room center while a side is
            // open. Probe its quarters, including our own room when moving
            // toward its unrevealed neighbor needs a closer observation point.
            for(const float y:{.25f,.5f,.75f}) for(const float x:{.25f,.5f,.75f}) {
                const Vec sample{float(r.x)+float(r.width)*x,float(r.y)+float(r.height)*y};
                const auto open=grid.nativeSpawn(sample-offset+Vec{.5f,.5f},8,playerMovement);
                if(!open) continue;
                ++pathSearches;const auto path=grid.path(from,*open,true,playerMovement,true);
                if(path.empty()) continue;
                const auto endpoint=nativePoint(path.back()+offset);
                const Vec to{float(endpoint.x),float(endpoint.y)};
                if(distance(to,sample)>10 || distance(c.position,to)<=3 ||
                    (next==root && distance(to,onward)+2>=distance(c.position,onward))) continue;
                if(std::any_of(exploredFrontiers.begin(),exploredFrontiers.end(),[&](auto p) {
                    return distance(to,{float(p.first),float(p.second)})<3;
                })) continue;
                const float score=float(depth[destination])*40+distance(to,onward)+.25f*distance(c.position,to);
                if(score<best) {best=score;point=endpoint;}
            }
        }
        if(point) {
            exploring.reset();interest.reset();
            note("exploration-backtrack",std::to_string(point->x)+","+std::to_string(point->y));
            wait("explore-backtrack","Returning through reconstructed rooms to another frontier");
            walk(c,{{"goal","point"},{"x",point->x},{"y",point->y},{"interact",false},{"purpose","exploration-backtrack"}});
            return false;
        }
        wait("explore-wait","No reachable unvisited reconstructed room for "+purpose+"; awaiting normal room updates");return false;
    }
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
    const Vec wanted{float(hint.x),float(hint.y)};if(distance(c.position,wanted)<=vicinity) return false;
    if(approachHint && *approachHint==hint && approachFrontier && walkIntent.is_object() &&
        walkIntent.value("goal",std::string{})=="point" && walkIntent.value("x",0)==approachFrontier->x &&
        walkIntent.value("y",0)==approachFrontier->y) {
        if(c.walk.active()) {wait("known-target-approach","Completing the current partial approach to "+purpose);return true;}
        const auto status=walkState(c);
        if(status=="completed" || status=="failed") {
            exploredFrontiers.emplace(approachFrontier->x,approachFrontier->y);
            note("known-target-frontier-observed",purpose);cancelWalk(c);approachHint.reset();approachFrontier.reset();return false;
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
    const auto grid=c.town.navigationGrid(c.session.read());
    const auto path=grid.path(c.position-offset+Vec{.5f,.5f},goal,true,playerMovement,true);
    if(path.empty() || distance(c.position,path.back()+offset)<std::min(4.f,vicinity)) return false;
    const auto endpoint=nativePoint(path.back()+offset);
    const bool partial=distance({float(endpoint.x),float(endpoint.y)},wanted)>vicinity;
    if(partial && std::any_of(exploredFrontiers.begin(),exploredFrontiers.end(),[&](auto point) {
        return distance({float(endpoint.x),float(endpoint.y)},{float(point.first),float(point.second)})<8;
    })) return false;
    if(walkIntent.is_object() && walkState(c)=="failed") {cancelWalk(c);return false;}
    wait("known-target-approach","Approaching the last client-observed position: "+purpose);
    // Keep a submitted approach even when its endpoint reaches the target.
    // Assignment can appear/disappear while moving between active rooms;
    // recomputing that path every tick must not alternate it with exploration.
    exploring.reset();interest.reset();
    walkPoint(c,endpoint);
    approachHint=hint;approachFrontier=endpoint;
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
    const auto chooseWaypoint=[&]() -> std::optional<uint16_t> {
        const auto current=route(c,*area,destination);if(current.empty()) return {};
        size_t shortest=current.size();std::optional<uint16_t> best;
        for(const auto &entry:c.town.read().waypoints) if(entry.unlocked && entry.level!=*area) {
            const auto rest=route(c,entry.level,destination);
            if(!rest.empty() && rest.size()+2<shortest) {shortest=rest.size()+2;best=entry.level;}
        }
        return best;
    };
    if(travelPhase==TravelPhase::WaypointWait || travelPhase==TravelPhase::CrossWait) {
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
        const bool assigned=std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[](const auto &t){return t.interaction==OnlineMapInteraction::Waypoint;});
        if(!assigned) if(const auto hint=c.town.knownWaypointPosition(*area);hint && approachPoint(c,*hint,"known waypoint")) return false;
        wait("waypoint","Opening the actual assigned waypoint");
        if(!walk(c,{{"goal","waypoint"}})) return false;
        if(!waypointDestination) waypointDestination=chooseWaypoint();
        if(!waypointDestination) {
            c.session.use_waypoint(0);cancelWalk(c);travelPhase=TravelPhase::Route;return false;
        }
        const auto entry=std::find_if(c.town.read().waypoints.begin(),c.town.read().waypoints.end(),
            [&](const auto &v){return v.level==waypointDestination && v.unlocked;});
        if(entry==c.town.read().waypoints.end()) throw std::runtime_error("Waypoint destination is not unlocked");
        if(c.session.use_waypoint(entry->level,entry->number)) {
            legDestination=entry->level;travelPhase=TravelPhase::WaypointWait;actionDeadline=now+std::chrono::seconds(20);cancelWalk(c);
            note("waypoint-request-submitted",std::to_string(entry->level));
        }
        return false;
    }
    if(travelPhase==TravelPhase::WalkExit || travelPhase==TravelPhase::WarpExit) {
        const bool warp=travelPhase==TravelPhase::WarpExit;
        if(warp && legUnit && std::none_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &t){return t.unit==legUnit;})) {
            cancelWalk(c);travelPhase=TravelPhase::Route;legUnit.reset();note("exit-assignment-withdrawn");return false;
        }
        if(warp && legUnit) {
            const auto entry=std::find_if(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &t){return t.unit==legUnit;});
            const Vec wanted{float(entry->position.x),float(entry->position.y)};
            // Warp units can be assigned before every intervening room is
            // materialized. Approach through known terrain before asking the
            // exact interaction child to reach the actual warp vicinity.
            if(distance(c.position,wanted)>8) {
                if(!approachPoint(c,entry->position,"assigned level connection",6))
                    explore(c,"reachable approach to the assigned level connection");
                return false;
            }
        }
        if(!warp && walkIntent.is_object() && walkIntent.value("goal",std::string{})=="exit" && walkState(c)=="failed") {
            note("exit-approach-unavailable",c.walk.snapshot().value("reason",std::string{}));
            cancelWalk(c);travelPhase=TravelPhase::Route;exitRetryAt=now+std::chrono::seconds(5);
            explore(c,"another reconstructed approach to the walking connection");return false;
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
        ++interactions;travelPhase=TravelPhase::CrossWait;actionDeadline=now+std::chrono::seconds(15);return false;
    }
    const auto path=route(c,*area,destination);
    if(path.empty()) throw std::runtime_error("No resolved same-act level route; AI does not invent an act transfer or unlock");
    if(allowWaypoint) {
        const bool source=c.town.read().town || std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),
            [](const auto &v){return v.interaction==OnlineMapInteraction::Waypoint;});
        // 63 reports unlocked destinations only after an actual menu is opened.
        if(source && !c.session.read().world.waypointHistory) {travelPhase=TravelPhase::WaypointOpen;return false;}
        if(source) waypointDestination=chooseWaypoint();
        if(waypointDestination) {travelPhase=TravelPhase::WaypointOpen;return false;}
    }
    if(openDoor(c)) return false;
    legDestination=uint16_t(path.at(1));
    for(const auto &entry:c.town.read().mapTargets) if(entry.destination==legDestination &&
        (entry.interaction==OnlineMapInteraction::Exit || entry.interaction==OnlineMapInteraction::Portal)) {
        legUnit=entry.unit;travelPhase=TravelPhase::WarpExit;return false;
    }
    if(approachKnown(c,Selector{5,*legDestination,"known connection to "+std::to_string(*legDestination)})) return false;
    const auto edges=c.town.walkingBoundaries();
    if(now>=exitRetryAt && std::any_of(edges.begin(),edges.end(),[&](const auto &v){return v.destination==legDestination;})) {
        travelPhase=TravelPhase::WalkExit;return false;
    }
    explore(c,"connection to level "+std::to_string(*legDestination));return false;
}
}
