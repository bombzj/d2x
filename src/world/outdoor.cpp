#include "outdoor.hpp"
#include "generation_seed.hpp"
#include "map.hpp"
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
                                  recipe.tileLibraries, recipe.fillBlanks, p.populate});
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
                for (const auto &b : position_.boundaries)
                    if (b.side == side && (side % 2 ? y : x) * 8 == b.start) {
                        id = straight[side];
                        variant = position_.level == 17 ? 4 : 3;
                    }
                if (!place(id, x, y, variant))
                    throw std::runtime_error("Outdoor border overlap");
            }
    }
    void specialPresets() {
        int id = position_.level;
        if (id == 17) {
            if (!place(108, 1, 1))
                throw std::runtime_error("Graveyard does not fit original outdoor bounds");
            return;
        }
        // Original cave entrance is mandatory; decorative placements may exhaust free cells.
        if (!random(id == 2 ? 52 : 51, 1))
            throw std::runtime_error("No room for the native cave entrance");
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
            random(160);
            random(45);
            random(162);
            cottage(47);
            camp(42);
            random(31);
            break;
        // Trees2 DS1 refers to an absent (style 1, sequence 14) shadow in its native
        // Dt1Mask. Defer this optional decoration instead of drawing substitute art.
        case 5:
            random(161);
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
        result_.ds1 = "outdoor-v1/" + std::to_string(position_.level) + "/" + std::to_string(seed);
    }
    MapRecipe build() {
        borders();
        specialPresets();
        return std::move(result_);
    }
};

void alignPresetBoundary(Archives &archives, const WorldCatalog &catalog,
                         std::map<int, OutdoorPosition> &layout, int id, int preset) {
    auto &p = layout.at(id);
    auto recipe = catalog.preset(preset, catalog.level(id).levelType, id == 1 ? p.direction : 0);
    TileLibraryCache cache(archives);
    Map map;
    map.load(archives, cache, recipe);
    auto &b = p.boundaries.front();
    int best = -1, distance = 100000;
    int size = (b.side % 2 ? p.height : p.width) * 5;
    for (int t = 5; t < size - 5; ++t) {
        int x = b.side == 1 ? 3 : b.side == 3 ? p.width * 5 - 3 : t;
        int y = b.side == 2 ? 3 : b.side == 0 ? p.height * 5 - 3 : t;
        if (map.grid.walkable(x, y) && std::abs(t - size / 2) < distance) {
            best = t;
            distance = std::abs(t - size / 2);
        }
    }
    if (best < 0)
        throw std::runtime_error("Native preset has no walkable connection on its linked edge: " +
                                 recipe.ds1);
    b.start = (best / 40) * 8;
    b.end = b.start + 8;
    auto &other = layout.at(b.destination);
    for (auto &back : other.boundaries)
        if (back.destination == id) {
            back.start = b.start + (b.side % 2 ? p.y - other.y : p.x - other.x);
            back.end = back.start + 8;
        }
}
} // namespace
std::map<int, MapRecipe> generateAct1Outdoors(Archives &archives, const WorldCatalog &catalog,
                                              uint32_t seed) {
    auto layout = layoutAct1(catalog, seed);
    alignPresetBoundary(archives, catalog, layout, 1, 1);
    alignPresetBoundary(archives, catalog, layout, 26, 165);
    std::map<int, MapRecipe> result;
    for (const auto &[id, p] : layout) {
        auto recipe = id == 1    ? catalog.preset(1, 1, p.direction)
                      : id == 26 ? catalog.preset(165, catalog.level(26).levelType)
                                 : Wilderness(catalog, p, seed).build();
        recipe.width = p.width;
        recipe.height = p.height;
        recipe.worldX = p.x;
        recipe.worldY = p.y;
        recipe.boundaries = p.boundaries;
        result.emplace(id, std::move(recipe));
    }
    return result;
}
std::vector<MapRecipe> outdoorTemplates(const WorldCatalog &catalog) {
    std::vector<MapRecipe> result;
    for (int id = 4; id <= 163; ++id) {
        if (!(id <= 15 || (id >= 29 && id <= 52) || id == 108 || id >= 160) || id == 40)
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
    return {result.begin(), result.end()};
}
} // namespace d2x
