// Cave/Crypt branches adapted from D2MOO DrlgMaze.cpp / D2Seed.h, MIT.
// Copyright (c) 2020-2025 The Phrozen Keep community. See docs/licenses/D2MOO.txt.
// Room graph rules follow the original branch; full engine RNG consumption is not reproduced.
#pragma once
#include "family.hpp"
#include "world/generation_seed.hpp"
#include "world/maze.hpp"

namespace d2x::maze {
struct Chamber {
    int x = 0, y = 0, mask = 0, preset = 0;
    bool fixed = false;
    int variant = -1;
    Seed seed;
    explicit Chamber(uint32_t value) : seed(value) { seed.next(); }
};
constexpr int dx[]{-1, 0, 1, 0}, dy[]{0, -1, 0, 1}, bits[]{1, 8, 2, 4};
class RoomMaze {
    const WorldCatalog &catalog_;
    const MazeRecord &maze_;
    Seed seed_;
    FamilyRules family_;
    int base_;
    std::vector<Chamber> rooms_;
    void placeBarracks(int direction);
    void initializeCatacombs(int level);
    void placeSewerEntrances();
    void placeArcane();
    void pick(int i) {
        auto &room = rooms_[i];
        int type = catalog_.level(maze_.level).levelType;
        if (type == 14 || type == 15) {
            constexpr int corners[]{0, 0, 0, 0, 0, 356, 355, 0, 0, 357, 354, 0, 0, 0, 0, 0};
            room.preset = corners[room.mask] + (type == 15 ? 4 : 0);
            room.variant = maze_.level == 52 && room.mask == 9 ? 2
                           : maze_.level == 54 && (room.mask == 9 || room.mask == 6) ? 3 : -1;
        } else if (type == 23) {
            constexpr int corners[]{0, 0, 0, 0, 0, 659, 660, 0, 0, 661, 662, 0, 0, 0, 0, 0};
            room.preset = corners[room.mask];
            if (maze_.level == 84 && room.preset == 662) room.preset = 664;
            if (maze_.level == 85 && room.preset == 661) room.preset = 663;
        } else if (type == 32) {
            constexpr int corners[]{0, 0, 0, 0, 0, 1045, 1044, 0, 0, 1043, 1042, 0, 0, 0, 0, 0};
            room.preset = corners[room.mask];
        } else if (type == 35) {
            constexpr int presets[]{0, 1056, 1055, 1057, 1054, 0, 0, 0,
                1053, 0, 0, 0, 1058, 0, 0, 0};
            room.preset = presets[room.mask];
        } else
            room.preset = base_ + room.mask;
    }
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
    void special(int direction, int first, int exactPreset = 0) {
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
            throw std::runtime_error("Maze generator cannot place original special room: level=" +
                std::to_string(maze_.level) + " preset=" + std::to_string(first) +
                " direction=" + std::to_string(direction) + " ordinary=" +
                std::to_string(std::count_if(rooms_.begin(), rooms_.end(),
                    [](const auto &room) { return !room.fixed; })));
        rooms_[chosen].preset = exactPreset ? exactPreset : first + offsets[direction];
        rooms_[chosen].variant = -1;
        rooms_[chosen].fixed = true;
    }

  public:
    RoomMaze(const WorldCatalog &catalog, int level, uint32_t seed)
        : catalog_(catalog), maze_(catalog.mazes().at(level)), seed_([&] {
              Seed world(seed);
              return world.next() + uint32_t(level);
          }()),
          family_(catalog.level(level).levelType == 23
              ? FamilyRules{658, maze_.width, {}, maze_.height, true}
              : catalog.level(level).levelType == 33
              ? FamilyRules{1002, maze_.width, {1018, 1022, 1026}, maze_.height, true}
              : catalog.level(level).levelType == 32
              ? FamilyRules{1041, maze_.width, {}, maze_.height, true}
              : catalog.level(level).levelType == 34
              ? FamilyRules{1058, maze_.width, {}, maze_.height}
              : catalog.level(level).levelType == 35
              ? FamilyRules{1052, maze_.width, {}, maze_.height}
              : catalog.level(level).levelType == 28
              ? FamilyRules{836, maze_.width, {}, maze_.height}
              : catalog.level(level).levelType == 25
              ? FamilyRules{704, maze_.width, {739, 743}, maze_.height, true}
              : catalog.level(level).levelType == 24
              ? FamilyRules{664, maze_.width, {695, 699}, maze_.height, true}
              : level == 100 || level == 101
              ? FamilyRules{753, maze_.width, level == 101 ? std::vector<int>{784, 792, 788}
                                                          : std::vector<int>{784, 788}, maze_.height, true}
              : level == 74 ? FamilyRules{509, 12, {525}}
              : catalog.level(level).levelType == 18 ? lairRules(level)
              : catalog.level(level).levelType == 17 ? tombRules(level)
              : level >= 51 && level <= 54 ? FamilyRules{level == 51 ? 353 : 357, 16, {}, 16, true}
              : catalog.level(level).levelType == 13 ? sewerRules(level)
              : level == 28                        ? barracksRules()
                  : level >= 29 && level <= 31          ? jailRules(level)
                  : level >= 34 && level <= 36          ? catacombsRules(level)
                  : catalog.level(level).levelType == 3 ? caveRules(level)
                                                        : cryptRules(level)),
          base_(family_.base) {}
    MapRecipe build(int level, uint32_t seed, int difficulty, int entranceDirection = 0);
};
} // namespace d2x::maze
