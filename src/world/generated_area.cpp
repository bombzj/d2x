#include "generated_area.hpp"
#include <stdexcept>

namespace d2x {
GeneratedArea generateArea(Archives &archives, AreaGenerationRequest request) {
    if (request.act < 0 || request.act > 4 || request.difficulty < 0 || request.difficulty > 2)
        throw std::runtime_error("Invalid area generation request");
    WorldCatalog catalog(archives, request.difficulty);
    const auto &level = catalog.level(request.level);
    if (level.act != request.act) throw std::runtime_error("Area belongs to another act");
    TileLibraryCache libraries(archives);
    NativeMapGenerator generator(archives, catalog, libraries, request.act, request.seed, request.difficulty);
    // Use the same reveal order delivered in native GS 07 packets. Offline
    // completeLevel() eagerly initializes a different room lifecycle.
    const auto rooms = generator.levelRooms(request.level);
    std::vector<std::pair<int, int>> anchors;
    for (auto entry = rooms.rbegin(); entry != rooms.rend(); ++entry) {
        const auto room = generator.tiles().rooms().at(*entry).room;
        anchors.emplace_back(room.x, room.y);
        generator.reveal(request.level, room.x, room.y);
    }
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
    result.map = std::make_shared<const Map>(std::move(snapshot.map));
    return result;
}
} // namespace d2x
