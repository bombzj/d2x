#include "ai_impl.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace d2x {
void DebugAi::Impl::observeLocalPad(Context &c) {
    if(!navigation.localPad || !navigation.localPad->waiting) return;
    const auto &view=c.observation.read();const auto &world=view.world;
    const auto actor=view.load.playerUnitId?world.units.find({0,*view.load.playerUnitId}):world.units.end();
    const auto unit=world.units.find(navigation.localPad->unit);
    const auto defer=[&](std::string reason) {
        const auto transfer=*navigation.localPad;
        navigation.failedPads[transfer.unit]=transfer.assignment;
        if(auto *memory=c.walk.navigationSearch().explorationMemory.area(transfer.area))
            for(auto &edge:memory->transports) if(edge.identity==transfer.identity &&
                edge.source==DebugExplorationMemory::Cell{transfer.source.x,transfer.source.y} && edge.enabled) {
                edge.enabled=false;++memory->revision;
            }
        recordFrontierAttempt(c,transfer.source,"failed",reason);
        note("local-pad-unanswered",std::move(reason));navigation.localPad.reset();cancelWalk(c);
    };
    if(c.town.read().area!=navigation.localPad->area) {navigation.localPad.reset();cancelWalk(c);return;}
    if(c.town.read().area==navigation.localPad->area && world.playerPosition && actor!=world.units.end() &&
        actor->second.positionDiscontinuity>navigation.localPad->discontinuity && actor->second.positionRevision>navigation.localPad->requestRevision) {
        // Original Operate27 selects its counterpart from the server's room
        // unit lists. Learn only the actual own-unit relocation, not a guessed
        // pairing from MPQ presets or the nearest similarly looking object.
        const auto transfer=*navigation.localPad;const auto destination=*world.playerPosition;
        if(destination==transfer.from || actor->second.repositionRevision<=transfer.requestRevision || !(actor->second.repositionFlags&1)) {
            defer("Own-unit correction did not establish a transport link");return;
        }
        const auto key=std::tuple{transfer.area,transfer.identity,transfer.source.x,transfer.source.y};
        if(navigation.padObservations.size()>=512 && !navigation.padObservations.contains(key))
            throw std::runtime_error("Observed local-transport edge capacity exhausted");
        auto &record=navigation.padObservations[key];record.destination=destination;record.used=Clock::now();++record.visits;
        if(auto *known=c.walk.navigationSearch().explorationMemory.area(transfer.area)) {
            const DebugExplorationMemory::Transport link{transfer.identity,{transfer.source.x,transfer.source.y},
                {transfer.from.x,transfer.from.y},{destination.x,destination.y}};
            const auto old=std::find_if(known->transports.begin(),known->transports.end(),[&](const auto &edge) {
                return edge.identity==link.identity && edge.source==link.source;
            });
            if(old==known->transports.end()) {known->transports.push_back(link);++known->revision;}
            else if(*old!=link) {*old=link;++known->revision;}
        }
        const auto memory=c.town.navigationMemory();
        if(memory.observedCells>navigation.padProgressCells || memory.targets>navigation.padProgressTargets) navigation.padTransfersWithoutDiscovery=0;
        else ++navigation.padTransfersWithoutDiscovery;
        navigation.padProgressCells=memory.observedCells;navigation.padProgressTargets=memory.targets;
        navigation.padArrival=destination;navigation.padArrivedAt=Clock::now();navigation.localPad.reset();navigation.frontierRoute.reset();cancelWalk(c);
        note("local-pad-arrival",std::to_string(transfer.source.x)+","+std::to_string(transfer.source.y)+" -> "+
            std::to_string(destination.x)+","+std::to_string(destination.y));
        return;
    }
    if(unit==world.units.end() || unit->second.assignmentRevision!=navigation.localPad->assignment) {
        defer("Pad assignment changed before relocation was confirmed");return;
    }
    if(Clock::now()>=navigation.localPad->deadline)
        defer("No relocation after the normal client's bounded source-only retries; continuing other routes");
}
bool DebugAi::Impl::followLocalPadRoute(Context &c,const DebugExplorationMemory::Plan &plan) {
    if(!plan.transport || teleportAvailable(c,false) || !area) return false;
    const auto &edge=*plan.transport;const auto &view=c.observation.read();
    const OnlinePoint source{uint16_t(edge.source.first),uint16_t(edge.source.second)};
    if(navigation.padObservationSource!=source) {navigation.padObservationSource=source;navigation.padObservationSince.reset();}
    else if(navigation.walkIntent.purpose.value_or(std::string{})=="known-pad-approach" && c.walk.active()) {
        wait("local-pad-observation","Following the observed route to the transport source");return true;
    } else if(navigation.walkIntent.purpose.value_or(std::string{})=="known-pad-approach" && c.walk.observe().state==DebugWalkState::Failed) {
        if(auto *memory=c.walk.navigationSearch().explorationMemory.area(*area))
            for(auto &known:memory->transports) if(known==edge) {known.enabled=false;++memory->revision;}
        recordFrontierAttempt(c,source,"failed",c.walk.observe().reason);
        cancelWalk(c);return true;
    }
    for(const auto &target:c.town.read().mapTargets) {
        if(target.interaction!=OnlineMapInteraction::TeleportPad ||
            DebugExplorationMemory::Cell{target.position.x,target.position.y}!=edge.source) continue;
        const auto unit=view.world.units.find(target.unit);
        if(unit==view.world.units.end() || unit->second.classId!=edge.identity) continue;
        navigation.localPad=LocalPad{target.unit,unit->second.assignmentRevision,*area,edge.identity,target.position};
        navigation.localPad->deadline=Clock::now()+std::chrono::seconds(30);
        note("local-pad-route-selected",Json{{"source",edge.source},{"destination",edge.to},{"cost",plan.cost}}.dump());
        return exploreLocalPad(c);
    }
    // Revisit the learned source through acquired terrain until its real unit
    // is assigned again. Never fabricate a GUID or walk the transport edge.
    auto prefix=plan.points;
    const auto end=std::find(prefix.begin(),prefix.end(),OnlinePoint{uint16_t(edge.from.first),uint16_t(edge.from.second)});
    if(end==prefix.end()) throw std::logic_error("Transport route has no source stance");
    prefix.erase(end+1,prefix.end());
    const auto leg=terrainLeg(c,prefix);
    if(leg.point && DebugExplorationMemory::playerCell(c.position)!=edge.from)
        walk(c,{.goal=DebugWalkGoal::Point,.point=*leg.point,.interact=false,.exactPoint=true,.purpose="known-pad-approach",.routeRelay=true});
    else if(DebugExplorationMemory::playerCell(c.position)==edge.from) {
        if(DebugNavigationSearch::observationStatus(c.town,view,c.position,c.moving,navigation.padObservationSince)!=
            DebugNavigationSearch::ObservationStatus::Ready) {
            wait("local-pad-observation","Waiting for the source room's actual unit assignments");return true;
        }
        auto *memory=c.walk.navigationSearch().explorationMemory.area(*area);
        if(memory) for(auto &known:memory->transports) if(known==edge) {known.enabled=false;++memory->revision;}
    }
    wait("local-pad-observation","Revisiting the observed source of a walking transport route");return true;
}
bool DebugAi::Impl::exploreLocalPad(Context &c) {
    const auto &view=c.observation.read();const auto now=Clock::now();
    if(teleportAvailable(c,false)) {
        if(navigation.localPad && !navigation.localPad->waiting) {cancelWalk(c);navigation.localPad.reset();}
        return navigation.localPad && navigation.localPad->waiting;
    }
    if(navigation.localPad) {
        if(navigation.localPad->waiting) {wait("local-pad-result","Waiting for the actual same-level relocation");return true;}
        const auto unit=view.world.units.find(navigation.localPad->unit);
        if(unit==view.world.units.end() || unit->second.assignmentRevision!=navigation.localPad->assignment || area!=navigation.localPad->area) {
            cancelWalk(c);navigation.localPad.reset();return false;
        }
        const auto source=navigation.localPad->source;
        if(now>=navigation.localPad->deadline) {
            recordFrontierAttempt(c,source,"failed","Observed local-pad approach exhausted its execution budget");
            cancelWalk(c);navigation.localPad.reset();return false;
        }
        if((Vec{float(source.x),float(source.y)}-c.position).length()>8) {
            if(approachPoint(c,source,"observed local teleport pad",6)) return true;
            recordFrontierAttempt(c,source,"unreachable","No current approach to the observed local pad");
            cancelWalk(c);navigation.localPad.reset();return false;
        }
        wait("local-pad-approach","Approaching the actually assigned local teleport pad");
        const DebugWalkIntent intent{.goal=DebugWalkGoal::Unit,.unit=OnlineUnitKey{navigation.localPad->unit.type,navigation.localPad->unit.id},.interact=true,.purpose="exploration-pad"};
        if(walk(c,intent)) observeChild(c,goals[cursor]);
        return true;
    }
    if(!c.town.map() || !c.town.read().origin || !area) return false;
    const auto memory=c.town.navigationMemory();
    if(memory.observedCells>navigation.padProgressCells || memory.targets>navigation.padProgressTargets) navigation.padTransfersWithoutDiscovery=0;
    if(navigation.padTransfersWithoutDiscovery>=16)
        throw std::runtime_error("Local teleport exploration exhausted its no-discovery budget; retaining observed links and scene");
    const OnlineMapTarget *chosen=nullptr;float best=std::numeric_limits<float>::max();
    for(const auto &entry:c.town.read().mapTargets) {
        if(entry.interaction!=OnlineMapInteraction::TeleportPad) continue;
        const auto unit=view.world.units.find(entry.unit);if(unit==view.world.units.end() || !unit->second.classId) continue;
        if(const auto failed=navigation.failedPads.find(entry.unit);failed!=navigation.failedPads.end() && failed->second==unit->second.assignmentRevision) continue;
        const Vec at{float(entry.position.x),float(entry.position.y)};
        if(frontierCoolingDown(entry.position,8) || (navigation.padArrival && now-navigation.padArrivedAt<std::chrono::seconds(5) &&
            (at-Vec{float(navigation.padArrival->x),float(navigation.padArrival->y)}).length()<8)) continue;
        const auto key=std::tuple{*area,*unit->second.classId,entry.position.x,entry.position.y};
        const auto observed=navigation.padObservations.find(key);
        if(observed!=navigation.padObservations.end() && now-observed->second.used<std::chrono::seconds(2)) continue;
        float score=observed==navigation.padObservations.end()?0.f:1000.f+float(observed->second.visits)*100.f;
        if(!c.town.interactionReady(view,entry.unit,c.position)) {
            auto &planner=c.walk.navigationSearch();
            const auto path=planner.observedRoute(*area,c.position,0,false,at,0,0,false,
                planner.interactionApproaches(c.town,view,entry.unit));
            if(path.outcome!=DebugExplorationMemory::Plan::Outcome::Goal || path.points.empty()) continue;
            score+=path.cost;
        }
        if(score>=best) continue;
        best=score;chosen=&entry;
    }
    if(!chosen) return false;
    const auto &unit=view.world.units.at(chosen->unit);
    navigation.localPad=LocalPad{chosen->unit,unit.assignmentRevision,*area,*unit.classId,chosen->position};
    navigation.localPad->deadline=now+std::chrono::seconds(30);
    navigation.padProgressCells=memory.observedCells;navigation.padProgressTargets=memory.targets;
    note("local-pad-selected",std::to_string(chosen->position.x)+","+std::to_string(chosen->position.y));
    return exploreLocalPad(c);
}
} // namespace d2x
