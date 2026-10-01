// Cave/Crypt branches adapted from D2MOO DrlgMaze.cpp / D2Seed.h, MIT.
// Copyright (c) 2020-2025 The Phrozen Keep community. See docs/licenses/D2MOO.txt.
// Room graph rules follow the original branch; full engine RNG consumption is not reproduced.
#include "room_graph.hpp"
#include <algorithm>
#include <numeric>

namespace d2x::maze {
void RoomMaze::placeSewerEntrances() {
    auto edge = [&](int direction) {
        int chosen = -1;
        for (int index = int(rooms_.size()) - 1; index >= 0; --index) {
            if (rooms_[index].fixed)
                continue;
            bool occupied = std::any_of(rooms_.begin(), rooms_.end(), [&](const auto &room) {
                return room.x == rooms_[index].x + dx[direction] &&
                       room.y == rooms_[index].y + dy[direction];
            });
            if (!occupied && (chosen < 0 || (direction == 1 ? rooms_[index].y < rooms_[chosen].y
                                                           : rooms_[index].x > rooms_[chosen].x)))
                chosen = index;
        }
        if (chosen < 0)
            throw std::runtime_error("Sewer entrance cannot extend the original maze");
        return chosen;
    };
    int vertical = (seed_.next() & 1) ? 3 : 1;
    int north = add(edge(1), 1, true);
    north = add(north, 1, true);
    int stair = add(north, 0, false);
    rooms_[stair].preset = 333;
    rooms_[stair].variant = 0;
    rooms_[stair].fixed = true;
    int east = add(edge(2), 2, true);
    east = add(east, 2, true);
    int dock = add(east, vertical, false);
    add(dock, vertical, false);
    rooms_[dock].preset = 336;
    rooms_[dock].variant = 0;
    rooms_[dock].fixed = true;
    special(seed_.next() & 3, 337);
}
MapRecipe RoomMaze::build(int level, uint32_t seed, int difficulty, int entranceDirection) {
    if (difficulty < 0 || difficulty > 2 || maze_.width != family_.roomSize ||
        maze_.height != family_.roomHeight || maze_.merge < 0 || maze_.merge > 1000)
        throw std::runtime_error("Unsupported Room maze dimensions or difficulty");
    int count = maze_.difficultyRooms[difficulty];
    int tombDirection = -1;
    if (catalog_.level(level).levelType == 17) {
        Seed act(seed);
        act.next();
        int staff = 0, boss = 0;
        do {
            staff = 66 + act.below(7);
            boss = 66 + act.below(7);
        } while (staff == boss);
        if (level == staff) {
            count *= 3;
            family_.specialRooms.push_back(460);
        } else if (level >= 66 && level <= 72)
            family_.specialRooms.push_back(472);
        if (level == boss) {
            count *= 2;
            family_.specialRooms.push_back(468);
        }
    }
    if (count < 1 || count > 256)
        throw std::runtime_error("Invalid LvlMaze room count");
    rooms_.emplace_back(seed_.next());
    if (catalog_.level(level).levelType == 17) {
        tombDirection = seed_.next() & 3;
        for (int index = 0; index < 3; ++index) {
            add(0, tombDirection, true);
            tombDirection = (tombDirection + 1) % 4;
        }
        constexpr int entrances[]{447, 444, 446, 445};
        rooms_[0].preset = entrances[tombDirection];
        rooms_[0].fixed = true;
    }
    if (level >= 34 && level <= 36)
        initializeCatacombs(level);
    if (family_.initialRing) {
        if (count < 4)
            throw std::runtime_error("Maze requires the original four-room starting layout");
        int north = add(0, 1, false);
        int west = add(north, 0, false);
        int south = add(west, 3, false);
        link(south, 0, 2);
    }
    for (int attempts = 0; int(rooms_.size()) < count; ++attempts) {
        if (attempts > count * 1000)
            throw std::runtime_error("Room maze growth exhausted");
        int parent = int(rooms_.size()) - 1 - seed_.below(int(rooms_.size()));
        int direction = rooms_[parent].seed.next() & 3;
        if (!rooms_[parent].fixed)
            add(parent, direction, true);
    }
    if (level == 64) {
        special(2, 507);
        special(3, 505);
        special(0, 497);
    } else if (level == 47)
        placeSewerEntrances();
    else if (level == 28)
        placeBarracks(entranceDirection);
    else {
        int direction = tombDirection >= 0 ? (tombDirection + 2) % 4 : seed_.next() & 3;
        auto place = [&](int first) {
            special(direction, first);
            direction = (direction + 1) % 4;
        };
        for (int preset : family_.specialRooms)
            place(preset);
    }
    // Native basic-to-theme substitution scan: shuffled 15-entry list, bounded scan.
    std::array<int, 15> offsets;
    std::iota(offsets.begin(), offsets.end(), 0);
    int cursor = seed_.below(15);
    for (int i = 0; i < 15; ++i) {
        int a = seed_.below(15), b = seed_.below(15);
        std::swap(offsets[a], offsets[b]);
    }
    int remaining = level == 8 || (level >= 51 && level <= 54) || (level >= 62 && level <= 64)
                        ? 0 : std::max(2, int(rooms_.size()) / 5 + 1);
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
    result.act = catalog_.level(level).act;
    result.ds1 =
        "maze-v1/" + std::to_string(level) + "/" + std::to_string(seed) + "/" + std::to_string(difficulty);
    if (level == 28)
        result.ds1 += "/" + std::to_string(entranceDirection);
    std::map<int, int> variants;
    for (auto i = rooms_.rbegin(); i != rooms_.rend(); ++i) {
        const auto &preset = catalog_.presets().at(i->preset);
        if (i->variant < 0 && preset.files < 1)
            throw std::runtime_error("Maze room has no selectable DS1 variants");
        int variant = i->variant >= 0 ? i->variant : seed_.below(preset.files);
        if (!(level >= 51 && level <= 54) && i->preset > base_ && i->preset < base_ + 16) {
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
} // namespace d2x::maze
