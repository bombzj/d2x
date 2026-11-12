#include "ai_impl.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace d2x {
namespace {
using Cell=DebugExplorationMemory::Cell;
using Json=nlohmann::json;
int number(const Json &value,int low,int high) {
    if(!value.is_number_integer()) throw std::invalid_argument("Search knowledge requires integer values");
    const auto n=value.get<int64_t>();
    if(n<low || n>high) throw std::invalid_argument("Search knowledge value outside supported range");
    return int(n);
}
Vec vector(const Json &value,int limit) {
    if(!value.is_array() || value.size()!=2) throw std::invalid_argument("Search vector requires [x,y]");
    return {float(number(value[0],-limit,limit)),float(number(value[1],-limit,limit))};
}
bool inside(Vec at,const std::optional<Vec> &center,const std::optional<float> &radius) {
    return !radius || (center && (at-*center).length()<=*radius);
}
}
DebugAi::Impl::TargetSearchKnowledge DebugAi::Impl::parseSearchKnowledge(const Json &j,TargetSearchKnowledge result) const {
    if(!j.is_object()) throw std::invalid_argument("search knowledge must be an object");
    for(const auto &[key,value]:j.items()) {
        if(key=="anchor") result.anchor=definitions->selector(value);
        else if(key=="radius") result.radius=value.is_null()?std::nullopt:std::optional{float(number(value,1,1024))};
        else if(key=="offset") result.offset=vector(value,1024);
        else if(key=="direction") {
            result.direction=vector(value,1);
            if(result.direction->length()==0) throw std::invalid_argument("Search direction must be nonzero");
        } else if(key=="waitMs") result.waitMs=unsigned(number(value,0,120000));
        else if(key=="candidateRooms") {
            if(!value.is_array() || value.empty() || value.size()>16)
                throw std::invalid_argument("candidateRooms requires 1..16 relative room offsets");
            result.candidateRooms.clear();
            for(const auto &entry:value) {
                const auto at=vector(entry,32);const Cell offset{int(at.x),int(at.y)};
                if(std::find(result.candidateRooms.begin(),result.candidateRooms.end(),offset)!=result.candidateRooms.end())
                    throw std::invalid_argument("Duplicate candidate room offset");
                result.candidateRooms.push_back(offset);
            }
        } else if(key=="returnViaWaypoint") {
            if(!value.is_boolean()) throw std::invalid_argument("returnViaWaypoint must be boolean");
            result.returnViaWaypoint=value.get<bool>();
        } else if(key=="timeoutSeconds") result.timeoutSeconds=unsigned(number(value,1,3600));
        else if(key=="openedObjects") {
            if(!value.is_array() || value.size()>16) throw std::invalid_argument("openedObjects requires at most 16 object identities");
            result.openedObjects.clear();
            for(const auto &object:value) result.openedObjects.push_back(definitions->object(object));
        } else throw std::invalid_argument("Unknown search knowledge field: "+key);
    }
    if(!result.candidateRooms.empty() && (!result.anchor || result.radius || result.direction ||
        result.offset.x || result.offset.y || result.waitMs || !result.openedObjects.empty()))
        throw std::invalid_argument("Candidate rooms require an anchor and a static search without radius, offset, direction or spawn wait");
    if(result.returnViaWaypoint && result.candidateRooms.empty())
        throw std::invalid_argument("Waypoint branch return requires candidate rooms");
    return result;
}
void DebugAi::Impl::configureSearchKnowledge(std::vector<Goal> &plan,const Json &j) const {
    if(!j.is_object()) throw std::invalid_argument("searchKnowledge must map quest requirement names to search knowledge");
    // Unused profiles are permitted when the command selects only one act.
    // Parse every value even then, so malformed knowledge cannot be hidden.
    for(const auto &[name,value]:j.items()) {
        parseSearchKnowledge(value,{});
        for(auto &goal:plan) for(size_t i=0;i<goal.requirements.size();++i)
            if(goal.requirements[i].name==name)
                goal.steps[i].search=parseSearchKnowledge(value,goal.steps[i].search.value_or(TargetSearchKnowledge{}));
    }
}
void DebugAi::Impl::observeSearchEvents(const Context &c) {
    const auto &view=c.observation.read();
    for(const auto &[key,unit]:view.world.units) {
        if(key.type!=2 || !unit.classId || !unit.position) continue;
        const auto level=c.town.assignedUnitLevel(key);if(!level) continue;
        const auto id=std::pair{*level,*unit.classId};
        const auto mode=c.town.projectedObjectMode(view,key);
        if(mode==0) navigation.openedObjects.erase(id);
        if(mode!=2 || navigation.openedObjects.contains(id)) continue;
        navigation.openedObjects[id]={*unit.position,Clock::now(),++navigation.searchEventSequence};
    }
}
bool DebugAi::Impl::searchTrigger(Context &c,const TargetSearchKnowledge &rule,Clock::time_point &readyAt,uint64_t &event) {
    if(!area) return false;
    for(const auto object:rule.openedObjects) {
        const auto opened=navigation.openedObjects.find({*area,object});
        if(opened==navigation.openedObjects.end()) {
            // The recipe's prerequisite is evidence, not the submitted click.
            const Selector trigger{2,object,"search trigger"};
            if(!approachKnown(c,trigger)) wait("target-search-trigger","Waiting to observe the opened search trigger");
            return false;
        }
        readyAt=std::max(readyAt,opened->second.observedAt);
        event=std::max(event,opened->second.event);
    }
    if(readyAt!=Clock::time_point{}) readyAt+=std::chrono::milliseconds(rule.waitMs);
    return true;
}
void DebugAi::Impl::searchTarget(Context &c,const Selector &selector,const TargetSearchKnowledge *knowledge) {
    if(!area) return;
    const TargetSearchKnowledge empty;
    const auto &rule=knowledge?*knowledge:empty;
    std::optional<Vec> center;Clock::time_point readyAt{};uint64_t event=0;
    if(!searchTrigger(c,rule,readyAt,event)) return;
    // A target observed after the trigger and then lost starts a new search
    // from its last real sighting. Earlier sightings cannot satisfy a birth.
    if(selector.type==1 && !selector.npc) if(const auto population=navigation.populationMemory.find(*area);population!=navigation.populationMemory.end()) {
        Clock::time_point lastSeen{};
        for(const auto &[unit,enemy]:population->second.enemies) {
            (void)unit;
            if(enemy.identity!=selector.identity || (selector.superUnique && enemy.superUnique!=selector.superUnique) ||
                (selector.eliteOnly && !enemy.elite) || enemy.lastSeen<lastSeen ||
                (readyAt!=Clock::time_point{} && enemy.lastSeen<readyAt-std::chrono::milliseconds(rule.waitMs))) continue;
            lastSeen=enemy.lastSeen;
        }
        if(lastSeen!=Clock::time_point{}) {
            event^=uint64_t(lastSeen.time_since_epoch().count());readyAt=lastSeen;
        }
    }
    const auto key="unit:"+std::to_string(selector.type)+":"+std::to_string(selector.identity)+":"+
        std::to_string(selector.superUnique.value_or(UINT16_MAX))+":"+std::to_string(selector.eliteOnly);
    // Exact, previously assigned identities remain useful leads. They are
    // reobserved through normal movement, never injected into path collision.
    const auto previous=navigation.targetSearches.find(std::to_string(*area)+":"+key);
    const auto effectiveReady=readyAt!=Clock::time_point{}?readyAt:
        previous!=navigation.targetSearches.end()?previous->second.readyAt:Clock::now()+std::chrono::milliseconds(rule.waitMs);
    if(effectiveReady<=Clock::now() && approachKnown(c,selector)) return;
    if(rule.anchor) {
        const auto assigned=find(c,*rule.anchor,true);
        auto point=assigned?assigned->position:c.town.knownTargetPosition(*area,rule.anchor->type,rule.anchor->identity);
        if(!point && rule.anchor->type==2)
            if(const auto opened=navigation.openedObjects.find({*area,rule.anchor->identity});opened!=navigation.openedObjects.end()) point=opened->second.position;
        // Authored fixed landmarks may bound a search before assignment.
        // Relative room layouts still require their actual observed anchor.
        if(!point && rule.candidateRooms.empty()) point=experienceHint(c,*rule.anchor);
        if(!point) {
            searchTarget(c,*rule.anchor);return;
        }
        center=Vec{float(point->x),float(point->y)}+rule.offset;
    }
    searchArea(c,key,selector.name,rule,center,readyAt,event,rule.waitMs>0 || !rule.openedObjects.empty());
}
void DebugAi::Impl::searchWorldTarget(Context &c,const QuestStep &step,OnlinePoint anchor) {
    TargetSearchKnowledge rule=step.search.value_or(TargetSearchKnowledge{});
    if(!rule.radius) rule.radius=float(eventPolicy.encounterRadius);
    if(!rule.timeoutSeconds) rule.timeoutSeconds=step.resultWaitSeconds?step.resultWaitSeconds:unsigned(eventPolicy.waitSeconds);
    if(!step.search) rule.waitMs=step.initialWaitMs?step.initialWaitMs:unsigned(eventPolicy.initialWaitMs);
    if(rule.anchor && area) {
        const auto unit=find(c,*rule.anchor,true);
        const auto known=unit?unit->position:c.town.knownTargetPosition(*area,rule.anchor->type,rule.anchor->identity);
        if(!known) {searchTarget(c,*rule.anchor);return;}
        anchor=*known;
    }
    // Assigned but not yet usable portals/monsters retain their normal event
    // wait. This search is only for a missing assignment.
    const std::string key="quest:"+std::to_string(cursor)+":"+std::to_string(questState.questRun.step);
    Clock::time_point readyAt{};uint64_t event=0;
    if(!searchTrigger(c,rule,readyAt,event)) return;
    searchArea(c,key,"expected quest assignment",rule,Vec{float(anchor.x),float(anchor.y)}+rule.offset,readyAt,event,true);
}
void DebugAi::Impl::targetSearchObserved(Context &c) {
    if(!navigation.activeTargetSearch) return;
    note("target-search-assignment-observed",*navigation.activeTargetSearch);
    navigation.targetSearches.erase(*navigation.activeTargetSearch);
    navigation.activeTargetSearch.reset();navigation.targetSearchDiagnostics["result"]="assignment-observed";
    if(navigation.walkIntent.purpose.value_or(std::string{})=="target-search") cancelWalk(c);
}
void DebugAi::Impl::searchArea(Context &c,std::string key,std::string purpose,const TargetSearchKnowledge &rule,
        std::optional<Vec> center,Clock::time_point readyAt,uint64_t event,bool awaitingAppearance,bool coverageOnly) {
    if(!area) return;
    if(navigation.localPad && exploreLocalPad(c)) return;
    const auto now=Clock::now();const auto &view=c.observation.read();
    key=std::to_string(*area)+":"+key;
    auto [entry,created]=navigation.targetSearches.try_emplace(key);
    auto &search=entry->second;
    if(created || search.area!=*area || search.seed!=view.load.mapSeed || search.event!=event) {
        std::vector<std::array<int,4>> regions;
        if(!rule.candidateRooms.empty()) {
            if(!center) throw std::runtime_error("Candidate room search needs an observed anchor");
            const auto &rooms=c.town.navigationRooms();
            const auto anchor=std::find_if(rooms.begin(),rooms.end(),[&](const auto &room) {
                return room.level==*area && room.width>0 && room.height>0 && center->x>=room.x && center->y>=room.y &&
                    center->x<room.x+room.width && center->y<room.y+room.height;
            });
            if(anchor==rooms.end()) {wait("target-search-anchor","Waiting for the observed anchor's room geometry");return;}
            for(const auto &[dx,dy]:rule.candidateRooms) {
                const int x=anchor->x+dx*anchor->width,y=anchor->y+dy*anchor->height;
                if(x<0 || y<0 || x+anchor->width>65536 || y+anchor->height>65536)
                    throw std::runtime_error("Candidate room lies outside native coordinates");
                regions.push_back({x,y,anchor->width,anchor->height});
            }
        }
        if(navigation.activeTargetSearch==key) cancelWalk(c);
        search={};search.area=*area;search.seed=view.load.mapSeed;search.event=event;search.awaitingAppearance=awaitingAppearance;
        search.center=center;search.radius=rule.radius;
        search.regions=std::move(regions);search.regionResults.resize(search.regions.size());
        if(search.radius && !center) throw std::runtime_error("Bounded target search needs an observed or configured anchor");
        if(center && (center->x<0 || center->y<0 || center->x>65535 || center->y>65535))
            throw std::runtime_error("Target search anchor lies outside native coordinates");
        search.readyAt=readyAt==Clock::time_point{}?now+std::chrono::milliseconds(rule.waitMs):readyAt;
        search.deadline=now+std::chrono::seconds(rule.timeoutSeconds?rule.timeoutSeconds:unsigned(goals.at(cursor).timeout));
        note("target-search-started",Json{{"target",purpose},{"area",*area},{"event",event},
            {"center",center?Json{center->x,center->y}:Json(nullptr)},{"radius",rule.radius?Json(*rule.radius):Json(nullptr)},
            {"awaitingAppearance",search.awaitingAppearance},
            {"waitMs",std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(search.readyAt-now).count())}}.dump());
    }
    if(navigation.activeTargetSearch!=key) {
        cancelWalk(c);navigation.frontierRoute.reset();navigation.approachHint.reset();navigation.approachFrontier.reset();
        navigation.activeTargetSearch=key;
    }
    const auto selectRegion=[&] {
        std::optional<size_t> selected;float nearest=std::numeric_limits<float>::max();
        for(size_t i=0;i<search.regions.size();++i) if(search.regionResults[i].empty()) {
            const auto &b=search.regions[i];const Vec at{float(b[0]+b[2]/2),float(b[1]+b[3]/2)};
            const auto distance=(at-c.position).length();
            if(distance<nearest) {nearest=distance;selected=i;}
        }
        search.region=selected;
        if(!selected) return false;
        const auto &b=search.regions[*selected];search.center=Vec{float(b[0]+b[2]/2),float(b[1]+b[3]/2)};
        // The approach disk is inside the authored room. Search coverage
        // below uses the whole rectangle, including its corners.
        search.radius=float(std::max(1,std::min(b[2],b[3])/2-1));
        search.destination.reset();search.objective.reset();search.route.clear();search.arrivalSince.reset();
        search.probe=false;search.transit=false;search.probePassage=0;search.probeEvidence=0;
        search.observationWait.reset();
        search.candidates.clear();search.candidateRevision=~uint64_t{};
        cancelWalk(c);navigation.frontierRoute.reset();
        navigation.approachHint.reset();navigation.approachFrontier.reset();navigation.approachObservation={};
        note("target-search-region-selected",Json{{"key",key},{"region",*selected},{"bounds",b}}.dump());
        return true;
    };
    if(!search.regions.empty() && !search.region && !search.exhausted) selectRegion();
    const auto inRange=[&](Vec at) {
        if(!search.region) return inside(at,search.center,search.radius);
        const auto &b=search.regions[*search.region];
        return at.x>=b[0] && at.y>=b[1] && at.x<b[0]+b[2] && at.y<b[1]+b[3];
    };
    auto &diagnostics=navigation.targetSearchDiagnostics;
    diagnostics={{"key",key},{"target",purpose},{"event",search.event},{"area",search.area},
        {"center",search.center?Json{search.center->x,search.center->y}:Json(nullptr)},
        {"radius",search.radius?Json(*search.radius):Json(nullptr)},{"checkedTiles",search.checked.size()},
        {"awaitingAppearance",search.awaitingAppearance},
        {"waitRemainingMs",std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(search.readyAt-now).count())},
        {"remainingMs",std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(search.deadline-now).count())},
        {"result",now<search.readyAt?"waiting":"searching"}};
    diagnostics["regions"]=search.regions;diagnostics["regionResults"]=search.regionResults;
    diagnostics["region"]=search.region?Json(*search.region):Json(nullptr);
    if(search.coverageComplete) {diagnostics["result"]="coverage-checked";diagnostics["absenceProven"]=false;return;}
    const auto exhausted=[&](std::string reason) {
        if(search.observationWait) diagnostics["lastRoundResult"]=search.observationWait->reason;
        search.exhausted=true;diagnostics["result"]=reason;diagnostics["checked"]=search.checked;
        diagnostics["remainingCandidates"]=search.candidates;
        diagnostics["absenceProven"]=false;
        if(!search.regions.empty()) {
            diagnostics["regionResults"]=search.regionResults;
            diagnostics["region"]=search.region?Json(*search.region):Json(nullptr);
            const auto *terrain=c.walk.navigationSearch().explorationMemory.area(search.area);
            diagnostics["regionCoverage"]=Json::array();
            for(size_t i=0;i<search.regions.size();++i) {
                const auto &b=search.regions[i];size_t unknown=0,checked=0;
                for(int y=b[1]/5;y<(b[1]+b[3]+4)/5;++y) for(int x=b[0]/5;x<(b[0]+b[2]+4)/5;++x) {
                    if(!terrain || !terrain->terrainLevels.contains({x,y})) ++unknown;
                    if(search.checked.contains({x,y})) ++checked;
                }
                diagnostics["regionCoverage"].push_back(Json{{"bounds",b},{"result",search.regionResults[i]},
                    {"checkedTiles",checked},{"unpublishedTiles",unknown}});
            }
        } else if(search.center && search.radius) {
            size_t unknown=0;const auto *terrain=c.walk.navigationSearch().explorationMemory.area(search.area);
            const auto center=*search.center;const int radius=int(std::ceil(*search.radius));
            for(int y=std::max(0,int(center.y)-radius)/5;y<=std::min(65535,int(center.y)+radius)/5;++y)
                for(int x=std::max(0,int(center.x)-radius)/5;x<=std::min(65535,int(center.x)+radius)/5;++x) {
                    const Vec nearest{std::clamp(center.x,float(x*5),float(x*5+4)),std::clamp(center.y,float(y*5),float(y*5+4))};
                    if(inside(nearest,search.center,search.radius) && (!terrain || !terrain->terrainLevels.contains({x,y}))) ++unknown;
                }
            diagnostics["unpublishedTiles"]=unknown;
        }
        note("target-search-stopped",diagnostics.dump());cancelWalk(c);
        throw std::runtime_error("Target search stopped for "+purpose+": "+reason+"; checked "+
            std::to_string(search.checked.size())+" native tiles. See targetSearch; scene retained");
    };
    if(search.exhausted) exhausted("previous-round-exhausted");
    if(now>=search.deadline) exhausted("deadline");
    if(now<search.readyAt) {
        wait("target-search-wait","Waiting for the expected appearance before checking "+purpose);return;
    }
    auto *memory=observeTerrain(c);
    if(!memory) {wait("target-search-terrain","Waiting for acquired terrain");return;}
    const auto &loading=c.town.read().mapLoading;
    if(!c.town.read().nativeMapReady || loading.pendingEvents || loading.terrainDirty) {
        wait("target-search-terrain","Waiting for the subscribed rooms' terrain publication");return;
    }
    const auto awaitCollisionTransition=[&] {
        const auto remaining=c.town.navigationTransitionRemainingMilliseconds();
        if(!remaining) return false;
        diagnostics["result"]="waiting-for-collision-transition";
        diagnostics["collisionTransitionRemainingMs"]=remaining;
        wait("target-search-transition","Waiting for an observed object animation to change passage collision");return true;
    };
    if(search.radius && search.center && !inRange(c.position)) {
        const OnlinePoint anchor{uint16_t(search.center->x),uint16_t(search.center->y)};
        if(approachPoint(c,anchor,"target search range",*search.radius)) return;
    }
    // waitMs permits the first spatial check, not a claim that spawning has
    // finished. Continuously subscribed coverage can observe a later birth.
    std::erase_if(search.rooms,[&](const auto &room){return !view.world.rooms.contains(room.first);});
    bool settling=false;
    std::set<Cell> subscribed;
    const auto observationFeet=DebugExplorationMemory::playerCell(c.position);
    const auto grace=std::chrono::milliseconds(std::clamp<uint64_t>(2ULL*view.latencyMilliseconds.value_or(0)+250,250,3000));
    for(const auto &room:c.town.navigationRooms()) {
        const auto roomKey=std::tuple{uint8_t(room.level),uint16_t(room.x/5),uint16_t(room.y/5)};
        if(room.level!=*area || !room.loaded || !view.world.rooms.contains(roomKey)) continue;
        const auto receipt=view.world.roomAssignmentRevisions.find(roomKey);
        const auto revision=receipt==view.world.roomAssignmentRevisions.end()?0:receipt->second;
        auto [r,fresh]=search.rooms.try_emplace(roomKey,debug_ai::NavigationState::TargetSearch::Room{revision,now+grace});
        if(!fresh && r->second.assignment!=revision) r->second={revision,now+grace};
        if(now<r->second.readyAt) {settling=true;continue;}
        const bool entered=observationFeet.first>=room.x && observationFeet.second>=room.y &&
            observationFeet.first<room.x+room.width && observationFeet.second<room.y+room.height;
        for(int y=room.y/5;y<(room.y+room.height)/5;++y) for(int x=room.x/5;x<(room.x+room.width)/5;++x) {
            // Include a boundary tile only if it intersects the search range.
            Vec nearest{float(x*5+2),float(y*5+2)};
            if(search.center) nearest={std::clamp(search.center->x,float(x*5),float(x*5+4)),std::clamp(search.center->y,float(y*5),float(y*5+4))};
            if(inRange(nearest)) {
                subscribed.emplace(x,y);
                // A neighbor's terrain receipt cannot complete this round's
                // unit check. Existing subscribed coverage still observes late
                // assignments; mark new coverage only after entering the room.
                if(entered && search.checked.emplace(x,y).second) ++search.coverageRevision;
            }
        }
    }
    diagnostics["checkedTiles"]=search.checked.size();diagnostics["settling"]=settling;
    const auto revisitInterval=std::max(std::chrono::milliseconds(rule.waitMs),
        std::chrono::duration_cast<std::chrono::milliseconds>(passageRetryInterval));
    const auto observeAppearance=[&](std::string reason) {
        if(coverageOnly && reason=="no-unchecked-reachable-range") {
            search.coverageComplete=true;diagnostics["result"]="coverage-checked";
            diagnostics["absenceProven"]=false;diagnostics["checked"]=search.checked;
            cancelWalk(c);note("target-search-coverage-checked",diagnostics.dump());return;
        }
        if(search.region) {
            search.regionResults[*search.region]=reason;
            note("target-search-region-finished",Json{{"key",key},{"region",*search.region},{"reason",reason}}.dump());
            if(selectRegion()) {
                if(rule.returnViaWaypoint && search.center)
                    beginExplorationReturn(c,{uint16_t(search.center->x),uint16_t(search.center->y)});
                wait("target-search-next-region","Approaching the next configured target region");return;
            }
            diagnostics["regionResults"]=search.regionResults;
            exhausted("configured-regions-exhausted");return;
        }
        if(!search.awaitingAppearance) {exhausted(std::move(reason));return;}
        if(!search.observationWait) {
            search.observationWait=debug_ai::NavigationState::TargetSearch::ObservationWait{
                std::move(reason),memory->revision,search.coverageRevision,DebugExplorationMemory::playerCell(c.position),
                teleportAvailable(c,false),now+revisitInterval};
            cancelWalk(c);
            note("target-search-awaiting-appearance",Json{{"key",key},{"roundResult",search.observationWait->reason},
                {"checkedTiles",search.checked.size()},{"remainingMs",diagnostics["remainingMs"]}}.dump());
        }
        diagnostics["result"]="awaiting-appearance";diagnostics["lastRoundResult"]=search.observationWait->reason;
        diagnostics["revisitInMs"]=std::max<int64_t>(0,
            std::chrono::duration_cast<std::chrono::milliseconds>(search.observationWait->revisitAt-now).count());
        wait("target-search-appearance","Range checked; observing the expected assignment until its deadline");
    };
    if(search.observationWait) {
        auto &pending=*search.observationWait;
        const bool changed=pending.terrain!=memory->revision || pending.coverage!=search.coverageRevision ||
            pending.from!=DebugExplorationMemory::playerCell(c.position) || pending.teleport!=teleportAvailable(c,false);
        if(!changed) {
            size_t revisit=0;
            if(now>=pending.revisitAt && !settling) {
                // Only stale, out-of-subscription coverage needs a new trip.
                // Do not clear current coverage and submit a zero-cost search
                // over the same visible rooms while waiting for a red portal.
                revisit=std::erase_if(search.checked,[&](Cell tile){return !subscribed.contains(tile);});
                pending.revisitAt=now+revisitInterval;
            }
            if(!revisit) {observeAppearance(pending.reason);return;}
            ++search.coverageRevision;search.candidateRevision=~uint64_t{};
            diagnostics["checkedTiles"]=search.checked.size();
            note("target-search-revisit",Json{{"key",key},{"tiles",revisit}}.dump());
        }
        search.observationWait.reset();
    }
    if(search.destination) {
        const auto point=*search.destination;const Cell tile{point.x/5,point.y/5};
        diagnostics["destination"]={point.x,point.y};diagnostics["transit"]=search.transit;
        diagnostics["terrainProbe"]=search.probe;
        const auto feet=DebugExplorationMemory::playerCell(c.position);
        if(!search.probe && !search.transit && search.checked.contains(tile)) {
            cancelWalk(c);search.destination.reset();search.route.clear();search.arrivalSince.reset();
        } else if(feet==Cell{point.x,point.y}) {
            const auto observation=DebugNavigationSearch::observationStatus(c.town,view,c.position,c.moving,search.arrivalSince);
            using Observation=DebugNavigationSearch::ObservationStatus;
            switch(observation) {
            case Observation::Moving:diagnostics["observation"]="local-path-moving";break;
            case Observation::TerrainPending:diagnostics["observation"]="terrain-pending";break;
            case Observation::RoomPending:diagnostics["observation"]="room-not-subscribed";break;
            case Observation::Settling:diagnostics["observation"]="room-messages-settling";break;
            case Observation::Ready:diagnostics["observation"]="ready";break;
            }
            if(observation!=Observation::Ready) {
                wait("target-search-observation","Waiting for "+diagnostics["observation"].get<std::string>());return;
            }
            if(settling) {
                diagnostics["observation"]="round-room-messages-settling";
                wait("target-search-observation","Waiting for this round's room messages");return;
            }
            if(search.probe) search.probes[search.probePassage]=search.probeEvidence;
            else if(!search.transit && !search.checked.contains(tile)) exhausted("arrival-without-unit-coverage");
            cancelWalk(c);search.destination.reset();search.route.clear();search.arrivalSince.reset();
            search.candidateRevision=~uint64_t{};
        }
    }
    if(!search.destination) {
        if(search.candidateRevision!=memory->revision || search.candidateCoverage!=search.coverageRevision) {
            search.candidates.clear();
            for(const auto &[tile,level]:memory->terrainLevels) {
                if(level!=*area || search.checked.contains(tile)) continue;
                for(int y=tile.second*5;y<tile.second*5+5;++y) for(int x=tile.first*5;x<tile.first*5+5;++x)
                    if(inRange({float(x),float(y)}) && memory->stand(x,y)) search.candidates.emplace_back(x,y);
            }
            // New terrain can still be needed. These probes are local to this
            // search, independent of the old global unit-observation frontier.
            for(const auto &passage:memory->passages) if(!passage.units &&
                (!search.probes.contains(passage.id) || search.probes.at(passage.id)!=passage.evidence)) for(const auto &at:passage.approaches)
                if(inRange({float(at.first),float(at.second)}) && memory->stand(at.first,at.second) &&
                    memory->terrainLevels.contains({at.first/5,at.second/5}) && memory->terrainLevels.at({at.first/5,at.second/5})==*area)
                    search.candidates.push_back(at);
            std::sort(search.candidates.begin(),search.candidates.end());
            search.candidates.erase(std::unique(search.candidates.begin(),search.candidates.end()),search.candidates.end());
            search.candidateRevision=memory->revision;search.candidateCoverage=search.coverageRevision;
        }
        diagnostics["candidateCells"]=search.candidates.size();
        if(search.candidates.empty()) {
            if(settling) {wait("target-search-observation","Waiting for current room messages");return;}
            if(awaitCollisionTransition()) return;
            if(!search.radius && (openDoor(c,true) || exploreLocalPad(c) || recoverExplorationLanding(c,*memory))) return;
            observeAppearance("no-unchecked-reachable-range");return;
        }
        bool restricted=false;const bool teleport=teleportAvailable(c,false);
        if(teleport) for(size_t row=0;row<definitions->levels.rows().size();++row)
            if(definitions->levels.number(row,"Id")==area) restricted=definitions->levels.number(row,"Teleport").value_or(0)==2;
        if(search.objective && !std::binary_search(search.candidates.begin(),search.candidates.end(),
            Cell{search.objective->x,search.objective->y})) search.objective.reset();
        auto candidates=search.objective?std::vector<Cell>{{search.objective->x,search.objective->y}}:search.candidates;
        if(rule.direction && !search.objective) {
            const Vec origin=search.center.value_or(c.position);
            std::erase_if(candidates,[&](Cell at) {
                const Vec v=Vec{float(at.first),float(at.second)}-origin;
                return v.x*rule.direction->x+v.y*rule.direction->y<0;
            });
        }
        auto &planner=c.walk.navigationSearch();
        auto plan=planner.observedRoute(*area,c.position,teleport?mobilityPolicy.distance:0,restricted,
            search.center.value_or(c.position),0,0,false,candidates,{},!teleport);
        if(plan.points.empty() && !search.objective && candidates!=search.candidates)
            plan=planner.observedRoute(*area,c.position,teleport?mobilityPolicy.distance:0,restricted,
                search.center.value_or(c.position),0,0,false,search.candidates,{},!teleport);
        if(followLocalPadRoute(c,plan)) return;
        if(plan.points.empty()) {
            if(settling) {wait("target-search-observation","Waiting for current room messages");return;}
            if(awaitCollisionTransition()) return;
            if(!search.radius && (openDoor(c,true) || exploreLocalPad(c) || recoverExplorationLanding(c,*memory))) return;
            if(plan.outcome==DebugExplorationMemory::Plan::Outcome::StartUnavailable) {
                observeAppearance("start-collision-unavailable");return;
            }
            if(plan.outcome==DebugExplorationMemory::Plan::Outcome::ExecutionBlocked) {
                observeAppearance("no-executable-route-prefix");return;
            }
            observeAppearance("remaining-range-unreachable");return;
        }
        search.destination=plan.points.back();search.route=std::move(plan.points);
        search.transit=plan.outcome==DebugExplorationMemory::Plan::Outcome::Partial;
        search.objective=search.transit?plan.objective:std::nullopt;
        search.probe=!search.transit && search.checked.contains({search.destination->x/5,search.destination->y/5});
        if(search.probe) for(const auto &passage:memory->passages) if(!passage.units &&
            (!search.probes.contains(passage.id) || search.probes.at(passage.id)!=passage.evidence) &&
            std::find(passage.approaches.begin(),passage.approaches.end(),Cell{search.destination->x,search.destination->y})!=passage.approaches.end()) {
            search.probePassage=passage.id;search.probeEvidence=passage.evidence;break;
        }
        search.arrivalSince.reset();
        note("target-search-destination",Json{{"key",key},{"point",{search.destination->x,search.destination->y}},
            {"terrainProbe",search.probe},{"transit",search.transit},{"routeCost",plan.cost}}.dump());
    }
    diagnostics["destination"]={search.destination->x,search.destination->y};
    diagnostics["transit"]=search.transit;
    diagnostics["objective"]=search.objective?Json{search.objective->x,search.objective->y}:Json(nullptr);
    if(DebugExplorationMemory::playerCell(c.position)==Cell{search.destination->x,search.destination->y}) {
        wait("target-search-observation","At the selected observation point; waiting for its result");return;
    }
    const auto leg=terrainLeg(c,search.route);
    if(!leg.point) {
        // Requery the same semantic destination as more pages become resident.
        // A transient occupied next step is handled by the ordinary executor.
        search.destination.reset();search.route.clear();
        wait("target-search-route-wait","Waiting for a local leg into the selected search range");return;
    }
    wait("target-search-moving","Checking "+purpose+" within the requested range");
    walk(c,{.goal=DebugWalkGoal::Point,.point=*leg.point,.interact=false,.exactPoint=true,.purpose="target-search",.routeRelay=true});
}
} // namespace d2x
