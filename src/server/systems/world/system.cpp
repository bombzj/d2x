#include "system.hpp"
#include "server/player_store.hpp"
#include <algorithm>
namespace d2x::server::world {
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
        state_.residency.emplace(id, AreaLease{id, area.generation, Residency::Prepared});
    }
}
DomainResult<uint64_t> System::request(RegionId destination) {
    if (const auto *area = ports_.areas.find(destination)) return {DomainStatus::Applied, area->generation};
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
    ports_.areas.areas_.emplace(id, AreaState{std::move(prepared.definition)});
    state_.residency.emplace(id, AreaLease{id, 1, Residency::Prepared});
    state_.preparation.erase(pending); link();
    return {DomainStatus::Applied, std::monostate{}};
}
void System::fail(uint64_t requestId) {
    auto &pending = state_.preparation;
    const auto found = std::find_if(pending.begin(), pending.end(), [&](const auto &p) { return p.request == requestId; });
    if (found != pending.end()) { failed_[found->destination] = requestId; pending.erase(found); }
}
std::vector<RegionId> System::visible(PlayerId id) const {
    const auto *player = ports_.players.find(id); if (!player || !player->entered) return {};
    std::vector<RegionId> result{player->area};
    for (const auto &edge : ports_.areas.at(player->area).definition.boundaries)
        if (ports_.areas.find(edge.destination) && std::find(result.begin(), result.end(), edge.destination) == result.end()) result.push_back(edge.destination);
    return result;
}
StepStatus System::step(TickContext, FrameFacts &) {
    std::set<RegionId> resident;
    for (const auto &[id, player] : ports_.players.all()) {
        (void)id;
        if (!player.entered) continue;
        for (auto area : visible(player.player)) resident.insert(area);
        for (const auto &edge : ports_.areas.at(player.area).definition.boundaries) request(edge.destination);
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
    if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || !area || area->generation!=actor.areaGeneration || !p->persistent.waypoints.contains(destination)) return {DomainStatus::InvalidActor,{}};
    return request(destination);
}
}
