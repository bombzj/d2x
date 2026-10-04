#include "outdoor.hpp"
#include "world/generation_seed.hpp"
#include "world/map.hpp"
#include "outdoor_cliffs.hpp"
#include "outdoor_paths.hpp"
#include "outdoor_river.hpp"
#include "outdoor_shrines.hpp"
#include "outdoor_substitution.hpp"
#include <algorithm>
#include <numeric>
#include <set>

// Rectangular Act I outdoor branch adapted from D2MOO DrlgOutdoors/OutWild/OutPlace, MIT.
// Copyright (c) 2020-2025 The Phrozen Keep community. See docs/licenses/D2MOO.txt.
namespace d2x {
namespace {
class Wilderness {
    const WorldCatalog &catalog_;
    OutdoorPosition position_;
    Seed seed_;
    MapRecipe result_;
    int width_, height_;
    std::vector<int> occupied_;
    bool place(int id, int x, int y, int variant = -1) {
        const auto &p = catalog_.presets().at(id);
        int w = p.width / 8, h = p.height / 8;
        if (x < 0 || y < 0 || x + w > width_ || y + h > height_ || w < 1 || h < 1)
            return false;
        for (int j = y; j < y + h; ++j)
            for (int i = x; i < x + w; ++i)
                if (occupied_[j * width_ + i])
                    return false;
        if (variant < 0)
            variant = seed_.below(p.files);
        auto recipe = catalog_.preset(id, 2, variant);
        result_.pieces.push_back({x * 8, y * 8, p.width, p.height, id, variant, recipe.ds1,
                                  recipe.tileLibraries, recipe.fillBlanks, p.populate, -1, recipe.killEdge, recipe.animationSpeed, 0, recipe.pops, recipe.popPad});
        for (int j = y; j < y + h; ++j)
            for (int i = x; i < x + w; ++i)
                occupied_[j * width_ + i] = id;
        return true;
    }
    bool random(int id, int padding = 0) {
        std::vector<int> cells((width_ - 2) * (height_ - 2));
        std::iota(cells.begin(), cells.end(), 0);
        for (size_t i = 0; i < cells.size(); ++i)
            std::swap(cells[seed_.below(int(cells.size()))], cells[seed_.below(int(cells.size()))]);
        const auto &p = catalog_.presets().at(id);
        for (int cell : cells) {
            int x = 1 + cell % (width_ - 2), y = 1 + cell / (width_ - 2);
            if (x + p.width / 8 >= width_ || y + p.height / 8 >= height_)
                continue;
            bool clear = true;
            for (int j = y - padding; j < y + p.height / 8 + padding; ++j)
                for (int i = x - padding; i < x + p.width / 8 + padding; ++i)
                    if (i < 0 || j < 0 || i >= width_ || j >= height_ || occupied_[j * width_ + i])
                        clear = false;
            if (clear && place(id, x, y))
                return true;
        }
        return false;
    }
    void borders() {
        auto cliffs = outdoorCliffEdges(position_);
        // The normal rectangular perimeter is the straight/corner subset of the native LUT.
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x) {
                int side = y == 0 ? 2 : y == height_ - 1 ? 0 : x == 0 ? 1 : x == width_ - 1 ? 3 : -1;
                if (side < 0)
                    continue;
                bool presetEdge =
                    std::any_of(position_.boundaries.begin(), position_.boundaries.end(), [&](const auto &b) {
                        int t = (side % 2 ? y : x) * 8;
                        return (b.destination == 1 || b.destination == 26) && b.side == side &&
                               t >= b.contactStart && t + 8 <= b.contactEnd;
                    });
                // Native DRLGVER bPreset edges carry flag 2: the adjacent authored
                // level owns the fence/facade, so no second outdoor wall is placed.
                if (presetEdge) {
                    occupied_[y * width_ + x] = -1;
                    continue;
                }
                constexpr int straight[]{4, 5, 6, 7};
                int id = straight[side], variant = -1;
                if (x == 0 && y == 0)
                    id = 9;
                else if (x == width_ - 1 && y == 0)
                    id = 10;
                else if (x == width_ - 1 && y == height_ - 1)
                    id = 11;
                else if (x == 0 && y == height_ - 1)
                    id = 8;
                auto cliff = cliffs[y * width_ + x];
                if (id == 5 && (cliff & 2))
                    id = 16;
                else if (id == 6 && (cliff & 4))
                    id = 17;
                else if (id == 8 && (cliff & 2))
                    id = 18;
                else if (id == 9 && (cliff & 6))
                    id = (cliff & 6) == 6 ? 19 : (cliff & 2) ? 20 : 21;
                else if (id == 10 && (cliff & 4))
                    id = 22;
                for (const auto &b : position_.boundaries)
                    if (b.side == side && (side % 2 ? y : x) * 8 == b.start) {
                        id = straight[side];
                        variant = position_.level == 17 ? 4 : 3;
                    }
                if (!place(id, x, y, variant))
                    throw std::runtime_error("Outdoor border overlap");
            }
    }
    void waypoint(Archives &archives) {
        const int waypointId = catalog_.level(position_.level).waypoint;
        if (waypointId < 0 || waypointId == 255)
            return;
        std::vector<int> cells;
        for (int row = 1; row < height_ - 1; ++row)
            for (int column = 1; column < width_ - 1; ++column)
                if (!occupied_[row * width_ + column])
                    cells.push_back(row * width_ + column);
        for (size_t index = 0; index < cells.size(); ++index)
            std::swap(cells[seed_.below(int(cells.size()))], cells[seed_.below(int(cells.size()))]);
        if (position_.level == 3)
            for (const auto &boundary : position_.boundaries)
                if (boundary.destination == 2) {
                    const int lateral = boundary.start / 8;
                    const int targetX = boundary.side == 1 ? 1 : boundary.side == 3 ? width_ - 2 : lateral;
                    const int targetY = boundary.side == 2 ? 1 : boundary.side == 0 ? height_ - 2 : lateral;
                    std::stable_sort(cells.begin(), cells.end(), [&](int left, int right) {
                        auto distance = [&](int cell) {
                            return std::abs(cell % width_ - targetX) + std::abs(cell / width_ - targetY);
                        };
                        return distance(left) < distance(right);
                    });
                }
        for (const auto &record : catalog_.substitutions()) {
            if (record.type != catalog_.level(position_.level).waypointSubstitution || record.gridSize != 1)
                continue;
            const auto pattern = decodeDs1(archives.read(record.file));
            for (size_t index = 0; index < pattern.substitutionGroups.size(); ++index) {
                const auto &group = pattern.substitutionGroups[index];
                if (group.variants != 0 || group.width > 7 || group.height > 7 || cells.empty())
                    continue;
                int cell = cells.front();
                MapPiece piece;
                piece.x = (cell % width_) * 8 + 1;
                piece.y = (cell / width_) * 8 + 1;
                piece.width = group.width;
                piece.height = group.height;
                piece.ds1 = record.file;
                piece.tileLibraries = result_.tileLibraries;
                auto libraries = catalog_.terrainLibraries(2, record.dt1Mask);
                piece.tileLibraries.insert(piece.tileLibraries.end(), libraries.begin(), libraries.end());
                piece.substitutionGroup = int(index);
                result_.pieces.push_back(std::move(piece));
                occupied_[cell] = -1;
                return;
            }
        }
        throw std::runtime_error("No valid original waypoint placement for level " + std::to_string(position_.level));
    }
    void specialPresets() {
        int id = position_.level;
        if (id == 17) {
            if (!place(108, 1, 1))
                throw std::runtime_error("Graveyard does not fit original outdoor bounds");
            return;
        }
        auto cottage = [&](int base) {
            random(base);
            if (seed_.below(4)) {
                if (!seed_.below(2))
                    random(49);
            } else
                random(base);
        };
        auto camp = [&](int base) {
            random(base);
            if (!seed_.below(4))
                random(base);
        };
        switch (id) {
        case 2:
            random(46);
            random(47);
            if (!seed_.below(4))
                random(47);
            random(29);
            random(30);
            break;
        case 3:
            cottage(48);
            random(44);
            random(29);
            random(30);
            break;
        case 4:
            if (!random(160))
                throw std::runtime_error("No room for the native Cairn Stones");
            random(45);
            random(162);
            cottage(47);
            camp(42);
            random(31);
            break;
        // Trees2 DS1 refers to an absent (style 1, sequence 14) shadow in its native
        // Dt1Mask. Defer this optional decoration instead of drawing substitute art.
        case 5:
            if (!random(161))
                throw std::runtime_error("No room for the native Tree of Inifuss");
            random(41);
            cottage(48);
            camp(43);
            random(29);
            random(30);
            break;
        case 6:
            if (!random(163))
                throw std::runtime_error("No room for Forgotten Tower entrance");
            random(38);
            random(39);
            cottage(47);
            camp(42);
            random(29);
            random(30);
            break;
        case 7:
            random(48, 1);
            camp(43);
            random(31);
            break;
        }
    }

