#include "ai_impl.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace d2x {
void DebugAi::Impl::observeLocalPad(Context &c) {
    if(!navigation.localPad || !navigation.localPad->waiting) return;
    const auto &view=c.observation.read();const auto &world=view.world;
    const auto actor=view.load.playerUnitId?world.units.find({0,*view.load.playerUnitId}):world.units.end();
    if(c.town.read().area==navigation.localPad->area && world.playerPosition && actor!=world.units.end() &&
        actor->second.positionDiscontinuity>navigation.localPad->discontinuity && actor->second.positionRevision>navigation.localPad->requestRevision) {
        // Original Operate27 selects its counterpart from the server's room
        // unit lists. Learn only the actual own-unit relocation, not a guessed
        // pairing from MPQ presets or the nearest similarly looking object.
        const auto transfer=*navigation.localPad;const auto destination=*world.playerPosition;
        const auto key=std::tuple{transfer.area,transfer.identity,transfer.source.x,transfer.source.y};
        if(navigation.padObservations.size()>=512 && !navigation.padObservations.contains(key))
            throw std::runtime_error("Observed local-transport edge capacity exhausted");
        auto &record=navigation.padObservations[key];record.destination=destination;record.used=Clock::now();++record.visits;
        const auto memory=c.town.navigationMemory();
        if(memory.observedCells>navigation.padProgressCells || memory.targets>navigation.padProgressTargets) navigation.padTransfersWithoutDiscovery=0;
        else ++navigation.padTransfersWithoutDiscovery;
        navigation.padProgressCells=memory.observedCells;navigation.padProgressTargets=memory.targets;
        navigation.padArrival=destination;navigation.padArrivedAt=Clock::now();navigation.localPad.reset();cancelWalk(c);
        note("local-pad-arrival",std::to_string(transfer.source.x)+","+std::to_string(transfer.source.y)+" -> "+
            std::to_string(destination.x)+","+std::to_string(destination.y));
        return;
    }
    if(Clock::now()>=navigation.localPad->deadline)
        throw std::runtime_error("Submitted local teleport pad has no actual relocation result; operation is not replayed");
}
bool DebugAi::Impl::exploreLocalPad(Context &c) {
    const auto &view=c.observation.read();const auto now=Clock::now();
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
    const auto *map=c.town.map();const auto origin=c.town.read().origin;
    if(!map || !origin || !area) return false;
    const auto memory=c.town.navigationMemory();
    if(memory.observedCells>navigation.padProgressCells || memory.targets>navigation.padProgressTargets) navigation.padTransfersWithoutDiscovery=0;
    if(navigation.padTransfersWithoutDiscovery>=16)
        throw std::runtime_error("Local teleport exploration exhausted its no-discovery budget; retaining observed links and scene");
    const Vec offset{float(origin->x),float(origin->y)},from=c.position-offset+Vec{.5f,.5f};
    const auto &grid=map->grid;
    const bool canTeleport=teleportAvailable(c);
    const OnlineMapTarget *chosen=nullptr;float best=std::numeric_limits<float>::max();
    for(const auto &entry:c.town.read().mapTargets) {
        if(entry.interaction!=OnlineMapInteraction::TeleportPad) continue;
        const auto unit=view.world.units.find(entry.unit);if(unit==view.world.units.end() || !unit->second.classId) continue;
        const Vec at{float(entry.position.x),float(entry.position.y)};const float span=(at-c.position).length();
        if(frontierCoolingDown(entry.position,8) || (navigation.padArrival && now-navigation.padArrivedAt<std::chrono::seconds(5) &&
            (at-Vec{float(navigation.padArrival->x),float(navigation.padArrival->y)}).length()<8)) continue;
        const auto key=std::tuple{*area,*unit->second.classId,entry.position.x,entry.position.y};
        const auto observed=navigation.padObservations.find(key);
        if(observed!=navigation.padObservations.end() && now-observed->second.used<std::chrono::seconds(2)) continue;
        float score=span+(observed==navigation.padObservations.end()?0.f:1000.f+float(observed->second.visits)*100.f);
        if(!canTeleport && !c.town.interactionReady(view,entry.unit,c.position)) {
            const auto path=c.walk.navigationSearch().path(grid,from,at-offset+Vec{.5f,.5f},true,playerMovement,true);
            // A disconnected platform can be reached with normal Teleport or
            // after further observation.read(). Lack of a walking path is not proof
            // that an actually assigned transport object is unusable.
            if(path.empty() || !c.town.interactionReady(view,entry.unit,path.back()+offset-Vec{.5f,.5f})) score+=200;
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
