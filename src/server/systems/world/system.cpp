#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include <algorithm>
namespace d2x::server::world {
void System::prepareActTime(int act) {
    if(act<0 || act>=5) throw std::invalid_argument("Invalid prepared act time");
    auto &time=ports_.areas.actTimes_.at(size_t(act));
    if(!time) {time.emplace();if(act==1 && sunDark_) setActSunEclipse(time->value,true);}
}
void System::activateAct(RegionId region) {
    prepareActTime(ports_.areas.at(region).definition.act);
}
StepStatus System::advanceEnvironment(TickContext tick) {
    if(environmentNotice_) return StepStatus::Blocked;
    if(!environmentFrame_ || tick.tick>*environmentFrame_) {environmentFrame_=tick.tick;environmentAct_=0;}
    else if(tick.tick<*environmentFrame_) throw std::logic_error("Native environment frame moved backwards");
    while(environmentAct_<ports_.areas.actTimes_.size()) {
        const auto act=environmentAct_;
        auto &time=ports_.areas.actTimes_[act];
        if(!time) {++environmentAct_;continue;}
        auto value=time->value;
        const bool notification=advanceActEnvironment(value,int(act));
        if(notification && time->revision==UINT64_MAX) throw std::overflow_error("Native environment revision exhausted");
        time->value=value;++environmentAct_;
        if(notification) {
            ++time->revision;environmentNotice_=EnvironmentNotice{{int(act),value.cycle,value.ticks,value.eclipse},time->revision};
            return StepStatus::Blocked;
        }
    }
    return StepStatus::Complete;
}
void System::acknowledgeEnvironment(int act,uint64_t revision) {
    if(!environmentNotice_ || environmentNotice_->value.act!=act || environmentNotice_->revision!=revision)
        throw std::logic_error("Native environment acknowledgement lost its notice");
    environmentNotice_.reset();
}
void System::sunEclipse(bool dark) {
    sunDark_=dark;if(!ports_.areas.actTimes_[1]) return;
    auto &time=*ports_.areas.actTimes_[1];
    if(time.value.eclipse==dark) return;
    if(time.revision==UINT64_MAX) throw std::overflow_error("Native eclipse revision exhausted");
    setActSunEclipse(time.value,dark);++time.revision;
}
DomainResult<> System::moveQuestNpc(RegionId region,EntityId id,Vec position,Vec destination,bool escaped) {
    const auto found=ports_.areas.areas_.find(region);if(found==ports_.areas.areas_.end()) return {DomainStatus::Unavailable,{}};
    for(const auto &npc:found->second.definition.npcs) if(npc.id==id && npc.rule.code=="act5pow")
        return ports_.monsters.moveNpc(id,region,position,destination,escaped);
    return {DomainStatus::Stale,{}};
}
DomainResult<EntityId> System::admitQuestNpc(RegionId region,std::string_view code,Vec position,int searchRadius) {
    const auto found=ports_.areas.areas_.find(region);if(found==ports_.areas.areas_.end()) return {DomainStatus::Unavailable,{}};
    auto &area=found->second.definition;
    for(const auto &npc:area.npcs) if(npc.rule.code==code)
        return ports_.monsters.find(npc.id)?DomainResult<EntityId>{DomainStatus::Applied,npc.id}:DomainResult<EntityId>{DomainStatus::Stale,{}};
    const auto prepared=area.questNpcs.find(code);
    if(prepared==area.questNpcs.end()) return {DomainStatus::Unavailable,{}};
    const auto point=area.collision.nativeSpawn(position,searchRadius,prepared->second.movement);
    if(!point) return {DomainStatus::Conflict,{}};
    if(ports_.ids.cursor()>=UINT32_MAX || ports_.monsters.read().actors.size()>=65536) return {DomainStatus::Capacity,{}};
    const auto id=ports_.ids.allocate();
    area.npcs.push_back({id,*point,prepared->second});ports_.monsters.admitNpc(id,region,*point,prepared->second);return {DomainStatus::Applied,id};
}
void System::registerAreaUnits(RegionId region) {
    ports_.areas.initializeUnitRooms(region);
    for(const auto &npc:ports_.areas.at(region).definition.npcs)
        ports_.monsters.admitNpc(npc.id,region,npc.spawnPosition,npc.rule);
}
void System::assign(AreaDefinition &area) {
    for (auto &exit : area.exits) exit.id = ports_.ids.allocate();
    for (auto &npc : area.npcs) npc.id = ports_.ids.allocate();
    for (auto &object : area.objects) {
        const auto previous = object.id; object.id = ports_.ids.allocate();
        for (auto &obstacle : area.collision.obstacles) if (obstacle.id == previous) obstacle.id = object.id;
    }
    if (ports_.ids.cursor() > UINT32_MAX) throw std::runtime_error("Native world identity capacity exhausted");
}
void System::initialize() {
    for (auto &[id, area] : ports_.areas.areas_) {
        assign(area.definition);
        registerAreaUnits(id);
        state_.residency.emplace(id, AreaLease{id, area.generation, Residency::Prepared});
    }
}
DomainResult<uint64_t> System::request(RegionId destination) {
    if (const auto *area = ports_.areas.find(destination)) {activateAct(destination);return {DomainStatus::Applied, area->generation};}
    if (failed_.contains(destination)) return {DomainStatus::Unavailable, {}};
    for (const auto &pending : state_.preparation) if (pending.destination == destination) return {DomainStatus::Applied, pending.request};
    if (state_.preparation.size() >= 16 || nextRequest_ == UINT64_MAX) return {DomainStatus::Capacity, {}};
    const auto id = nextRequest_;
    state_.preparation.push_back({destination, id, ports_.settings});
    ++nextRequest_;
    return {DomainStatus::Applied, id};
}
DomainResult<uint64_t> System::requestArea(const ActorContext &actor, RegionId destination) {
    const auto *player = ports_.players.find(actor.player);
    const auto *source = ports_.areas.find(actor.area);
    if (!player || player->actor != actor.actor || player->area != actor.area || !source || source->generation != actor.areaGeneration)
        return {DomainStatus::InvalidActor, {}};
    const auto &definition = source->definition;
    if (!std::any_of(definition.exits.begin(), definition.exits.end(), [&](const auto &exit) { return exit.destination == destination; }) &&
        !std::any_of(definition.boundaries.begin(), definition.boundaries.end(), [&](const auto &edge) { return edge.destination == destination; }))
        return {DomainStatus::InvalidRequest, {}};
    return request(destination);
}
void System::link() {
    for (auto &[id, area] : ports_.areas.areas_) {
        auto &grid = area.definition.collision; grid.neighbours.clear();
        for (const auto &edge : area.definition.boundaries) {
            const auto *other = ports_.areas.find(edge.destination);
            if (!other || other->definition.act != area.definition.act) continue;
            const auto &back = other->definition.boundaries;
            if (!std::any_of(back.begin(), back.end(), [&](const auto &b) { return b.destination == id && b.side == (edge.side + 2) % 4; })) continue;
            const Vec offset = area.definition.origin - other->definition.origin;
            grid.neighbours.push_back({&other->definition.collision, int(offset.x), int(offset.y), edge.side, edge.plane, edge.start, edge.end});
        }
    }
}
DomainResult<> System::install(PreparedArea prepared) {
    const auto pending = std::find_if(state_.preparation.begin(), state_.preparation.end(), [&](const auto &p) { return p.request == prepared.request; });
    if (pending == state_.preparation.end() || pending->destination != prepared.definition.id || ports_.areas.find(prepared.definition.id))
        return {DomainStatus::Stale, {}};
    AreaStore::validate(prepared.definition);
    assign(prepared.definition);
    const auto id = prepared.definition.id;
    ports_.areas.areas_.emplace(id, AreaState{std::move(prepared.definition), 1, {}});
    registerAreaUnits(id);
    state_.residency.emplace(id, AreaLease{id, 1, Residency::Prepared});
    state_.preparation.erase(pending); link(); invalidateRooms();
    return {DomainStatus::Applied, std::monostate{}};
}
void System::fail(uint64_t requestId) {
    auto &pending = state_.preparation;
    const auto found = std::find_if(pending.begin(), pending.end(), [&](const auto &p) { return p.request == requestId; });
    if (found != pending.end()) { failed_[found->destination] = requestId; pending.erase(found); }
}
void System::openTombWall(RegionId id) {
    openedTombs_.insert(id);
    auto &grid=ports_.areas.areas_.at(id).definition.collision;
    for(const auto &cell:ports_.areas.areas_.at(id).definition.openedTombWall) {
        if (!ports_.areas.loaded(id, {float(cell.index % grid.width), float(cell.index / grid.width)})) continue;
        grid.blocked[cell.index]=cell.blocked;grid.lightBlocked[cell.index]=cell.light;
        grid.terrainCollision[cell.index]=cell.terrain;
        if(!grid.fullTerrainCollision.empty()) grid.fullTerrainCollision[cell.index]=cell.full;
    }
    ++grid.obstacleRevision;
}
std::vector<RegionId> System::visible(PlayerId id) const {
    const auto *player = ports_.players.find(id); if (!player || !player->entered) return {};
    std::vector<RegionId> result{player->area};
    for (const auto &room : visibleRooms(id))
        if (std::find(result.begin(), result.end(), room.area) == result.end()) result.push_back(room.area);
    return result;
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    updateRooms(tick);
    std::set<RegionId> resident;
    for (const auto &[id, player] : ports_.players.all()) {
        (void)id;
        if (!player.entered) continue;
        for (auto area : visible(player.player)) resident.insert(area);

    }
    for (auto &[id, lease] : state_.residency) lease.residency = resident.contains(id) ? Residency::Active : Residency::Sleeping;
    return state_.preparation.empty() ? StepStatus::Complete : StepStatus::Blocked;
}
}

namespace d2x::server::world {
DomainResult<uint64_t> System::requestTown(const ActorContext &actor) {
    const auto *player = ports_.players.find(actor.player); const auto *area = ports_.areas.find(actor.area);
    if (!player || !area || player->actor != actor.actor || player->area != actor.area) return {DomainStatus::InvalidActor, {}};
    return request(area->definition.townRegion);
}
}

namespace d2x::server::world { void System::objectCollision(RegionId id, std::vector<Grid::Obstacle> values) { ports_.areas.areas_.at(id).definition.collision.setObstacles(std::move(values)); } }

namespace d2x::server::world {
DomainResult<uint64_t> System::requestWaypoint(const ActorContext &actor,RegionId destination) {
    const auto *p=ports_.players.find(actor.player); const auto *area=ports_.areas.find(actor.area);
    if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || !area || area->generation!=actor.areaGeneration) return {DomainStatus::InvalidActor,{}};
    if(!p->rules.character || !p->rules.character->waypointIndices.contains(destination) || !p->persistent.waypoints.contains(destination)) return {DomainStatus::InvalidRequest,{}};
    return request(destination);
}
}