    void river() {
        if (!(position_.flags & 28))
            return;
        std::vector<OutdoorCell> cells(size_t(width_) * height_);
        for (const auto &piece : result_.pieces)
            cells[(piece.y / 8) * width_ + piece.x / 8] = {piece.preset, piece.variant};
        for (const auto &blank : result_.blankAreas)
            for (int row = blank.y / 8; row < (blank.y + blank.height) / 8; ++row)
                for (int column = blank.x / 8; column < (blank.x + blank.width) / 8; ++column)
                    cells[row * width_ + column].blank = true;
        for (size_t index = 0; index < cells.size(); ++index)
            cells[index].levelLink = occupied_[index] < 0 && !cells[index].blank;
        if (!placeOutdoorRiver(width_, height_, position_.flags, cells, seed_))
            return;
        result_.pieces.clear();
        std::fill(occupied_.begin(), occupied_.end(), 0);
        for (int row = 0; row < height_; ++row)
            for (int column = 0; column < width_; ++column) {
                const auto &cell = cells[row * width_ + column];
                if (cell.preset) {
                    if (!place(cell.preset, column, row, cell.variant))
                        throw std::runtime_error("Outdoor river overlap");
                } else if (cell.levelLink || cell.blank)
                    occupied_[row * width_ + column] = -1;
            }
    }
    bool cliffEntrance() {
        bool available = std::any_of(result_.pieces.begin(), result_.pieces.end(),
                                      [](const auto &piece) { return piece.preset == 16 || piece.preset == 17; });
        if (!available)
            return false;
        bool transpose = (seed_.next() & 1) != 0;
        for (int row = 0; row < height_; ++row)
            for (int column = 0; column < width_; ++column) {
                int x = transpose ? row : column, y = transpose ? column : row;
                if (x >= width_ || y >= height_)
                    continue;
                auto found = std::find_if(result_.pieces.begin(), result_.pieces.end(),
                                           [&](const auto &piece) { return piece.x == x * 8 && piece.y == y * 8 &&
                                               (piece.preset == 16 || piece.preset == 17); });
                if (found == result_.pieces.end())
                    continue;
                int preset = found->preset == 16 ? 25 : 24;
                result_.pieces.erase(found);
                occupied_[y * width_ + x] = 0;
                if (!place(preset, x, y))
                    throw std::runtime_error("Cannot place original cliff entrance");
                return true;
            }
        return false;
    }
    void townTransitions() {
        auto replace = [&](int preset, int x, int y, int variant) {
            const auto &record = catalog_.presets().at(preset);
            int columns = record.width / 8, rows = record.height / 8;
            if (x < 0 || y < 0 || x + columns > width_ || y + rows > height_)
                throw std::runtime_error("Town transition exceeds original outdoor bounds");
            std::erase_if(result_.pieces, [&](const auto &piece) {
                return piece.x < (x + columns) * 8 && piece.x + piece.width > x * 8 &&
                       piece.y < (y + rows) * 8 && piece.y + piece.height > y * 8;
            });
            for (int row = y; row < y + rows; ++row)
                for (int column = x; column < x + columns; ++column)
                    occupied_[row * width_ + column] = 0;
            if (!place(preset, x, y, variant))
                throw std::runtime_error("Cannot place original town transition");
        };
        if (position_.flags & 0x80)
            replace(3, 0, 0, 1);
        if (position_.flags & 0x100)
            replace(3, width_ - 7, 0, 2);
        if (position_.flags & 0x200)
            replace(2, 0, 1, 1);
        if (position_.flags & 0x400)
            replace(2, 0, height_ - 6, 1);
    }
    void secondaryBorders(Archives &archives, int firstType, int lastType) {
        if (position_.level == 17)
            return;
        std::vector<OutdoorCell> cells(size_t(width_) * height_);
        for (const auto &piece : result_.pieces) {
            if (piece.preset >= 4 && piece.preset <= 23)
                cells[(piece.y / 8) * width_ + piece.x / 8] = {piece.preset, piece.variant};
            else
                for (int row = piece.y / 8; row < (piece.y + piece.height) / 8; ++row)
                    for (int column = piece.x / 8; column < (piece.x + piece.width) / 8; ++column)
                        cells[row * width_ + column] = {piece.preset, piece.variant, false, true};
        }
        for (int row = 0; row < height_; ++row)
            for (int column = 0; column < width_; ++column) {
                auto &cell = cells[row * width_ + column];
                cell.blank = std::any_of(result_.blankAreas.begin(), result_.blankAreas.end(),
                                          [&](const auto &blank) {
                                              return column * 8 >= blank.x && row * 8 >= blank.y &&
                                                     column * 8 < blank.x + blank.width &&
                                                     row * 8 < blank.y + blank.height;
                                          });
                if (occupied_[row * width_ + column] < 0 && !cell.blank)
                    cell.levelLink = true;
                for (const auto &boundary : position_.boundaries) {
                    int lateral = (boundary.side % 2 ? row : column) * 8;
                    bool edge = boundary.side == 0   ? row == height_ - 1
                                : boundary.side == 1 ? column == 0
                                : boundary.side == 2 ? row == 0
                                                     : column == width_ - 1;
                    if (edge && lateral >= boundary.start && lateral < boundary.end)
                        cell.levelLink = true;
                }
            }
        for (int type = firstType; type <= lastType; ++type)
            for (const auto &record : catalog_.substitutions())
                if (record.type == type)
                    applyOutdoorBorder(record, decodeDs1(archives.read(record.file)), width_, height_, cells,
                                       seed_);
        std::erase_if(result_.pieces, [](const auto &piece) {
            return piece.preset >= 4 && piece.preset <= 23;
        });
        result_.blankAreas.clear();
        std::fill(occupied_.begin(), occupied_.end(), 0);
        for (int row = 0; row < height_; ++row)
            for (int column = 0; column < width_; ++column) {
                const auto &cell = cells[row * width_ + column];
                if (cell.preset > 23 || (cell.preset > 0 && cell.preset < 4)) {
                    occupied_[row * width_ + column] = cell.preset;
                    continue;
                }
                if (cell.preset) {
                    if (!place(cell.preset, column, row, cell.variant))
                        throw std::runtime_error("Secondary outdoor border overlap");
                } else if (cell.blank || cell.levelLink) {
                    occupied_[row * width_ + column] = -1;
                    if (cell.blank)
                        result_.blankAreas.push_back({column * 8, row * 8, 8, 8});
                }
            }
    }

