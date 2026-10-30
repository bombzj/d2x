#include "ai_impl.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace d2x {
void DebugAi::Impl::observeLocalPad(Context &c) {
    if(!localPad || !localPad->waiting) return;
    const auto &view=c.session.read();const auto &world=view.world;
    const auto actor=view.load.playerUnitId?world.units.find({0,*view.load.playerUnitId}):world.units.end();
    if(c.town.read().area==localPad->area && world.playerPosition && actor!=world.units.end() &&
        actor->second.positionDiscontinuity>localPad->discontinuity && actor->second.positionRevision>localPad->requestRevision) {
        // Original Operate27 selects its counterpart from the server's room
        // unit lists. Learn only the actual own-unit relocation, not a guessed
        // pairing from MPQ presets or the nearest similarly looking object.
        const auto transfer=*localPad;const auto destination=*world.playerPosition;
        const auto key=std::tuple{transfer.area,transfer.identity,transfer.source.x,transfer.source.y};
        if(padObservations.size()>=512 && !padObservations.contains(key))
            throw std::runtime_error("Observed local-transport edge capacity exhausted");
        auto &record=padObservations[key];record.destination=destination;record.used=Clock::now();++record.visits;
        const auto memory=c.town.navigationMemory();
        if(memory.observedCells>padProgressCells || memory.targets>padProgressTargets) padTransfersWithoutDiscovery=0;
        else ++padTransfersWithoutDiscovery;
        padProgressCells=memory.observedCells;padProgressTargets=memory.targets;
        padArrival=destination;padArrivedAt=Clock::now();localPad.reset();cancelWalk(c);
        note("local-pad-arrival",std::to_string(transfer.source.x)+","+std::to_string(transfer.source.y)+" -> "+
            std::to_string(destination.x)+","+std::to_string(destination.y));
        return;
    }
    if(Clock::now()>=localPad->deadline)
        throw std::runtime_error("Submitted local teleport pad has no actual relocation result; operation is not replayed");
}
bool DebugAi::Impl::exploreLocalPad(Context &c) {
    const auto &view=c.session.read();const auto now=Clock::now();
    if(localPad) {
        if(localPad->waiting) {wait("local-pad-result","Waiting for the actual same-level relocation");return true;}
        const auto unit=view.world.units.find(localPad->unit);
        if(unit==view.world.units.end() || unit->second.assignmentRevision!=localPad->assignment || area!=localPad->area) {
            cancelWalk(c);localPad.reset();return false;
        }
        const auto source=localPad->source;
        if(now>=localPad->deadline) {
            recordFrontierAttempt(c,source,"failed","Observed local-pad approach exhausted its execution budget");
            cancelWalk(c);localPad.reset();return false;
        }
        if((Vec{float(source.x),float(source.y)}-c.position).length()>8) {
            if(approachPoint(c,source,"observed local teleport pad",6)) return true;
            recordFrontierAttempt(c,source,"unreachable","No current approach to the observed local pad");
            cancelWalk(c);localPad.reset();return false;
        }
        wait("local-pad-approach","Approaching the actually assigned local teleport pad");
        const Json intent{{"goal","unit"},{"unitType",localPad->unit.type},{"unitId",localPad->unit.id},
            {"interact",true},{"purpose","exploration-pad"}};
        if(walk(c,intent)) observeChild(c,goals[cursor]);
        return true;
    }
    const auto *map=c.town.map();const auto origin=c.town.read().origin;
    if(!map || !origin || !area) return false;
    const auto memory=c.town.navigationMemory();
    if(memory.observedCells>padProgressCells || memory.targets>padProgressTargets) padTransfersWithoutDiscovery=0;
    if(padTransfersWithoutDiscovery>=16)
        throw std::runtime_error("Local teleport exploration exhausted its no-discovery budget; retaining observed links and scene");
    const Vec offset{float(origin->x),float(origin->y)},from=c.position-offset+Vec{.5f,.5f};
    const auto grid=c.town.navigationGrid(view);
    const OnlineMapTarget *chosen=nullptr;float best=std::numeric_limits<float>::max();
    for(const auto &entry:c.town.read().mapTargets) {
        if(entry.interaction!=OnlineMapInteraction::TeleportPad) continue;
        const auto unit=view.world.units.find(entry.unit);if(unit==view.world.units.end() || !unit->second.classId) continue;
        const Vec at{float(entry.position.x),float(entry.position.y)};const float span=(at-c.position).length();
        if(frontierCoolingDown(entry.position,8) || (padArrival && now-padArrivedAt<std::chrono::seconds(5) &&
            (at-Vec{float(padArrival->x),float(padArrival->y)}).length()<8)) continue;
        const auto key=std::tuple{*area,*unit->second.classId,entry.position.x,entry.position.y};
        const auto observed=padObservations.find(key);
        if(observed!=padObservations.end() && now-observed->second.used<std::chrono::seconds(2)) continue;
        float score=span+(observed==padObservations.end()?0.f:1000.f+float(observed->second.visits)*100.f);
        if(!c.town.interactionReady(view,entry.unit,c.position)) {
            const auto path=c.walk.navigationSearch().path(grid,from,at-offset+Vec{.5f,.5f},true,playerMovement,true);
            // A disconnected platform can be reached with normal Teleport or
            // after further observation. Lack of a walking path is not proof
            // that an actually assigned transport object is unusable.
            if(path.empty() || !c.town.interactionReady(view,entry.unit,path.back()+offset-Vec{.5f,.5f})) score+=200;
        }
        if(score>=best) continue;
        best=score;chosen=&entry;
    }
    if(!chosen) return false;
    const auto &unit=view.world.units.at(chosen->unit);
    localPad=LocalPad{chosen->unit,unit.assignmentRevision,*area,*unit.classId,chosen->position};
    localPad->deadline=now+std::chrono::seconds(30);
    padProgressCells=memory.observedCells;padProgressTargets=memory.targets;
    note("local-pad-selected",std::to_string(chosen->position.x)+","+std::to_string(chosen->position.y));
    return exploreLocalPad(c);
}
} // namespace d2x
