#include "maze.hpp"
#include "generation_seed.hpp"
#include <algorithm>
#include <numeric>
#include <set>

// Cave/Crypt branches adapted from D2MOO DrlgMaze.cpp / D2Seed.h, MIT.
// Copyright (c) 2020-2025 The Phrozen Keep community. See docs/licenses/D2MOO.txt.
// Room graph rules follow the original branch; full engine RNG consumption is not reproduced.
namespace d2x {
namespace {
struct Chamber {
    int x = 0, y = 0, mask = 0, preset = 0;
    bool fixed = false;
    Seed seed;
    explicit Chamber(uint32_t value) : seed(value) { seed.next(); }
};
constexpr int dx[]{-1, 0, 1, 0}, dy[]{0, -1, 0, 1}, bits[]{1, 8, 2, 4};
class RoomMaze {
    const WorldCatalog &catalog_;
    const MazeRecord &maze_;
    Seed seed_;
    int base_;
    std::vector<Chamber> rooms_;
    void pick(int i) { rooms_[i].preset = base_ + rooms_[i].mask; }
    void link(int a, int b, int direction) {
        rooms_[a].mask |= bits[direction];
        rooms_[b].mask |= bits[(direction + 2) % 4];
        pick(a);
        pick(b);
    }
    int add(int parent, int direction, bool merge) {
        Chamber room(seed_.next()); // Failed placements still advance the level stream.
        room.x = rooms_[parent].x + dx[direction];
        room.y = rooms_[parent].y + dy[direction];
        for (const auto &existing : rooms_)
            if (existing.x == room.x && existing.y == room.y)
                return -1;
        int added = int(rooms_.size());
        rooms_.push_back(room);
        link(parent, added, direction);
        if (merge)
            for (int i = added - 1; i >= 0; --i) {
                if (i == parent || rooms_[i].fixed)
                    continue;
                for (int d = 0; d < 4; ++d)
                    if (rooms_[i].x + dx[d] == room.x && rooms_[i].y + dy[d] == room.y &&
                        rooms_[i].seed.below(1000) < maze_.merge)
                        link(i, added, d);
            }
        return added;
    }
    void special(int direction, int first) {
        const int base[]{base_ + 8, base_ + 2, base_ + 4, base_ + 1};
        constexpr int offsets[]{3, 1, 2, 0};
        constexpr int extension[]{3, 0, 1, 2};
        int chosen = -1;
        for (int i = int(rooms_.size()) - 1; i >= 0; --i)
            if (!rooms_[i].fixed && rooms_[i].preset == base[direction]) {
                chosen = i;
                break;
            }
        if (chosen < 0)
            for (int i = int(rooms_.size()) - 1; i >= 0; --i)
                if (!rooms_[i].fixed && (chosen = add(i, extension[direction], false)) >= 0)
                    break;
        if (chosen < 0)
            throw std::runtime_error("Maze generator cannot place the original stair room");
        rooms_[chosen].preset = first + offsets[direction];
        rooms_[chosen].fixed = true;
    }

