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
    GeneratedArea result;
    result.request = request; result.origin = {float(snapshot.tileX * 5), float(snapshot.tileY * 5)};
    result.palette = level.palette;
    result.rooms = std::move(anchors);
    result.map = std::make_shared<const Map>(std::move(snapshot.map));
    return result;
}
} // namespace d2x
