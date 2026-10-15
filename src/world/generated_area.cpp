#include "generated_area.hpp"
#include <stdexcept>

namespace d2x {
struct AreaGenerator::Impl {
    AreaGenerationRequest request;
    std::shared_ptr<const WorldCatalog> catalogSource;
    TileLibraryCache libraries;
    NativeMapGenerator generator;
    std::vector<size_t> rooms;
    std::vector<std::pair<int, int>> anchors;
    size_t remaining{};
    std::optional<GeneratedArea> ready;
    double milliseconds{};
    Impl(Archives &archives, AreaGenerationRequest value)
        : request(value), catalogSource(sharedWorldCatalog(archives, value.difficulty)),
          libraries(archives), generator(archives, *catalogSource, libraries, value.act, value.seed, value.difficulty) {
        if (catalogSource->level(request.level).act != request.act)
            throw std::runtime_error("Area belongs to another act");
        rooms = generator.levelRooms(request.level);
        remaining = rooms.size(); anchors.reserve(remaining);
    }
    GeneratedArea finish() {
        const auto &catalog = *catalogSource;
        const auto &level = catalog.level(request.level);
        auto snapshot = generator.snapshot(request.level, false);
        snapshot.map.grid.fullTerrainCollision = std::move(snapshot.collision);
        // snapshot() carries native terrain rooms; the host also needs their exact
        // subcell footprints and MPQ Populate flags for interest and population.
        for (const auto &room : snapshot.map.terrain.rooms)
            snapshot.map.rooms.push_back({room.x * 5, room.y * 5, room.width * 5, room.height * 5,
                !room.preset || catalog.presets().at(room.preset).populate,
                level.generation==GenerationKind::Maze || (room.preset && (room.flags&0x00080000)!=0)});
        if (snapshot.map.rooms.empty()) throw std::runtime_error("Prepared native map lacks authored room footprints");
        snapshot.map.activation = RoomLayout(snapshot.map.grid.width, snapshot.map.grid.height, snapshot.map.rooms);
        GeneratedArea result;
        result.request = request; result.origin = {float(snapshot.tileX * 5), float(snapshot.tileY * 5)};
        result.palette = level.palette;
        result.staffTomb = generator.layout().staffTomb;
        result.recipe = generator.recipe(request.level);
        result.rooms = std::move(anchors);
        for (const auto id : rooms) {
            const auto &room = generator.tiles().rooms().at(id);
            bool protectedRoom=(room.room.flags&0x00030000u)!=0;
            for(size_t slot=0;slot<room.warpSlots.size() && !protectedRoom;++slot)
                protectedRoom=(room.room.flags&(0x10u<<slot))!=0 && room.warpSlots[slot]!=-1;
            if(protectedRoom)
                result.populationWarpCenters.push_back(Vec{float((room.room.x+room.room.width/2)*5),
                    float((room.room.y+room.room.height/2)*5)}-result.origin);
            RoomPopulationLayout logical;
            logical.x=room.room.x;logical.y=room.room.y;
            logical.width=room.room.width;logical.height=room.room.height;
            logical.single=room.singleLogicalRegion;logical.cells=room.logicalCells;
            for(const auto floor:room.floors) {
                const auto &tile=generator.tiles().tiles().at(floor);
                logical.floors.push_back({tile.x,tile.y,tile.tile?tile.tile->materialFlags:uint16_t{}});
            }
            for(const auto &region:room.logicalRegions)
                logical.regions.push_back({region.index,region.left,region.top,region.right,region.bottom,region.node});
            result.populationRooms.emplace(std::pair{room.room.x,room.room.y},std::move(logical));
            if(room.initialActiveRandom)
                result.initialRoomRandom.emplace(std::pair{room.room.x,room.room.y},*room.initialActiveRandom);
            auto &near = result.nearRooms[{room.room.x, room.room.y}];
            for (const auto adjacent : room.near) {
                const auto &other = generator.tiles().rooms().at(adjacent);
                near.push_back({other.level, other.room.x, other.room.y});
            }
        }
        result.map = std::make_shared<const Map>(std::move(snapshot.map));
        return result;
    }
};
AreaGenerator::AreaGenerator(Archives &archives, AreaGenerationRequest request) {
    if (request.act < 0 || request.act > 4 || request.difficulty < 0 || request.difficulty > 2)
        throw std::runtime_error("Invalid area generation request");
    const auto started = std::chrono::steady_clock::now();
    impl_ = std::make_unique<Impl>(archives, request);
    impl_->milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}
AreaGenerator::~AreaGenerator() = default;
bool AreaGenerator::advance(std::chrono::steady_clock::time_point deadline) {
    auto &state = *impl_;
    if (state.ready) return true;
    const auto started = std::chrono::steady_clock::now();
    if (started >= deadline) return false;
    if (state.remaining) {
        // Preserve reverse allocation/reveal order, including all native RNG
        // draws; a yield only separates operations, never replays one.
        do {
            const auto room = state.generator.tiles().rooms().at(state.rooms[state.remaining - 1]).room;
            state.generator.reveal(state.request.level, room.x, room.y);
            state.anchors.emplace_back(room.x, room.y);
            --state.remaining;
        } while (state.remaining && std::chrono::steady_clock::now() < deadline);
    } else state.ready = state.finish();
    state.milliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    if (state.ready) state.ready->preparationMilliseconds = state.milliseconds;
    return state.ready.has_value();
}
GeneratedArea AreaGenerator::take() {
    if (!impl_ || !impl_->ready) throw std::logic_error("Area preparation is not complete");
    auto result = std::move(*impl_->ready);
    impl_.reset();
    return result;
}
GeneratedArea generateArea(Archives &archives, AreaGenerationRequest request) {
    AreaGenerator generation(archives, request);
    while (!generation.advance(std::chrono::steady_clock::time_point::max())) {}
    return generation.take();
}
} // namespace d2x