  public:
    RoomMaze(const WorldCatalog &catalog, int level, uint32_t seed)
        : catalog_(catalog), maze_(catalog.mazes().at(level)), seed_([&] {
              Seed world(seed);
              return world.next() + uint32_t(level);
          }()),
          base_(catalog.level(level).levelType == 3 ? 52 : 108) {}
    MapRecipe build(int level, uint32_t seed, int difficulty) {
        if (difficulty < 0 || difficulty > 2 || maze_.width != (base_ == 52 ? 24 : 8) ||
            maze_.height != maze_.width || maze_.merge < 0 || maze_.merge > 1000)
            throw std::runtime_error("Unsupported Room maze dimensions or difficulty");
        int count = maze_.difficultyRooms[difficulty];
        if (count < 1 || count > 256)
            throw std::runtime_error("Invalid LvlMaze room count");
        rooms_.emplace_back(seed_.next());
        for (int attempts = 0; int(rooms_.size()) < count; ++attempts) {
            if (attempts > count * 1000)
                throw std::runtime_error("Room maze growth exhausted");
            int parent = int(rooms_.size()) - 1 - seed_.below(int(rooms_.size()));
            int direction = rooms_[parent].seed.next() & 3;
            if (!rooms_[parent].fixed)
                add(parent, direction, true);
        }
        int direction = seed_.next() & 3;
        auto place = [&](int first) {
            special(direction, first);
            direction = (direction + 1) % 4;
        };
        if (base_ == 52) {
            place(83);
            place(level == 8 ? 95 : 91);
            if (level == 9)
                place(99);
            if (level == 10)
                place(87);
        } else {
            place(139);
            if (level == 18)
                place(147);
            else if (level == 19)
                place(151);
            else
                place(143);
        }
        // Native basic-to-theme substitution scan: shuffled 15-entry list, bounded scan.
        std::array<int, 15> offsets;
        std::iota(offsets.begin(), offsets.end(), 0);
        int cursor = seed_.below(15);
        for (int i = 0; i < 15; ++i) {
            int a = seed_.below(15), b = seed_.below(15);
            std::swap(offsets[a], offsets[b]);
        }
        int remaining = level == 8 ? 0 : std::max(2, int(rooms_.size()) / 5 + 1);
        for (int attempt = 0; remaining && attempt < 2 * int(rooms_.size()); ++attempt) {
            for (auto i = rooms_.rbegin(); i != rooms_.rend(); ++i)
                if (!i->fixed && i->preset == base_ + offsets[cursor]) {
                    i->preset += 15;
                    i->fixed = true;
                    --remaining;
                    break;
                }
            cursor = (cursor + 1) % 15;
        }
        int minX = 0, minY = 0;
        for (const auto &r : rooms_) {
            minX = std::min(minX, r.x);
            minY = std::min(minY, r.y);
        }
        MapRecipe result;
        result.preset = base_ + 1; // Native population-enabled Crypt room family.
        result.levelType = catalog_.level(level).levelType;
        result.ds1 = "maze-v1/" + std::to_string(level) + "/" + std::to_string(seed) + "/" +
                     std::to_string(difficulty);
        std::map<int, int> variants;
        for (auto i = rooms_.rbegin(); i != rooms_.rend(); ++i) {
            const auto &preset = catalog_.presets().at(i->preset);
            int variant = seed_.below(preset.files);
            if (i->preset > base_ && i->preset < base_ + 16) {
                auto [it, inserted] = variants.try_emplace(i->preset, variant);
                (void)inserted;
                it->second = (it->second + 1) % preset.files;
                variant = it->second;
            }
            auto source = catalog_.preset(i->preset, result.levelType, variant);
            result.pieces.push_back({(i->x - minX) * maze_.width, (i->y - minY) * maze_.height, maze_.width,
                                     maze_.height, i->preset, variant, source.ds1, source.tileLibraries,
                                     source.fillBlanks, preset.populate});
        }
        return result;
    }
};
} // namespace
std::vector<int> mazePresets() {
    std::vector<int> ids;
    for (int id = 53; id <= 102; ++id)
        ids.push_back(id);
    for (int id = 109; id <= 154; ++id)
        ids.push_back(id);
    return ids;
}
bool supportsMaze(int level) {
    return (level >= 8 && level <= 12) || level == 18 || level == 19 || (level >= 21 && level <= 24);
}
MapRecipe generateMaze(const WorldCatalog &catalog, int level, uint32_t seed, int difficulty) {
    if (!supportsMaze(level))
        throw std::runtime_error("This original maze family is not implemented");
    return RoomMaze(catalog, level, seed).build(level, seed, difficulty);
}
std::vector<std::string> mazeMissing(Archives &archives, const WorldCatalog &catalog) {
    std::set<std::string> missing;
    for (int id : mazePresets())
        for (int v = 0; v < catalog.presets().at(id).files; ++v)
            for (const auto &file : catalog.missing(archives, catalog.preset(id, id < 108 ? 3 : 4, v)))
                missing.insert(file);
    return {missing.begin(), missing.end()};
}
void collectMazeResources(Archives &archives, const WorldCatalog &catalog) {
    for (int id : mazePresets())
        for (int v = 0; v < catalog.presets().at(id).files; ++v) {
            auto recipe = catalog.preset(id, id < 108 ? 3 : 4, v);
            archives.read(recipe.ds1);
            for (const auto &path : recipe.tileLibraries)
                archives.read(path);
        }
}
} // namespace d2x
