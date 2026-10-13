#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/loot/system.hpp"
#include "world/native_room_activation.hpp"
#include <algorithm>
#include <limits>

namespace d2x::server::world {
// The kernel retains compact immutable terrain instructions and authoritative
// unit state. Active collision cells are materialized only for leased rooms.
// No DT1, MPQ, rendering map or file handle crosses this boundary.
struct System::RoomRuntime {
    struct Run {
        uint32_t count{};
        uint16_t flags{};
        uint8_t blocked{}, light{}, terrain{};
        bool same(const Run &other) const {
            return flags == other.flags && blocked == other.blocked && light == other.light && terrain == other.terrain;
        }
    };
    struct Room {
        RegionId area;
        size_t index{};
        RoomBounds bounds;
        std::vector<Run> terrain;
        std::vector<size_t> near;
    };
    System &owner;
    std::vector<Room> rooms;
    std::map<std::pair<RegionId, size_t>, size_t> identities;
    std::map<PlayerId, size_t> clients;
    std::unique_ptr<NativeRoomActivation> activation;
    bool dirty{true};
    explicit RoomRuntime(System &system) : owner(system) {}
    AreaState &area(RegionId id) { return owner.ports_.areas.areas_.at(id); }
    std::optional<size_t> locate(RegionId id, Vec point) const {
        const auto *source = owner.ports_.areas.find(id);
        if (!source) return {};
        const auto &layout = source->definition.activation;
        const auto *room = layout.room(point);
        if (!room) return {};
        const auto found = identities.find({id, size_t(room - layout.rooms().data())});
        return found == identities.end() ? std::nullopt : std::optional<size_t>{found->second};
    }
    template<class Operation> void cells(const Room &room, Operation operation) {
        auto &grid = area(room.area).definition.collision;
        for (int y = room.bounds.y; y < room.bounds.y + room.bounds.height; ++y)
            for (int x = room.bounds.x; x < room.bounds.x + room.bounds.width; ++x)
                operation(grid, size_t(y) * grid.width + x);
    }
    void release(size_t id) {
        const auto &room = rooms.at(id);
        cells(room, [](Grid &grid, size_t cell) {
            grid.fullTerrainCollision[cell] = 0xffff;
            grid.terrainCollision[cell] = 0xff;
            grid.blocked[cell] = grid.lightBlocked[cell] = 1;
        });
        auto &state = area(room.area);
        state.rooms.at(room.index).loaded = false;
        ++state.definition.collision.obstacleRevision;
    }
    void create(size_t id) {
        const auto &room = rooms.at(id);
        auto &state = area(room.area);
        if (state.rooms.at(room.index).loaded) return;
        size_t run = 0;
        uint32_t remaining = room.terrain.at(0).count;
        cells(room, [&](Grid &grid, size_t cell) {
            const auto &value = room.terrain.at(run);
            grid.fullTerrainCollision[cell] = value.flags;
            grid.blocked[cell] = value.blocked;
            grid.lightBlocked[cell] = value.light;
            grid.terrainCollision[cell] = value.terrain;
            if (!--remaining && ++run < room.terrain.size()) remaining = room.terrain[run].count;
        });
        state.rooms.at(room.index).loaded = true;
        if (owner.openedTombs_.contains(room.area)) for (const auto &cell : state.definition.openedTombWall) {
            auto &grid = state.definition.collision;
            const int x = int(cell.index % grid.width), y = int(cell.index / grid.width);
            if (x < room.bounds.x || x >= room.bounds.x + room.bounds.width ||
                y < room.bounds.y || y >= room.bounds.y + room.bounds.height) continue;
            grid.blocked[cell.index] = cell.blocked; grid.lightBlocked[cell.index] = cell.light;
            grid.terrainCollision[cell.index] = cell.terrain; grid.fullTerrainCollision[cell.index] = cell.full;
        }
        ++state.definition.collision.obstacleRevision;
    }
    void rebuild() {
        if (!dirty) return;
        for (auto &[region, state] : owner.ports_.areas.areas_) {
            const auto &layout = state.definition.activation.rooms();
            if (!state.rooms.empty()) continue;
            if (layout.empty()) throw std::runtime_error("Authority area has no original rooms");
            auto &grid = state.definition.collision;
            state.rooms.resize(layout.size());
            for (size_t index = 0; index < layout.size(); ++index) {
                const auto &bounds = layout[index];
                if (bounds.x < 0 || bounds.y < 0 || bounds.width <= 0 || bounds.height <= 0 ||
                    bounds.x + bounds.width > grid.width || bounds.y + bounds.height > grid.height)
                    throw std::runtime_error("Authority room lies outside its level");
                Room room{region, index, bounds, {}, {}};
                cells(room, [&](Grid &source, size_t cell) {
                    Run value{1, source.fullTerrainCollision[cell], source.blocked[cell],
                        source.lightBlocked[cell], source.terrainCollision[cell]};
                    if (!room.terrain.empty() && room.terrain.back().same(value) && room.terrain.back().count < UINT32_MAX)
                        ++room.terrain.back().count;
                    else room.terrain.push_back(value);
                });
                identities.emplace(std::pair{region, index}, rooms.size());
                rooms.push_back(std::move(room));
            }
            // Only authored room footprints own collision. Snapshot padding
            // must not shadow the adjacent level's real seam cells.
            std::fill(grid.fullTerrainCollision.begin(), grid.fullTerrainCollision.end(), 0xffff);
            std::fill(grid.terrainCollision.begin(), grid.terrainCollision.end(), 0xff);
            std::fill(grid.blocked.begin(), grid.blocked.end(), 1);
            std::fill(grid.lightBlocked.begin(), grid.lightBlocked.end(), 1);
            ++grid.obstacleRevision;
        }
        for (auto &room : rooms) {
            room.near.clear();
            const auto &source = area(room.area).definition;
            for (const auto &link : source.roomNear.at(room.index)) {
                const auto *target = owner.ports_.areas.find(link.area);
                if (!target) continue;
                const Vec point{float(link.x * 5) - target->definition.origin.x,
                    float(link.y * 5) - target->definition.origin.y};
                const auto adjacent = locate(link.area, point);
                if (!adjacent) throw std::runtime_error("Prepared near-room anchor has no owner");
                room.near.push_back(*adjacent);
            }
        }
        // Changing the allocated graph requires rebuilding its derived counts.
        // Never decrement references through a different near-room graph.
        activation = std::make_unique<NativeRoomActivation>(NativeRoomActivation::Operations{
            [this](size_t id) { return rooms.at(id).near; },
            [](size_t) {},
            [this](size_t id) { create(id); },
            [this](size_t id) { release(id); }}, NativeRoomActivation::Side::Server);
        for (size_t id = 0; id < rooms.size(); ++id)
            if (area(rooms[id].area).rooms.at(rooms[id].index).loaded) activation->initialize(id);
        clients.clear(); dirty = false;
    }
    void publish() {
        for (auto &[id, state] : owner.ports_.areas.areas_)
            for (auto &room : state.rooms) { room.references = {}; room.status = 4; }
        for (const auto &[id, status] : activation->states()) {
            const auto &room = rooms.at(id);
            auto &state = area(room.area).rooms.at(room.index);
            state.references = status.references; state.status = status.status;
        }
    }
    bool retain(size_t id, uint64_t tick) const {
        const auto &room = rooms.at(id);
        const auto &source = owner.ports_.areas.at(room.area).definition;
        if (source.town || int(room.area) == 120)
            for (const auto &[key, state] : activation->states())
                if (rooms.at(key).area == room.area && state.status <= 1) return true;
        const auto contains = [&](RegionId region, Vec point) { return locate(region, point) == id; };
        const auto &ports = owner.ports_;
        const auto portal = [&](const auto &value) {
            return contains(value.field, value.fieldPosition) || contains(value.town, value.townPosition);
        };
        for (const auto &[key, value] : ports.travel.read().portals) { (void)key; if (portal(value)) return true; }
        for (const auto &[key, value] : ports.travel.read().specialPortals) { (void)key; if (portal(value)) return true; }
        // Complete committed work before retiring collision. This also covers
        // output backpressure: pending damage/loot cannot be lost on unload.
        for (const auto &[key, value] : ports.monsters.read().actors) {
            if (!contains(value.area, value.position)) continue;
            if (value.owner || value.busyUntil > tick || value.poison || value.wound || value.skillMotion ||
                value.knockbackGoal || (value.life <= 0 && !value.rewardComplete) || ports.skills.busy(key, tick)) return true;
        }
        for (const auto &[key, value] : ports.missiles.read().missiles) { (void)key; if (contains(value.area, value.position)) return true; }
        for (const auto &[key, value] : ports.objects.read().objects) {
            (void)key;
            if (contains(value.area, value.position) && (value.pending || value.specialPending || value.lootAt || value.trapAt || value.hazardAt)) return true;
        }
        for (const auto &[key, value] : ports.loot.read().pending) { (void)key; if (contains(value.request.source.region, value.request.position)) return true; }
        return false;
    }
};
System::System(Ports ports) : ports_(ports), rooms_(std::make_unique<RoomRuntime>(*this)) {}
System::~System() = default;
void System::invalidateRooms() { rooms_->dirty = true; }
void System::activate(RegionId region, Vec point) {
    rooms_->rebuild();
    if (const auto id = rooms_->locate(region, point)) {
        for (const auto near : rooms_->rooms.at(*id).near) rooms_->activation->initialize(near);
        rooms_->activation->initialize(*id);
        rooms_->publish();
    }
}
void System::updateRooms(TickContext tick) {
    rooms_->rebuild();
    auto previous = rooms_->clients;
    for (const auto &[id, player] : ports_.players.all()) {
        if (!player.entered) continue;
        const auto next = rooms_->locate(player.area, player.position + Vec{.5f, .5f});
        if (!next) throw std::runtime_error("Entered player has no owning native room");
        const auto old = rooms_->clients.find(id);
        rooms_->activation->changeClientRoom(old == rooms_->clients.end() ? std::nullopt : std::optional<size_t>{old->second}, next);
        rooms_->clients.insert_or_assign(id, *next);
        previous.erase(id);
    }
    for (const auto &[id, room] : previous) {
        rooms_->activation->changeClientRoom(room, {});
        rooms_->clients.erase(id);
    }
    rooms_->activation->update(tick.tick, [&](size_t room) { return rooms_->retain(room, tick.tick); });
    rooms_->publish();
    for (const auto &[id, state] : rooms_->activation->states()) {
        if (state.status >= 3) continue;
        const auto &room = rooms_->rooms.at(id);
        const auto &definition = ports_.areas.at(room.area).definition;
        for (const auto &near : definition.roomNear.at(room.index))
            if (!ports_.areas.find(near.area)) request(near.area);
    }
}
bool System::visible(PlayerId id, RegionId region, Vec point) const {
    const auto *player = ports_.players.find(id);
    return player && player->entered && ports_.areas.nearby(player->area, player->position + Vec{.5f, .5f}, region, point);
}
std::vector<RoomView> System::visibleRooms(PlayerId id) const {
    const auto *player = ports_.players.find(id);
    if (!player || !player->entered) return {};
    std::vector<RoomView> result;
    const auto &source = ports_.areas.at(player->area).definition;
    if (const auto *observer = source.activation.room(player->position + Vec{.5f, .5f}); observer) {
        const auto index = size_t(observer - source.activation.rooms().data());
        for (const auto &link : source.roomNear.at(index)) {
            const auto *destination = ports_.areas.find(link.area);
            if (!destination || destination->definition.act != source.act) continue;
            const Vec point{float(link.x * 5) - destination->definition.origin.x,
                float(link.y * 5) - destination->definition.origin.y};
            if (ports_.areas.loaded(link.area, point)) result.push_back(link);
        }
        return result;
    }
    return result;
}
}
