#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/population/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/maps/system.hpp"
#include "world/native_room_activation.hpp"
#include "world/room_terrain.hpp"
#include <algorithm>
#include <limits>

namespace d2x::server::world {
// Decoded room inputs are immutable; each act owns its mutable DRLG and shared
// tile state. Collision and the separate active-room seed are produced at the
// actual create operation, never copied from an offline whole-level snapshot.
struct System::RoomRuntime {
    struct Room {
        RoomTerrainId key;
        RegionId area;
        size_t index{SIZE_MAX};
        RoomBounds bounds{};
        int act{};
        bool town{};
        std::vector<size_t> near;
        uint64_t creationOrder{};
    };
    struct Act {
        std::unique_ptr<RoomTerrainSession> terrain;
        std::unique_ptr<NativeRoomActivation> activation;
        std::optional<uint64_t> advancedTick;
        bool terrainDirty{true};
    };
    System &owner;
    std::vector<Room> rooms;
    std::map<std::pair<RegionId, size_t>, size_t> identities;
    std::map<std::pair<int,RoomTerrainId>,size_t> anchors;
    std::map<PlayerId, size_t> clients;
    std::map<PlayerId,std::vector<RoomView>> clientRooms;
    std::map<RoomView,size_t> roomClients;
    std::array<Act,5> acts;
    bool dirty{true};
    uint64_t currentTick{};
    std::map<RegionId,unsigned> inactiveLevels;
    std::map<PlayerId,RegionId> clientLevels;
    std::optional<uint64_t> levelCheckTick;
    bool awaitingInputs{};
    struct Retirement {
        size_t room{};
        uint64_t generation{},tick{};
        std::vector<EntityId> order;
        size_t next{};
    };
    std::optional<Retirement> retirement;
    explicit RoomRuntime(System &system) : owner(system) {}
    AreaState &area(RegionId id) { return owner.ports_.areas.areas_.at(id); }
    NativeRoomActivation &activation(size_t id) {return *acts.at(size_t(rooms.at(id).act)).activation;}
    RoomTerrainSession &terrain(size_t id) {return *acts.at(size_t(rooms.at(id).act)).terrain;}
    bool ensure(size_t id,unsigned depth) {
        std::map<size_t,unsigned> needed{{id,depth}};
        std::vector<size_t> queue{id};
        for(size_t at=0;at<queue.size();++at) {
            const auto current=queue[at];const auto remaining=needed.at(current);
            if(!remaining) continue;
            for(const auto near:rooms.at(current).near) {
                const auto found=needed.find(near);
                if(found==needed.end() || found->second<remaining-1) {
                    needed[near]=remaining-1;queue.push_back(near);
                }
            }
        }
        bool ready=true;
        for(const auto &[current,remaining]:needed) {
            (void)remaining;const auto &room=rooms.at(current);
            if(terrain(current).resident(room.key)) continue;
            const auto *source=owner.ports_.areas.find(room.area);
            const auto *root=owner.ports_.areas.find(rooms.at(id).area);
            owner.request(room.area,source?source->definition.pd2Map:root?root->definition.pd2Map:nullptr);
            ready=false;
        }
        if(!ready) awaitingInputs=true;
        return ready;
    }
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
        terrain(id).release(room.key);
        acts.at(size_t(room.act)).terrainDirty = true;
        if (room.index==SIZE_MAX) return;
        cells(room, [](Grid &grid, size_t cell) {
            grid.fullTerrainCollision[cell] = 0xffff;
            grid.terrainCollision[cell] = 0xff;
            grid.blocked[cell] = grid.lightBlocked[cell] = 1;
            if(cell<grid.missileCollision.size() && grid.missileCollision[cell]) {
                grid.missileCollision[cell]=0;++grid.missileCollisionRevision;
            }
            grid.nativeCollision.at(cell)=0xffff;++grid.nativeCollisionRevision;
        });
        auto &state = area(room.area);
        auto &population=state.definition.populationRooms.at(room.index);
        decltype(population.regions){}.swap(population.regions);
        decltype(population.cells){}.swap(population.cells);
        decltype(population.floors){}.swap(population.floors);
        state.rooms.at(room.index).loaded = false;
        state.rooms.at(room.index).random.reset();
        state.rooms.at(room.index).lastDead={};state.rooms.at(room.index).deathIndex=0;
        ++state.definition.collision.obstacleRevision;
    }
    void create(size_t id) {
        auto &room = rooms.at(id);
        terrain(id).create(room.key);
        acts.at(size_t(room.act)).terrainDirty = true;
        if(owner.ports_.areas.roomCreationOrder_==UINT64_MAX) throw std::overflow_error("Native room creation order exhausted");
        room.creationOrder=++owner.ports_.areas.roomCreationOrder_;
    }
    void project(size_t id,const RoomTerrainSample &sample) {
        const auto &room=rooms.at(id);
        auto &state=area(room.area);
        state.definition.collision.restoreStorage();
        auto &active=state.rooms.at(room.index);
        if (sample.width!=room.bounds.width || sample.height!=room.bounds.height ||
            sample.collision.size()!=size_t(sample.width)*sample.height)
            throw std::runtime_error("Materialized native room collision extent changed");
        size_t from{};bool changed{};
        cells(room,[&](Grid &grid,size_t cell) {
            const auto flags=sample.collision.at(from++);
            const auto previous=grid.fullTerrainCollision[cell];
            if(!active.loaded) grid.nativeCollision.at(cell)=flags|grid.objectCollision[cell]|grid.missileCollision[cell];
            else if(previous!=flags) grid.nativeCollision.at(cell)=uint16_t((grid.nativeCollision.at(cell)&~previous)|flags);
            changed|=grid.fullTerrainCollision[cell]!=flags;
            grid.fullTerrainCollision[cell]=flags;grid.terrainCollision[cell]=uint8_t(flags);
            grid.blocked[cell]=(flags&0x09)!=0;grid.lightBlocked[cell]=(flags&0x22)!=0;
        });
        if (!active.loaded) {
            active.loaded=true;active.creationOrder=room.creationOrder;
            if (active.randomRevision!=UINT64_MAX) {
                ++active.randomRevision;active.random=sample.initialActiveRandom;
            } else active.random.reset();
            const RoomView view{room.area,(int(state.definition.origin.x)+room.bounds.x)/5,
                (int(state.definition.origin.y)+room.bounds.y)/5};
            owner.ports_.areas.rememberCollisionRoom([areas=&owner.ports_.areas,spatial=&owner.ports_.spatial,view] {
                areas->restoreNativeRoomBase(view);spatial->restoreRoomFootprints(view);
            });
            owner.ports_.spatial.restoreRoomFootprints(view);
        }
        state.definition.populationRooms.at(room.index)=sample.population;
        if (changed) ++state.definition.collision.obstacleRevision;
    }
    void rebuild() {
        if (!dirty) return;
        // New resource handoffs extend the logical graph. Existing references,
        // prefetch cursor, tiles and gameplay random remain in the same DRLG.
        for (auto &[region,state] : owner.ports_.areas.areas_) {
            (void)region;
            if (!state.rooms.empty()) continue;
            const auto &prepared=state.definition.roomTerrain;
            if (!prepared || prepared->act()!=state.definition.act)
                throw std::runtime_error("Authority area lacks decoded native room inputs");
            auto &act=acts.at(size_t(prepared->act()));
            if (!act.terrain) act.terrain=prepared->instantiate();
            else act.terrain->include(*prepared);
            act.terrainDirty = true;
            for (const auto &link : prepared->rooms()) if (!anchors.contains({prepared->act(),link.id})) {
                anchors.emplace(std::pair{prepared->act(),link.id},rooms.size());
                Room room;room.key=link.id;room.area=RegionId(link.id.level);
                room.act=prepared->act();room.town=link.town;
                rooms.push_back(std::move(room));
            }
            for (const auto &link : prepared->rooms()) {
                if (link.near.empty()) continue;
                auto &room=rooms.at(anchors.at({prepared->act(),link.id}));std::vector<size_t> near;
                for (const auto other : link.near) near.push_back(anchors.at({prepared->act(),other}));
                if (!room.near.empty() && room.near!=near) throw std::runtime_error("Live native room graph changed");
                room.near=std::move(near);
            }
        }
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
                const RoomTerrainId key{int(region),(int(state.definition.origin.x)+bounds.x)/5,
                    (int(state.definition.origin.y)+bounds.y)/5};
                const auto id=anchors.at({state.definition.act,key});
                auto &room=rooms.at(id);room.index=index;room.bounds=bounds;
                identities.emplace(std::pair{region,index},id);
            }
            // Only authored room footprints own collision. Snapshot padding
            // must not shadow the adjacent level's real seam cells.
            std::fill(grid.fullTerrainCollision.begin(), grid.fullTerrainCollision.end(), 0xffff);
            std::fill(grid.terrainCollision.begin(), grid.terrainCollision.end(), 0xff);
            std::fill(grid.blocked.begin(), grid.blocked.end(), 1);
            std::fill(grid.lightBlocked.begin(), grid.lightBlocked.end(), 1);
            ++grid.obstacleRevision;
            state.definition.roomTerrain.reset();
        }
        for (auto &act : acts) if (act.terrain && !act.activation)
          act.activation = std::make_unique<NativeRoomActivation>(NativeRoomActivation::Operations{
            [this](size_t id) { return rooms.at(id).near; },
            [this](size_t id) { terrain(id).prepare(rooms.at(id).key); },
            [this](size_t id) { create(id); },
            [this](size_t id) { release(id); },
            [this](size_t id) { return tryRetire(id); },
            [this] {return retirement.has_value();}}, NativeRoomActivation::Side::Server);
        dirty = false;
        publish();
    }
    void publish() {
        for (auto &[id, state] : owner.ports_.areas.areas_)
            for (auto &room : state.rooms) { room.references = {}; room.status = 4; }
        for (const auto &act : acts) if (act.activation) for (const auto &[id, status] : act.activation->states()) {
            const auto &room = rooms.at(id);
            if (room.index==SIZE_MAX) continue;
            auto &state = area(room.area).rooms.at(room.index);
            state.references = status.references; state.status = status.status;
            // Reference/status publication keeps its original timing. Static
            // terrain changes only on lifecycle/include or explicit tomb edits.
            // Refresh the whole act after those operations: shared boundary
            // tiles and active-near lists can also change in existing rooms.
            if (status.active && (act.terrainDirty || !state.loaded)) {
                std::vector<std::pair<int,int>> opened;
                if (owner.openedTombs_.contains(room.area)) {
                    const auto &definition=area(room.area).definition;
                    for (const auto &cell : definition.openedTombWall) {
                        const std::pair tile{(int(cell.index%definition.collision.width)+int(definition.origin.x))/5,
                            (int(cell.index/definition.collision.width)+int(definition.origin.y))/5};
                        if (std::find(opened.begin(),opened.end(),tile)==opened.end()) opened.push_back(tile);
                    }
                }
                const auto sample=act.terrain->sample(room.key,false,opened);
                if (!sample) throw std::runtime_error("Active native room lacks collision");
                project(id,*sample);
            }
        }
        for (auto &act : acts) act.terrainDirty = false;
    }
    bool change(std::optional<size_t> previous,std::optional<size_t> next) {
        if(next && previous!=next && !ensure(*next,3)) return false;
        if (previous && next && rooms.at(*previous).act==rooms.at(*next).act)
            activation(*next).changeClientRoom(previous,next);
        else {
            if (next) activation(*next).changeClientRoom({},next);
            if (previous) activation(*previous).changeClientRoom(previous,{});
        }
        return true;
    }
    void releaseLevels(uint64_t tick) {
        // DRLG_ChangeRoom resets both the old/new level and their Vis levels.
        const auto touch=[&](RegionId region) {
            inactiveLevels[region]=10;
            const auto *source=owner.ports_.areas.find(region);if(!source) return;
            const auto &act=acts.at(size_t(source->definition.act));if(!act.terrain) return;
            for(const auto other:act.terrain->visibleLevels(int(region))) inactiveLevels[RegionId(other)]=10;
        };
        std::map<PlayerId,RegionId> current;
        std::set<RegionId> active;
        for(const auto &[player,id]:clients) {
            const auto &room=rooms.at(id);current.emplace(player,room.area);active.insert(room.area);
            for(const auto other:terrain(id).visibleLevels(int(room.area))) active.insert(RegionId(other));
            const auto old=clientLevels.find(player);
            if(old==clientLevels.end() || old->second!=room.area) {
                if(old!=clientLevels.end()) touch(old->second);
                touch(room.area);
            }
        }
        for(const auto &[player,region]:clientLevels) if(!current.contains(player)) touch(region);
        clientLevels=std::move(current);
        if(tick%11 || levelCheckTick==tick) return;
        levelCheckTick=tick;
        // The descriptor walk is separate from active-room retirement. It
        // never changes population progress, unit IDs or saved inactive nodes.
        std::vector<std::pair<int,int>> levels;
        std::set<std::pair<int,int>> seen;
        for(size_t id=0;id<rooms.size();++id) {
            const auto &room=rooms[id];const auto key=std::pair{room.act,room.key.level};
            if(terrain(id).resident(room.key) && seen.insert(key).second) levels.insert(levels.begin(),key);
        }
        // Act traversal is ascending; each DRLG prepends newly allocated levels.
        std::stable_sort(levels.begin(),levels.end(),[](const auto &a,const auto &b){return a.first<b.first;});
        for(const auto &[actId,level]:levels) {
            const auto region=RegionId(level);
            if(active.contains(region)) continue;
            auto &delay=inactiveLevels[region];if(delay) {--delay;continue;}
            auto &act=acts.at(size_t(actId));
            if(std::any_of(owner.state_.preparation.begin(),owner.state_.preparation.end(),[&](const auto &p){return p.destination==region;})) continue;
            const auto dependencies=act.terrain->releaseDependencies(level);
            bool busy=false;
            for(const auto key:dependencies) {
                const auto id=anchors.at({actId,key});const auto state=act.activation->states().find(id);
                if(state!=act.activation->states().end() && (state->second.status<=3 || state->second.active)) {busy=true;break;}
            }
            if(busy) {delay=10;continue;}
            for(const auto key:dependencies) {
                const auto id=anchors.at({actId,key});
                if(key.level==level && !act.activation->discardUnreferenced(id))
                    throw std::logic_error("Inactive level regained room references during release");
            }
            const auto installed=owner.ports_.areas.areas_.find(region);
            if(installed!=owner.ports_.areas.areas_.end() && installed->second.terrainGeneration==UINT64_MAX)
                throw std::overflow_error("Native terrain generation exhausted");
            act.terrain->releaseLevel(level);
            if(installed!=owner.ports_.areas.areas_.end()) {
                installed->second.definition.collision.releaseStorage();
                ++installed->second.terrainGeneration;
            }
        }
    }
    void changeClientRooms(PlayerId player,const std::vector<RoomView> &next,uint64_t tick) {
        auto &previous=clientRooms[player];
        for(const auto room:next) if(std::find(previous.begin(),previous.end(),room)==previous.end()) {
            auto &count=roomClients[room];
            if(!count) owner.ports_.monsters.roomClientsChanged(room,true,tick);
            ++count;
        }
        for(const auto room:previous) if(std::find(next.begin(),next.end(),room)==next.end()) {
            const auto found=roomClients.find(room);
            if(found==roomClients.end() || !found->second) throw std::logic_error("Unbalanced native room client registration");
            if(found->second==1) {
                owner.ports_.monsters.roomClientsChanged(room,false,tick);roomClients.erase(found);
            } else --found->second;
        }
        previous=next;
    }
    bool retain(size_t id, uint64_t tick) const {
        const auto &room = rooms.at(id);
        const auto &act=acts.at(size_t(room.act));
        if (room.town || int(room.area) == 120)
            for (const auto &[key, state] : act.activation->states())
                if (rooms.at(key).area == room.area && state.status <= 1) return true;
        if (room.index==SIZE_MAX) return false;
        const auto &runtime=owner.ports_.areas.at(room.area).rooms.at(room.index);
        if(!runtime.controllerRetainers.empty()) {
            owner.ports_.maps.sceneRoomRelease(room.area,room.index);return true;
        }
        if(runtime.portalRetained) return true;
        const auto &source = owner.ports_.areas.at(room.area).definition;
        const auto contains = [&](RegionId region, Vec point) { return locate(region, point) == id; };
        const auto &ports = owner.ports_;
        const auto portal = [&](const auto &value) {
            const auto matches=[&](const std::optional<RoomView> &view,RegionId region,Vec point) {
                if(!view) return contains(region,point);
                return view->area==room.area && view->x==(int(source.origin.x)+room.bounds.x)/5 &&
                    view->y==(int(source.origin.y)+room.bounds.y)/5;
            };
            return matches(value.fieldRoom,value.field,value.fieldPosition) || matches(value.townRoom,value.town,value.townPosition);
        };
        for (const auto &[key, value] : ports.travel.read().portals) { (void)key; if (portal(value)) return true; }
        for (const auto &[key, value] : ports.travel.read().specialPortals) { (void)key; if (portal(value)) return true; }
        // Complete committed work before retiring collision. This also covers
        // output backpressure: pending damage/loot cannot be lost on unload.
        for (const auto &[key, value] : ports.monsters.read().actors) {
            if(value.skillReservation && contains(value.area,value.skillReservation->position)) return true;
            if (!contains(value.area, value.position)) continue;
            if (value.owner || value.busyUntil > tick || value.poison || value.wound || value.skillMotion ||
                value.knockbackGoal || (!value.alive() && !value.rewardComplete) || ports.skills.busy(key, tick)) return true;
        }
        for (const auto &[key, value] : ports.missiles.read().missiles) { (void)key; if (contains(value.area, value.position)) return true; }
        for (const auto &[key, value] : ports.objects.read().objects) {
            (void)key;
            if (contains(value.area, value.position) && (value.pending || value.specialPending || value.urnPending || value.lootAt || value.trapAt ||
                (value.rule.initialization==59 && value.hazardAt))) return true;
        }
        for (const auto &[key, value] : ports.loot.read().pending) { (void)key; if (contains(value.request.source.region, value.request.position)) return true; }
        return false;
    }
    bool tryRetire(size_t id) {
        const auto &room=rooms.at(id);
        if(room.index==SIZE_MAX) return true;
        const auto &area=owner.ports_.areas.at(room.area);
        if(!retirement) {
            if(!owner.ports_.monsters.canCompressRoom(room.area,room.index,currentTick)) return false;
            const RoomView view{room.area,(int(area.definition.origin.x)+room.bounds.x)/5,
                (int(area.definition.origin.y)+room.bounds.y)/5};
            retirement=Retirement{id,area.generation,currentTick,owner.ports_.areas.roomUnitOrder(view)};
        }
        if(retirement->room!=id) return false;
        if(retirement->generation!=area.generation || retirement->tick!=currentTick)
            throw std::logic_error("Room retirement crossed its generation or frame");
        auto &pending=*retirement;
        while(pending.next<pending.order.size()) {
            const auto unit=pending.order[pending.next];
            if(owner.ports_.monsters.find(unit)) {
                if(!owner.ports_.monsters.compressUnit(room.area,room.index,currentTick,unit)) return false;
            } else if(owner.ports_.items.read().world.items.contains(unit)) {
                auto plan=owner.ports_.items.prepareRoomCompression(room.area,room.index,currentTick,unit);
                if(!plan) return false;
                owner.ports_.items.retireCompressedItem(*plan.value,unit);
                owner.ports_.items.publishRoomCompression(std::move(*plan.value));
            } else if(owner.ports_.objects.read().objects.contains(unit)) {
                auto plan=owner.ports_.objects.prepareRoomCompression(room.area,room.index,currentTick,unit);
                if(!plan || !owner.ports_.objects.validateRoomCompression(*plan.value)) return false;
                owner.ports_.objects.retireCompressedObject(*plan.value,unit);
                owner.ports_.objects.publishRoomCompression(std::move(*plan.value));
            } else if(owner.ports_.areas.hasRoomUnit(unit)) {
                throw std::logic_error("Unsupported live unit in retiring room");
            }
            ++pending.next;
        }
        retirement.reset();return true;
    }
};
System::System(Ports ports) : ports_(ports), rooms_(std::make_unique<RoomRuntime>(*this)) {}
System::~System() = default;
bool System::discardPreparedRooms(std::span<const RegionId> regions) {
    const auto selected=[&](const RoomRuntime::Room &room) {
        return std::find(regions.begin(),regions.end(),room.area)!=regions.end();
    };
    // Only unpublished, unreferenced rooms may be discarded. Logical decoded
    // room inputs can remain cached in the act; their live area binding cannot.
    for(size_t id=0;id<rooms_->rooms.size();++id) {
        const auto &room=rooms_->rooms[id];if(!selected(room)) continue;
        const auto &act=rooms_->acts.at(size_t(room.act));if(!act.activation) continue;
        const auto state=act.activation->states().find(id);
        if(state!=act.activation->states().end() &&
           (state->second.status<=1 || std::any_of(state->second.references.begin(),state->second.references.end(),[](auto count){return count!=0;})))
            return false;
    }
    for(size_t id=0;id<rooms_->rooms.size();++id) {
        auto &room=rooms_->rooms[id];if(!selected(room)) continue;
        auto &act=rooms_->acts.at(size_t(room.act));
        if(act.activation && !act.activation->discardUnreferenced(id)) return false;
        if(room.index!=SIZE_MAX) rooms_->identities.erase({room.area,room.index});
        room.index=SIZE_MAX;
    }
    return true;
}
void System::invalidateRooms() { rooms_->dirty = true; }
bool System::roomInputsReady(RegionId region) const {
    const auto *area=ports_.areas.find(region);if(!area) return false;
    if(area->definition.roomTerrain) return true;
    bool found=false;
    for(size_t id=0;id<rooms_->rooms.size();++id) {
        const auto &room=rooms_->rooms[id];if(room.area!=region) continue;
        found=true;if(!rooms_->terrain(id).resident(room.key)) return false;
    }
    return found;
}
bool System::restoreRoomInputs(const AreaDefinition &prepared) {
    auto &area=ports_.areas.areas_.at(prepared.id);
    const auto &old=area.definition;
    if(!prepared.roomTerrain || prepared.act!=old.act || prepared.origin.x!=old.origin.x || prepared.origin.y!=old.origin.y ||
       prepared.initialLevelRandom!=old.initialLevelRandom || prepared.roomNear!=old.roomNear ||
       prepared.collision.width!=old.collision.width || prepared.collision.height!=old.collision.height ||
       prepared.activation.rooms().size()!=old.activation.rooms().size()) return false;
    for(size_t i=0;i<old.activation.rooms().size();++i) {
        const auto &a=old.activation.rooms()[i],&b=prepared.activation.rooms()[i];
        if(a.x!=b.x || a.y!=b.y || a.width!=b.width || a.height!=b.height) return false;
    }
    auto &act=rooms_->acts.at(size_t(old.act));
    if(!act.terrain) return false;
    act.terrain->include(*prepared.roomTerrain);
    act.terrainDirty=true;
    return true;
}
void System::refreshRoomTerrain() {
    rooms_->rebuild();
    for (auto &act : rooms_->acts) act.terrainDirty = true;
    rooms_->publish();
}
void System::activate(RegionId region, Vec point) {
    activateAct(region);
    rooms_->rebuild();
    if (const auto id = rooms_->locate(region, point)) {
        // Original StreamRoomAtCoords initializes the destination only.
        // ChangeClientRoom supplies the actual reference propagation later.
        if(!rooms_->ensure(*id,0)) return;
        rooms_->activation(*id).initialize(*id);
        rooms_->publish();
    }
}
void System::changeClientRoom(PlayerId id) {
    rooms_->rebuild();
    const auto *player=ports_.players.find(id);
    const auto old=rooms_->clients.find(id);
    const std::optional<size_t> previous=old==rooms_->clients.end()?std::nullopt:std::optional<size_t>{old->second};
    const auto next=player && player->entered?rooms_->locate(player->area,player->position+Vec{.5f,.5f}):std::nullopt;
    if(!rooms_->change(previous,next)) return;
    if (next) rooms_->clients.insert_or_assign(id,*next);else rooms_->clients.erase(id);
    rooms_->publish();
}
bool System::retiringRoom() const {return rooms_->retirement.has_value();}
bool System::awaitingRoomInputs() const {return rooms_->awaitingInputs;}
void System::updateRooms(TickContext tick) {
    rooms_->rebuild();
    rooms_->awaitingInputs=false;
    rooms_->currentTick=tick.tick;
    if(rooms_->retirement && !rooms_->activation(rooms_->retirement->room).retire(rooms_->retirement->room)) return;
    auto previous = rooms_->clients;
    std::set<PlayerId> departed;
    for(const auto &[player,registered]:rooms_->clientRooms) {(void)registered;departed.insert(player);}
    for (const auto &[id, player] : ports_.players.all()) {
        if (!player.entered) continue;
        const auto next = rooms_->locate(player.area, player.position + Vec{.5f, .5f});
        if (!next) throw std::runtime_error("Entered player has no owning native room");
        const auto old = rooms_->clients.find(id);
        if(!rooms_->change(old == rooms_->clients.end() ? std::nullopt : std::optional<size_t>{old->second}, next)) return;
        rooms_->clients.insert_or_assign(id, *next);
        rooms_->publish();
        std::vector<RoomView> registered;
        for(const auto adjacent:rooms_->rooms.at(*next).near) {
            const auto &room=rooms_->rooms.at(adjacent);
            if (room.index==SIZE_MAX) continue;
            const auto &area=ports_.areas.at(room.area);
            if(area.rooms.at(room.index).loaded) registered.push_back({room.area,
                (int(area.definition.origin.x)+room.bounds.x)/5,(int(area.definition.origin.y)+room.bounds.y)/5});
        }
        rooms_->changeClientRooms(id,registered,tick.tick);departed.erase(id);
        previous.erase(id);
    }
    for (const auto &[id, room] : previous) {
        rooms_->change(room, {});
        rooms_->clients.erase(id);
    }
    for(const auto id:departed) {rooms_->changeClientRooms(id,{},tick.tick);rooms_->clientRooms.erase(id);}
    for (auto &act : rooms_->acts) if (act.activation && act.advancedTick!=tick.tick) {
        act.activation->update(tick.tick, [&](size_t room) { return rooms_->retirement.has_value() || rooms_->retain(room, tick.tick); });
        if(rooms_->retirement) return;
        act.advancedTick=tick.tick;
    }
    rooms_->publish();
    rooms_->releaseLevels(tick.tick);
    for (const auto &act : rooms_->acts) if (act.activation) for (const auto &[id, state] : act.activation->states()) {
        if (state.status >= 3) continue;
        const auto &room = rooms_->rooms.at(id);
        const auto *area=ports_.areas.find(room.area);
        auto map=area?area->definition.pd2Map:nullptr;
        if(!area) for(const auto near:room.near) {
            const auto *source=ports_.areas.find(rooms_->rooms.at(near).area);
            if(source && source->definition.pd2Map) {map=source->definition.pd2Map;break;}
        }
        const auto prepare=[&](RegionId destination) {
            if(ports_.areas.find(destination)) return;
            // DRLG neighbour prefetch precedes the player's exit command.
            // A child prepared without its map would later reject that same
            // immutable context and could never be entered through the warp.
            request(destination,map);
        };
        prepare(room.area);
        for (const auto near : room.near)
            prepare(rooms_->rooms.at(near).area);
    }
}
bool System::clientRoomsReady(PlayerId id) const {
    if(rooms_->dirty) return false;
    const auto *player=ports_.players.find(id);
    if(!player || !player->entered) return false;
    const auto current=rooms_->locate(player->area,player->position+Vec{.5f,.5f});
    const auto registered=rooms_->clients.find(id);
    if(!current || registered==rooms_->clients.end() || registered->second!=*current) return false;
    const auto &near=rooms_->rooms.at(*current).near;
    if(near.empty()) return false;
    // D2Common_10073 requires the complete active near list, not just the
    // currently installed/visible subset and not an entire generated level.
    for(const auto adjacent:near) {
        const auto &room=rooms_->rooms.at(adjacent);
        const auto *area=ports_.areas.find(room.area);
        if(!area || room.index==SIZE_MAX || room.index>=area->rooms.size() || !area->rooms[room.index].loaded) return false;
        if(!ports_.population.roomReady(room.area,room.index)) return false;
    }
    return true;
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