  public:
    Wilderness(const WorldCatalog &catalog, OutdoorPosition position, uint32_t seed)
        : catalog_(catalog), position_(std::move(position)), seed_([&] {
              Seed world(seed);
              return world.next() + uint32_t(position_.level);
          }()),
          width_(position_.width / 8), height_(position_.height / 8), occupied_(width_ * height_) {
        result_.preset = 5; // Population-enabled native outdoor room family.
        result_.levelType = 2;
        result_.width = position_.width;
        result_.height = position_.height;
        result_.worldX = position_.x;
        result_.worldY = position_.y;
        result_.baseFloor = 0x40002; // DRLGOUTPLACE_InitOutdoorRoomGrids: native Act I grass.
        result_.tileLibraries = catalog.terrainLibraries(2, 0x44103);
        result_.boundaries = position_.boundaries;
        result_.ds1 = "outdoor-v7/" + std::to_string(position_.level) + "/" + std::to_string(seed);
    }
    MapRecipe build(Archives &archives) {
        borders();
        secondaryBorders(archives, 0, 0);
        river();
        townTransitions();
        if (position_.level != 17 && !cliffEntrance() && !random(position_.level == 2 ? 52 : 51, 1))
            throw std::runtime_error("No room for the native cave entrance");
        secondaryBorders(archives, 1, 3);
        waypoint(archives);
        placeAct1OutdoorShrines(archives, catalog_, catalog_.level(position_.level), occupied_, result_, seed_);
        if (position_.level != 17)
            generateOutdoorPaths(archives, result_, occupied_, seed_);
        specialPresets();
        return std::move(result_);
    }
};

void alignPresetBoundary(Archives &archives, const WorldCatalog &catalog,
                         std::map<int, OutdoorPosition> &layout, int id, int preset, uint32_t worldSeed) {
    auto &p = layout.at(id);
    auto recipe = catalog.preset(preset, catalog.level(id).levelType, id == 1 || id == 40 ? p.direction : 0);
    auto &boundary = p.boundaries.front();
    auto contact = boundary;
    const int size = boundary.side % 2 ? p.height : p.width;
    contact.start = std::clamp(boundary.contactStart, 0, size);
    contact.end = std::clamp(boundary.contactEnd, 0, size);
    recipe.width = p.width;
    recipe.height = p.height;
    recipe.boundaries = {contact};
    TileLibraryCache cache(archives);
    Map map;
    Seed tileRoot(worldSeed);
    map.load(archives, cache, recipe, tileRoot.next() + uint32_t(id));
    const Vec origin = id == 1 || id == 40 ? map.actSpawn() : map.spawn;
    const auto reachable = map.grid.reachableFrom(origin);
    int first = contact.end * 5, last = -1;
    for (int lateral = contact.start * 5; lateral < contact.end * 5; ++lateral) {
        const int column = boundary.side == 1 ? 0 : boundary.side == 3 ? p.width * 5 - 1 : lateral;
        const int row = boundary.side == 2 ? 0 : boundary.side == 0 ? p.height * 5 - 1 : lateral;
        if (map.grid.walkable(column, row) && reachable[size_t(row * map.grid.width + column)]) {
            first = std::min(first, lateral);
            last = lateral;
        }
    }
    if (last < 0)
        throw std::runtime_error("Native preset has no reachable connection on its linked edge: " + recipe.ds1);
    boundary.start = first / 5;
    boundary.end = last / 5 + 1;
    if (id == 1) {
        // The original town DS1 supplies the road crossing. Only consider
        // floors on the reachable gate, not decorative dirt elsewhere on the edge.
        auto findPath = [&](bool includeAuthoredApproach) {
        for (int tile = boundary.start; tile < boundary.end; ++tile) {
            const int x = boundary.side == 1 ? 0 : boundary.side == 3 ? p.width : tile;
            const int y = boundary.side == 2 ? 0 : boundary.side == 0 ? p.height : tile;
            bool passage = false;
            for (int sub = tile * 5; sub < (tile + 1) * 5; ++sub) {
                const int column = boundary.side == 1 ? 0 : boundary.side == 3 ? p.width * 5 - 1 : sub;
                const int row = boundary.side == 2 ? 0 : boundary.side == 0 ? p.height * 5 - 1 : sub;
                passage |= map.grid.walkable(column, row) && reachable[size_t(row) * map.grid.width + column];
            }
            if (!passage) continue;
            const bool road = std::any_of(map.terrain.data.floors.begin(), map.terrain.data.floors.end(),
                [&](const auto &layer) {
                    const auto &cell = layer[size_t(y) * map.terrain.data.width + x];
                    // TownE's reachable opening is an authored bridge approach,
                    // not a dirt tile. Preserve it and join the outdoor dirt at its edge.
                    return isOutdoorPathFloor(cell) || (includeAuthoredApproach && cell.present() && cell.key() != 0);
                });
            if (!road) continue;
            if (boundary.pathStart < 0) boundary.pathStart = tile;
            if (tile > boundary.pathStart + 1) break;
            boundary.pathEnd = tile + 1;
        }
        };
        findPath(false);
        if (boundary.pathStart < 0) findPath(true);
        if (boundary.pathStart < 0)
            throw std::runtime_error("Native town gate has no authored road approach: " + recipe.ds1 +
                " side=" + std::to_string(boundary.side) + " span=" + std::to_string(boundary.start) +
                ":" + std::to_string(boundary.end));
    }
    auto &other = layout.at(boundary.destination);
    for (auto &back : other.boundaries)
        if (back.destination == id) {
            const int offset = boundary.side % 2 ? p.y - other.y : p.x - other.x;
            back.start = boundary.start + offset;
            back.end = boundary.end + offset;
            if (boundary.pathStart >= 0) {
                back.pathStart = boundary.pathStart + offset;
                back.pathEnd = boundary.pathEnd + offset;
            }
        }
}
} // namespace
std::map<int, MapRecipe> generateAct1Outdoors(Archives &archives, const WorldCatalog &catalog,
                                              uint32_t seed) {
    auto layout = layoutAct1(catalog, seed);
    alignPresetBoundary(archives, catalog, layout, 1, 1, seed);
    alignPresetBoundary(archives, catalog, layout, 26, 165, seed);
    std::map<int, MapRecipe> result;
    for (const auto &[id, p] : layout) {
        auto recipe = id == 1    ? catalog.preset(1, 1, p.direction)
                      : id == 26 ? catalog.preset(165, catalog.level(26).levelType)
                                 : Wilderness(catalog, p, seed).build(archives);
        recipe.width = p.width;
        recipe.height = p.height;
        recipe.worldX = p.x;
        recipe.worldY = p.y;
        recipe.boundaries = p.boundaries;
        result.emplace(id, std::move(recipe));
    }
    return result;
}
std::map<int, MapRecipe> generateAct2Outdoors(Archives &archives, const WorldCatalog &catalog, uint32_t seed) {
    auto layout = layoutAct2(catalog, seed);
    alignPresetBoundary(archives, catalog, layout, 40, 301, seed);
    std::map<int, MapRecipe> result;
    for (const auto &[id, position] : layout) {
        auto recipe = id == 40 ? catalog.preset(301, 12, position.direction)
                               : generateDesert(archives, catalog, position, seed);
        recipe.width = position.width;
        recipe.height = position.height;
        recipe.worldX = position.x;
        recipe.worldY = position.y;
        recipe.boundaries = position.boundaries;
        result.emplace(id, std::move(recipe));
    }
    return result;
}
std::vector<MapRecipe> outdoorTemplates(const WorldCatalog &catalog) {
    std::vector<MapRecipe> result;
    for (int id = 2; id <= 163; ++id) {
        if (!(id <= 52 || id == 108 || id >= 160) || id == 40)
            continue;
        const auto &preset = catalog.presets().at(id);
        for (int v = 0; v < 6; ++v)
            if (!preset.variants[v].empty())
                result.push_back(catalog.preset(id, 2, v));
    }
    return result;
}
std::vector<std::string> outdoorMissing(Archives &archives, const WorldCatalog &catalog) {
    std::set<std::string> result;
    for (const auto &r : outdoorTemplates(catalog))
        for (const auto &path : catalog.missing(archives, r))
            result.insert(path);
    for (const auto &path : catalog.terrainLibraries(2, 0x44103))
        if (!archives.contains(path))
            result.insert(path);
    for (int type = 0; type <= 5; ++type) {
        bool found = false;
        for (const auto &record : catalog.substitutions())
            if (record.type == type) {
                found = true;
                if (!archives.contains(record.file))
                    result.insert(record.file);
            }
        if (!found)
            result.insert("LvlSub type " + std::to_string(type));
    }
    for (int id = 2; id <= 7; ++id) {
        const int type = catalog.level(id).shrineSubstitution;
        if (type < 0 || std::count_if(catalog.substitutions().begin(), catalog.substitutions().end(),
                                      [&](const auto &record) { return record.type == type; }) < 4)
            result.insert("Levels.SubShrine / LvlSub rows for level " + std::to_string(id));
    }
    return {result.begin(), result.end()};
}
} // namespace d2x
