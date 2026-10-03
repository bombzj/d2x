#include "map.hpp"
#include "map_assembly.hpp"
#include "core/random.hpp"
#include <limits>
#include <algorithm>
#include <iostream>
namespace d2x {
bool Map::openTombWall(Vec position) {
    if (!tombWalls.empty()) return true;
    const int column = int(std::floor(position.x / 5));
    const int row = int(std::floor(position.y / 5));
    if (column < 1 || row < 1 || column >= data.width - 1 || row >= data.height - 1) return false;
    std::vector<std::pair<int, int>> opening;
    for (int vertical = row - 1; vertical <= row + 1; ++vertical)
        for (int horizontal = column - 1; horizontal <= column + 1; ++horizontal)
            if (std::any_of(data.walls.begin(), data.walls.end(), [&](const auto &layer) {
                const auto &cell = layer[size_t(vertical) * data.width + horizontal];
                return cell.occupied() && cell.orientation >= 1 && cell.orientation <= 7 &&
                    ((cell.value >> 20) & 63) == 8 && ((cell.value >> 8) & 255) <= 1;
            })) opening.emplace_back(horizontal, vertical);
    if (opening.empty()) {
        const size_t entrance = size_t(row) * data.width + column;
        const bool eastWall = std::any_of(data.walls.begin(), data.walls.end(), [&](const auto &layer) {
            const auto &cell = layer[entrance];
            return cell.occupied() && cell.orientation == 1 && ((cell.value >> 20) & 63) == 6 &&
                (((cell.value >> 8) & 255) == 8 || ((cell.value >> 8) & 255) == 9);
        });
        if (eastWall)
            for (int vertical = row - 1; vertical <= row; ++vertical)
                if (std::any_of(data.walls.begin(), data.walls.end(), [&](const auto &layer) {
                    const auto &cell = layer[size_t(vertical) * data.width + column];
                    return cell.occupied() && cell.orientation == 1 && ((cell.value >> 20) & 63) == 6 &&
                        (((cell.value >> 8) & 255) == 8 || ((cell.value >> 8) & 255) == 9);
                })) opening.emplace_back(column, vertical);
    }
    if (opening.empty()) return false;
    for (const auto &[wallColumn, wallRow] : opening) {
        const size_t cellIndex = size_t(wallRow) * data.width + wallColumn;
        TombWall saved;
        saved.x = wallColumn;
        saved.y = wallRow;
        for (auto &layer : data.walls) {
            auto &cell = layer[cellIndex];
            saved.walls.push_back(cell);
            const auto main = (cell.value >> 20) & 63;
            const auto sub = (cell.value >> 8) & 255;
            if ((main == 8 && sub <= 1 && cell.orientation >= 1 && cell.orientation <= 7) ||
                (main == 6 && (sub == 8 || sub == 9) && cell.orientation == 1)) cell = {};
        }
        for (int vertical = 0; vertical < 5; ++vertical)
            for (int horizontal = 0; horizontal < 5; ++horizontal) {
                const size_t index = size_t(wallRow * 5 + vertical) * grid.width + wallColumn * 5 + horizontal;
                saved.collision[size_t(vertical * 5 + horizontal)] = grid.terrainCollision[index];
                uint8_t flags = 0;
                auto merge = [&](const MapCell &cell) {
                    if (!cell.occupied() || ((cell.orientation == 10 || cell.orientation == 11) &&
                        (cell.hidden() || ((cell.value >> 20) & 63) >= 8))) return;
                    const int tile = tileIndex(cell, wallColumn, wallRow);
                    if (tile < 0) return;
                    flags |= tiles[size_t(tile)]->flags[size_t((4 - vertical) * 5 + horizontal)];
                    if (cell.orientation == 3) {
                        auto companion = cell;
                        companion.orientation = 4;
                        const int corner = tileIndex(companion, wallColumn, wallRow);
                        if (corner >= 0) flags |= tiles[size_t(corner)]->flags[size_t((4 - vertical) * 5 + horizontal)];
                    }
                    if (cell.value & (1u << 16)) flags |= 0x04;
                    if (cell.value & (1u << 17)) flags |= 0x01;
                    if ((cell.value & (1u << 28)) || (cell.orientation >= 8 && cell.orientation <= 11)) flags |= 0x10;
                };
                for (const auto &layer : data.floors) merge(layer[cellIndex]);
                for (const auto &layer : data.walls) merge(layer[cellIndex]);
                grid.terrainCollision[index] = flags;
                grid.blocked[index] = (flags & 0x09) != 0;
                grid.lightBlocked[index] = (flags & 0x22) != 0;
            }
        tombWalls.push_back(std::move(saved));
    }
    ++grid.obstacleRevision;
    return true;
}
void Map::restoreTombWall() {
    if (tombWalls.empty()) return;
    for (const auto &saved : tombWalls) {
        const size_t cellIndex = size_t(saved.y) * data.width + saved.x;
        for (size_t layer = 0; layer < data.walls.size(); ++layer) data.walls[layer][cellIndex] = saved.walls[layer];
        for (int vertical = 0; vertical < 5; ++vertical)
            for (int horizontal = 0; horizontal < 5; ++horizontal) {
                const size_t index = size_t(saved.y * 5 + vertical) * grid.width + saved.x * 5 + horizontal;
                const auto flags = saved.collision[size_t(vertical * 5 + horizontal)];
                grid.terrainCollision[index] = flags;
                grid.blocked[index] = (flags & 0x09) != 0;
                grid.lightBlocked[index] = (flags & 0x22) != 0;
            }
    }
    tombWalls.clear();
    ++grid.obstacleRevision;
}
Vec Map::actSpawn() const {
    for (unsigned markerType : {30u, 33u})
        for (const auto &layer : data.walls)
            for (size_t index = 0; index < layer.size(); ++index) {
                const auto &cell = layer[index];
                if (!cell.occupied() || (cell.orientation != 10 && cell.orientation != 11) ||
                    ((cell.value >> 20) & 63) != markerType ||
                    (markerType == 30 && ((cell.value >> 8) & 255) > 4))
                    continue;
                const Vec marker{float(index % data.width * 5 + 3), float(index / data.width * 5 + 3)};
                const Vec arrival = grid.nearest(marker, playerMovement);
                if (!grid.walkable(arrival, playerMovement) || (arrival - marker).length() > 5)
                    throw std::runtime_error("Invalid DS1 act spawn marker: " + path);
                return arrival;
            }
    throw std::runtime_error("Missing DS1 act spawn marker: " + path);
}
int Map::tileIndex(const MapCell &c, int x, int y) const {
    auto found = tileChoices.find({x, y, c.libraryScope, c.key()});
    return found == tileChoices.end() ? -1 : found->second;
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
void Map::load(Archives &a, TileLibraryCache &cache, const MapRecipe &recipe, uint32_t seed) {
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
    tileChoices.clear();
    auto tileRandom = initialRandom(seed);
    auto chooseTile = [&](const MapCell &cell, int x, int y) {
        auto key = std::tuple{x, y, cell.libraryScope, cell.key()};
        if (tileChoices.contains(key)) return;
        const auto &scope = scopedLookup.at(cell.libraryScope);
        auto found = scope.find(cell.key());
        if ((found == scope.end() || found->second.empty()) && cell.hidden()) {
            const auto fallback = scope.find(10u);
            if (fallback != scope.end() && !fallback->second.empty()) {
                tileChoices.emplace(key, fallback->second.front());
                return;
            }
        }
        if (found == scope.end() || found->second.empty()) return;
        uint64_t total = 0;
        for (int index : found->second) total += std::max(0, tiles[index]->rarity);
        if (total > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("DT1 rarity total exceeds native random range");
        // DRLGROOMTILE_GetTileCache: weighted D2Seed roll, or first entry if all weights are zero.
        auto choice = limitedRandom(tileRandom, uint32_t(total));
        int selected = found->second.front();
        if (total)
            for (int index : found->second) {
                const auto weight = uint32_t(std::max(0, tiles[index]->rarity));
                if (choice < weight) { selected = index; break; }
                choice -= weight;
            }
        tileChoices.emplace(key, selected);
    };
    for (int y = 0; y < data.height; ++y)
        for (int x = 0; x < data.width; ++x) {
            auto choose = [&](MapCell cell) {
                if (!cell.occupied()) return;
                chooseTile(cell, x, y);
                if (cell.orientation == 3) {
                    cell.orientation = 4;
                    chooseTile(cell, x, y);
                }
            };
            for (const auto &layer : data.floors) choose(layer[y * data.width + x]);
            for (const auto &layer : data.walls) choose(layer[y * data.width + x]);
            choose(data.shadows[y * data.width + x]);
        }
    grid = Grid(data.width * 5, data.height * 5);
    // Native room grids start clear and OR all tile layers (D2Collision.cpp).
    // Gaps between assembled rooms have no collision grid and stop missiles;
    // a missing floor inside a real room is not itself a missile barrier.
    if (!recipe.pieces.empty()) {
        std::fill(grid.terrainCollision.begin(), grid.terrainCollision.end(), 0x27);
        for (const auto &room : rooms)
            for (int y = std::max(0, room.y); y < std::min(grid.height, room.y + room.height); ++y)
                for (int x = std::max(0, room.x); x < std::min(grid.width, room.x + room.width); ++x)
                    grid.terrainCollision[size_t(y) * grid.width + x] = 0;
    }
    for (int y = 0; y < data.height; y++)
        for (int x = 0; x < data.width; x++) {
            auto apply = [&](const MapCell &c) {
                if (!c.occupied())
                    return;
                if ((c.orientation == 10 || c.orientation == 11) && ((c.value >> 20) & 63) >= 8)
                    return;
                if ((c.orientation == 10 || c.orientation == 11) && c.hidden())
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
                auto applyCollision = [&](const Tile &tile) {
                    // DT1 0x04 = COLLIDE_MISSILE_BARRIER. DS1 bFillLOS (bit 16)
                    // maps through MAPTILE_FILL_LOS to that same flag; neither
                    // DT1 0x01 nor DS1 bUnwalkable (bit 17) blocks mode 3 missiles.
                    for (int sy = 0; sy < 5; ++sy)
                        for (int sx = 0; sx < 5; ++sx) {
                            auto flags = tile.flags[(4 - sy) * 5 + sx];
                            if (c.value & (1u << 16)) flags |= 0x04;
                            if (c.value & (1u << 17)) flags |= 0x01;
                            // MAPTILE_WALL_EXIT -> COLLIDE_PRESET for native
                            // placement masks (not ordinary walking/missiles).
                            if ((c.value & (1u << 28)) || (c.orientation >= 8 && c.orientation <= 11))
                                flags |= 0x10;
                            const size_t index = size_t(y * 5 + sy) * grid.width + x * 5 + sx;
                            grid.terrainCollision[index] |= flags;
                        }
                };
                applyCollision(t);
                if (c.orientation == 3) {
                    // DRLGROOMTILE_InitWallTileData creates both halves. Match
                    // the companion tile already used by the world renderer.
                    auto companion = c;
                    companion.orientation = 4;
                    const int corner = tileIndex(companion, x, y);
                    if (corner < 0)
                        throw std::runtime_error("Missing DT1 corner collision tile in " + ds1);
                    applyCollision(*tiles[corner]);
                }
            };
            for (auto &l : data.floors)
                apply(l[y * data.width + x]);
            for (auto &l : data.walls)
                apply(l[y * data.width + x]);
            const auto &shadow = data.shadows[y * data.width + x];
            if (shadow.present() && tileIndex(shadow, x, y) < 0) {
                ++unresolved;
                if (unresolved < 4)
                    std::cerr << "Missing shadow " << shadow.key() << " at=" << x << ',' << y << " in " << ds1
                              << '\n';
            }
        }
    // Derive all consumers from the merged native flags. A room with no floor
    // graphic starts clear, just like COLLISION_AllocRoomCollisionGrid; it is
    // not an invisible walking/light barrier with a clear missile mask.
    for (size_t index = 0; index < grid.terrainCollision.size(); ++index) {
        const auto flags = grid.terrainCollision[index];
        grid.blocked[index] = (flags & (0x01 | 0x08)) != 0;
        grid.lightBlocked[index] = (flags & (0x02 | 0x20)) != 0;
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
                (!opening && (x < 2 || y < 2 || x >= width - 2 || y >= height - 2))) {
                grid.blocked[y * grid.width + x] = grid.lightBlocked[y * grid.width + x] = 1;
                grid.terrainCollision[y * grid.width + x] |= 0x0f;
            }
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
