#include "map.hpp"
#include "map_assembly.hpp"
#include <algorithm>
#include <iostream>
namespace d2x {
Vec Map::actSpawn() const {
    for (const auto &layer : data.walls)
        for (size_t index = 0; index < layer.size(); ++index) {
            const auto &cell = layer[index];
            if (!cell.occupied() || (cell.orientation != 10 && cell.orientation != 11) ||
                ((cell.value >> 20) & 63) != 30 || ((cell.value >> 8) & 255) > 4)
                continue;
            const Vec marker{float(index % data.width * 5 + 3), float(index / data.width * 5 + 3)};
            const Vec arrival = grid.nearest(marker);
            if (!grid.walkable(arrival) || (arrival - marker).length() > 5)
                throw std::runtime_error("Invalid DS1 act spawn marker: " + path);
            return arrival;
        }
    throw std::runtime_error("Missing DS1 act spawn marker: " + path);
}
int Map::tileIndex(const MapCell &c, int x, int y) const {
    const auto &candidates = scopedLookup.at(c.libraryScope);
    auto it = candidates.find(c.key());
    if (it == candidates.end() || it->second.empty())
        return -1;
    // DT1 rarity is the native variant weight. Coordinate hashing only stabilizes
    // this preset viewer; it does not reproduce the original room seed sequence.
    uint64_t total = 0;
    for (int index : it->second)
        total += std::max(0, tiles[index]->rarity);
    if (!total)
        return it->second.front();
    uint64_t choice = (uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u) % total;
    for (int index : it->second) {
        auto weight = uint64_t(std::max(0, tiles[index]->rarity));
        if (choice < weight)
            return index;
        choice -= weight;
    }
    return it->second.back();
}
std::shared_ptr<const std::vector<Tile>> TileLibraryCache::load(const std::string &path) {
    auto key = normalize(path);
    auto found = libraries_.find(key);
    if (found != libraries_.end())
        return found->second;
    auto decoded = std::make_shared<const std::vector<Tile>>(decodeDt1(archives_.read(key)));
    libraries_.emplace(key, decoded);
    return decoded;
}
void Map::load(Archives &a, TileLibraryCache &cache, const MapRecipe &recipe) {
    const auto &ds1 = recipe.ds1;
    path = name = ds1;
    data = assembleMap(a, recipe);
    rooms.clear();
    warpArrivals.clear();
    for (const auto &piece : recipe.pieces)
        rooms.push_back({piece.x * 5, piece.y * 5, piece.width * 5, piece.height * 5, piece.populate});
    if (recipe.baseFloor)
        for (int y = 0; y < recipe.height; y += 8)
            for (int x = 0; x < recipe.width; x += 8) {
                bool authored = std::any_of(recipe.pieces.begin(), recipe.pieces.end(), [&](const auto &p) {
                    return x >= p.x && y >= p.y && x < p.x + p.width && y < p.y + p.height;
                });
                bool blank =
                    std::any_of(recipe.blankAreas.begin(), recipe.blankAreas.end(), [&](const auto &area) {
                        return x >= area.x && y >= area.y && x < area.x + area.width &&
                               y < area.y + area.height;
                    });
                if (!authored && !blank)
                    rooms.push_back({x * 5, y * 5, 40, 40, true});
            }
    tiles.clear();
    libraries.clear();
    lookup.clear();
    scopedLookup.clear();
    unresolved = 0;
    std::map<std::string, std::vector<int>> loaded;
    auto addScope = [&](const std::vector<std::string> &files) {
        auto &scope = scopedLookup.emplace_back();
        std::set<std::string> included;
        for (const auto &file : files) {
            auto name = normalize(file);
            if (!included.insert(name).second)
                continue;
            auto [entry, fresh] = loaded.try_emplace(name);
            if (fresh) {
                auto library = cache.load(file);
                for (const auto &tile : *library) {
                    int index = int(tiles.size());
                    entry->second.push_back(index);
                    lookup[tile.key()].push_back(index);
                    tiles.push_back(&tile);
                }
                libraries.push_back(std::move(library));
            }
            for (int index : entry->second)
                scope[tiles[index]->key()].push_back(index);
        }
    };
    addScope(recipe.tileLibraries);
    for (const auto &piece : recipe.pieces)
        addScope(piece.tileLibraries);
    if (recipe.fillBlanks)
        for (auto &cell : data.floors.front())
            if (!cell.occupied())
                cell.value = (30u << 20) | 0x80000002u;
    if (tiles.empty())
        throw std::runtime_error("No DT1 tiles for " + ds1);
    grid = Grid(data.width * 5, data.height * 5);
    std::fill(grid.blocked.begin(), grid.blocked.end(), 1);
    std::fill(grid.lightBlocked.begin(), grid.lightBlocked.end(), 1);
    for (int y = 0; y < data.height; y++)
        for (int x = 0; x < data.width; x++) {
            auto apply = [&](const MapCell &c, bool floor) {
                if (!c.occupied())
                    return;
                if ((c.orientation == 10 || c.orientation == 11) && ((c.value >> 20) & 63) >= 8)
                    return;
                int idx = tileIndex(c, x, y);
                if (idx < 0) {
                    unresolved++;
                    if (unresolved < 4) {
                        std::cerr << "Missing tile " << c.key() << " orientation " << c.orientation << " in "
                                  << ds1 << " at=" << x << ',' << y << " hidden=" << c.hidden() << '\n';
                        for (const auto &piece : recipe.pieces)
                            if (x >= piece.x && x <= piece.x + piece.width && y >= piece.y &&
                                y <= piece.y + piece.height)
                                std::cerr << "  preset=" << piece.preset << " variant=" << piece.variant
                                          << " source=" << piece.ds1 << '\n';
                    }
                    return;
                }
                const auto &t = *tiles[idx];
                if (c.orientation == 10 || c.orientation == 11 || c.orientation == 15)
                    return;
                for (int sy = 0; sy < 5; sy++)
                    for (int sx = 0; sx < 5; sx++) {
                        auto flag = t.flags[(4 - sy) * 5 + sx];
                        bool blocked = (flag & 1) || (c.value & (1u << 17));
                        auto index = (y * 5 + sy) * grid.width + x * 5 + sx;
                        auto &dest = grid.blocked[index];
                        auto &light = grid.lightBlocked[index];
                        if (floor)
                            dest = blocked ? 1 : 0;
                        else if (blocked)
                            dest = 1;
                        // DT1 bit 2 blocks LOS and light; bit 32 blocks light
                        // without blocking LOS. They must both affect lighting.
                        if (floor)
                            light = (flag & (2 | 32)) ? 1 : 0;
                        else if (flag & (2 | 32))
                            light = 1;
                    }
            };
            for (auto &l : data.floors)
                apply(l[y * data.width + x], true);
            for (auto &l : data.walls)
                apply(l[y * data.width + x], false);
            const auto &shadow = data.shadows[y * data.width + x];
            if (shadow.present() && tileIndex(shadow, x, y) < 0) {
                ++unresolved;
                if (unresolved < 4)
                    std::cerr << "Missing shadow " << shadow.key() << " at=" << x << ',' << y << " in " << ds1
                              << '\n';
            }
        }
    for (int y = 0; y < grid.height; y++)
        for (int x = 0; x < grid.width; x++) {
            int width = recipe.width ? recipe.width * 5 : grid.width;
            int height = recipe.height ? recipe.height * 5 : grid.height;
            bool opening =
                std::any_of(recipe.boundaries.begin(), recipe.boundaries.end(), [&](const auto &b) {
                    int t = b.side % 2 ? y : x;
                    int plane = b.coordinate(width / 5, height / 5) * 5;
                    int normal = b.side % 2 ? x : y;
                    bool edge = b.plane >= 0  ? normal >= plane - 2 && normal < plane + 2
                                : b.side == 1 ? x < 2
                                : b.side == 2 ? y < 2
                                : b.side == 3 ? x >= width - 2
                                              : y >= height - 2;
                    return edge && t >= b.start * 5 && t < b.end * 5;
                });
            if (x >= width || y >= height ||
                (!opening && (x < 2 || y < 2 || x >= width - 2 || y >= height - 2)))
                grid.blocked[y * grid.width + x] = grid.lightBlocked[y * grid.width + x] = 1;
        }
    if (unresolved)
        throw std::runtime_error("Unresolved DT1 cells in " + ds1 +
                                 "; map disabled, no substitute tiles generated");
    if (std::find(grid.blocked.begin(), grid.blocked.end(), 0) == grid.blocked.end())
        throw std::runtime_error("No walkable floor in " + ds1);
    spawn = grid.inspectionArrival();
    activation = RoomLayout(grid.width, grid.height, rooms);
    std::cout << "Map " << ds1 << ": " << data.width << "x" << data.height << ", " << tiles.size()
              << " tiles, " << unresolved << " unresolved cells\n";
}
} // namespace d2x
