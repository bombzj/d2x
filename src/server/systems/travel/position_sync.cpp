#include "system.hpp"
#include "server/player_store.hpp"
#include "server/movement.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/trade/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::travel {
namespace {
// PlrMsg::Rcv0x5F uses WALL | NOPLAYER | OBJECT for reachability,
// then SUnit::sub_6FCBDFE0 uses the ordinary player mask for placement.
constexpr MovementCollisionRule synchronizationPath{0x409,2};
constexpr uint64_t attemptLifetime=90*25;
bool recent(uint64_t frame,uint64_t now) {return frame && now>=frame && now-frame<=attemptLifetime;}
bool exactPath(const Grid &grid, Vec from, Vec to) {
    constexpr Vec center{.5f, .5f};
    const auto route = grid.nativeUnitPath(from + center, to + center, 15, 77, synchronizationPath);
    return !route.empty() && int(std::floor(route.back().x)) == int(std::floor(to.x + .5f)) &&
        int(std::floor(route.back().y)) == int(std::floor(to.y + .5f));
}
}

DomainResult<> System::synchronize(const ActorContext &actor,Vec reported,bool uninterruptible) {
    const auto *found=ports_.players.find(actor.player);
    const auto *source=ports_.areas.find(actor.area);
    if(!found || !source || !found->entered || found->actor!=actor.actor || found->area!=actor.area)
        return {DomainStatus::InvalidActor,{}};
    if(source->generation!=actor.areaGeneration) return {DomainStatus::Stale,{}};
    if(found->persistent.player.hp<=0) return {DomainStatus::Applied,std::monostate{}};
    if(!std::isfinite(reported.x) || !std::isfinite(reported.y) ||
       std::abs(reported.x)>65535 || std::abs(reported.y)>65535) return {DomainStatus::InvalidRequest,{}};
    const auto correction=[&]() -> DomainResult<> {
        const auto output=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},
            {RepositionFact{actor.actor,actor.area,found->position}}});
        return output?DomainResult<>{DomainStatus::Applied,std::monostate{}}:DomainResult<>{output.status,{}};
    };
    const int distance=missileDistance(found->position,reported);
    if(distance<5) return {DomainStatus::Applied,std::monostate{}};
    const auto previous=state_.synchronizations.find(actor.player);
    const bool synchronized=previous!=state_.synchronizations.end() && previous->second.until>actor.tick;
    if(synchronized || distance<15 || distance>45) {
        if(found->movementTarget && (!found->route.empty() || found->moving)) return {DomainStatus::Applied,std::monostate{}};
        if(ports_.trade.find(actor.player)) return correction();
        auto &player=ports_.players.players_.at(actor.player);
        const MovementCommand movement{MovementAction::Move,reported,player.runningNow};
        cancel(actor.player);
        const auto grid=ports_.spatial.navigationGrid(player.area,player.actor);
        const auto moved=applyMovement(player,*source,movement,grid);
        if(moved!=CommandStatus::Applied) return correction();
        player.locomotionSequence=actor.sequence;
        return {DomainStatus::Applied,std::monostate{}};
    }
    if(uninterruptible || ports_.trade.find(actor.player) || !found->rules.skills) return correction();
    RegionId region=actor.area;
    const AreaState *destination=source;
    Vec at=reported;
    if (!source->definition.activation.room(reported + Vec{.5f,.5f})) {
        destination = nullptr;
        Vec origin; const auto navigation = source->definition.collision.linkedNavigation(origin);
        for (const auto &[id, candidate] : ports_.areas.all()) {
            if (std::none_of(navigation.navigationSources.begin(), navigation.navigationSources.end(),
                [&](const auto &entry) { return entry.grid == &candidate.definition.collision; })) continue;
            const Vec point = reported + source->definition.origin - candidate.definition.origin;
            if (!candidate.definition.activation.room(point + Vec{.5f,.5f})) continue;
            if (destination) return correction();
            region = id; destination = &candidate; at = point;
        }
    }
    if (!destination || !ports_.areas.nearby(actor.area, found->position + Vec{.5f,.5f}, region, at + Vec{.5f,.5f})) return correction();
    // PlrMsg 5F uses the original 77-step wall-follow path in the same room
    // collision domain. Never search hand-picked seam entrances or warp nearby.
    const auto placementGrid=ports_.spatial.navigationGrid(region,actor.actor);
    const auto sourceGrid=ports_.spatial.navigationGrid(actor.area,actor.actor);
    if (!placementGrid.walkable(at + Vec{.5f,.5f}, playerMovement) ||
        !exactPath(sourceGrid, found->position, reported)) return correction();
    const int state=found->rules.skills->syncWarped.id;
    if(state<0 || state>=255) return {DomainStatus::Unavailable,{}};
    auto next=previous==state_.synchronizations.end()?PositionSynchronization{}:previous->second;
    for(auto &frame:next.attempts) if(!recent(frame,actor.tick)) frame=actor.tick;
    uint64_t random=ports_.random;
    uint64_t delay=125;
    if(ports_.settings.singlePlayer && std::all_of(next.attempts.begin(),next.attempts.end(),[&](auto frame){return recent(frame,actor.tick);})) {
        const auto roll=limitedRandom(random,100);
        delay=roll<50?1500:roll<75?3000:4500;
    }
    next.until=actor.tick+delay;
    std::deque<Vec> route;
    if(!found->route.empty()) {
        Vec goal=found->route.back()+source->definition.origin-destination->definition.origin;
        constexpr Vec center{.5f,.5f};
        route=placementGrid.nativePlayerPath(at+center,goal+center,playerMovement);
        for(auto &point:route) point=point-center;
    }
    std::vector<EventBatch> events;
    if(region!=actor.area) events.push_back({0,actor.tick,{}, {AudienceKind::Player,actor.player,region},
        {TravelFact{actor.player,actor.actor,actor.area,region,destination->generation,at,true}}});
    events.push_back({0,actor.tick,{}, {AudienceKind::Area,{},region},
        {RepositionFact{actor.actor,region,at,false},StateFact{actor.actor,0,region,state,true}}});
    const auto output=ports_.events.publishGroup(std::move(events));
    if(!output) return {output.status,{}};
    ports_.npc.close(actor.player);ports_.inventory.close(actor.player);
    state_.waypoints.erase(actor.player);cancel(actor.player);
    auto &player=ports_.players.players_.at(actor.player);
    player.area=region;player.position=at;player.route=std::move(route);
    player.pathStep.reset();
    player.movementTarget.reset();player.movementArrival.reset();
    player.routeDestination=player.route.empty()?at:player.route.back();player.routeObstacles=destination->definition.collision.obstacleRevision;
    player.moving=!player.route.empty();player.locomotionSequence=actor.sequence;
    ++player.teleportRevision;
    player.lastTeleportPosition=at+destination->definition.origin;
    state_.synchronizations[actor.player]=next;ports_.random=random;
    return {DomainStatus::Applied,std::monostate{}};
}

StepStatus System::expireSynchronizations(TickContext tick) {
    bool blocked=false;
    for(auto entry=state_.synchronizations.begin();entry!=state_.synchronizations.end();) {
        const auto *player=ports_.players.find(entry->first);
        if(!player || !player->entered) {entry=state_.synchronizations.erase(entry);continue;}
        auto &sync=entry->second;
        if(sync.until && (sync.until<=tick.tick || player->persistent.player.hp<=0)) {
            const int state=player->rules.skills->syncWarped.id;
            if(ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},player->area},
                {StateFact{player->actor,0,player->area,state,false}}})) sync.until=0;
            else blocked=true;
        }
        ++entry;
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
