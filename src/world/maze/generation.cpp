// Cave/Crypt branches adapted from D2MOO DrlgMaze.cpp / D2Seed.h, MIT.
// Copyright (c) 2020-2025 The Phrozen Keep community. See docs/licenses/D2MOO.txt.
// Room graph and allocation draws are shared by offline and native map consumers.
#include "room_graph.hpp"
#include <algorithm>
#include <numeric>

namespace d2x::maze {
void RoomMaze::fillBlank(int ignore) {
    constexpr int horizontal[]{-1,0,1,0,-1,1,1,-1};
    constexpr int vertical[]{0,-1,0,1,-1,-1,1,1};
    const int count = int(rooms_.size());
    for (int index = count - 1; index >= 0; --index) {
        if (index == ignore) continue;
        for (int direction = 0; direction < 8; ++direction) {
            Chamber filler(seed_.next()); // Native allocates even failed overlapping probes.
            filler.x = rooms_[index].x + horizontal[direction];
            filler.y = rooms_[index].y + vertical[direction];
            if (std::any_of(rooms_.begin(), rooms_.end(), [&](const auto &room) {
                return room.x == filler.x && room.y == filler.y;
            })) continue;
            filler.preset = 836;
            filler.fixed = true;
            rooms_.push_back(filler);
        }
    }
}
void RoomMaze::placeArcane() {
    int variant = seed_.next() & 3;
    for (int branch = 0; branch < 4; ++branch) {
        int parent = 0;
        for (int index = 0; index < 15; ++index) {
            int direction = branch;
            if (index == 2 || index == 12)
                direction += 3;
            else if (index == 7 || index == 9)
                direction += 1;
            else if (index == 10 || index == 11 || index == 13 || index == 14)
                direction += 2;
            int added = add(parent, direction % 4, true);
            if (added < 0)
                throw std::runtime_error("Original Arcane branch overlaps another room");
            if (index != 8 && index != 12) {
                rooms_[added].variant = (variant + branch) % 4;
                parent = added;
            }
        }
    }
    rooms_[0].variant = 4;
}
void RoomMaze::placeSewerEntrances() {
    auto edge = [&](int direction) {
        int chosen = -1;
        for (int index = int(rooms_.size()) - 1; index >= 0; --index) {
            if (rooms_[index].fixed)
                continue;
            if (chosen >= 0 && !(direction == 1 ? rooms_[index].y < rooms_[chosen].y :
                                                    rooms_[index].x > rooms_[chosen].x)) continue;
            if (rooms_[index].mask & bits[direction]) continue;
            // CheckIfMayPlaceAdjacentPresetRoom allocates and frees a probe.
            // Even an overlap consumes a level seed; only existing orths skip it.
            seed_.next();
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
    const int direction = seed_.next() & 3;
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
    special(direction, 337);
}
MapRecipe RoomMaze::build(int level, uint32_t seed, int difficulty, int entranceDirection,
    const NativeActLayout *layout, const ConvertRoom &convert) {
    if (difficulty < 0 || difficulty > 2 || maze_.width != family_.roomSize ||
        maze_.height != family_.roomHeight || maze_.merge < 0 || maze_.merge > 1000)
        throw std::runtime_error("Unsupported Room maze dimensions or difficulty");
    int count = maze_.difficultyRooms[difficulty];
    int tombDirection = -1;
    int lavaBridge = -1;
    if (catalog_.level(level).levelType == 17) {
        const auto tombs = actTwoTombs(seed);
        const int staff = tombs[0], boss = tombs[1];
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
    if (level == 61 || level == 114 || level == 116 || level == 119) {
        rooms_[0].preset = level == 61 ? 480 : level == 114 ? (seed_.below(2) ? 1038 : 1039)
                             : level == 116 ? 1040 : 1041;
        rooms_[0].fixed = true;
    } else {
    if (catalog_.level(level).levelType == 35) {
        struct LavaBranch { int preset, direction, variant; };
        constexpr LavaBranch branches[]{
            {1054, 1, 0}, {1053, 3, 1}, {1054, 1, 1}, {1053, 3, 0},
            {1055, 0, 1}, {1056, 2, 0}, {1055, 0, 0}, {1056, 2, 1}};
        const int selected = 2 * (rooms_[0].seed.next() & 3);
        for (int index = selected; index < selected + 2; ++index) {
            const auto &branch = branches[index];
            const int added = add(0, branch.direction, false);
            if (added < 0) throw std::runtime_error("Original lava branch overlaps");
            rooms_[added].preset = branch.preset;
            rooms_[added].variant = branch.variant;
            rooms_[added].fixed = true;
        }
        fillBlank();
    }
    if (level == 74)
        placeArcane();
    if (catalog_.level(level).levelType == 17 || catalog_.level(level).levelType == 34) {
        tombDirection = seed_.next() & 3;
        for (int index = 0; index < 3; ++index) {
            add(0, tombDirection, true);
            tombDirection = (tombDirection + 1) % 4;
        }
        constexpr int entrances[]{447, 444, 446, 445};
        constexpr int baalEntrances[]{1075, 1077, 1076, 1074};
        rooms_[0].preset = catalog_.level(level).levelType == 34
            ? baalEntrances[tombDirection] : entrances[tombDirection];
        rooms_[0].fixed = true;
    }
    if (level >= 34 && level <= 36)
        initializeCatacombs(level);
    if (family_.initialRing) {
        const int sideRooms = level == 92 ? 5 : 2;
        if (count < 4 * (sideRooms - 1))
            throw std::runtime_error("Maze requires the original four-room starting layout");
        int parent = 0;
        for (int direction : {1, 0, 3, 2})
            for (int step = 0; step < sideRooms - 1; ++step) {
                if (direction == 2 && step == sideRooms - 2) {
                    link(parent, 0, direction);
                } else {
                    parent = add(parent, direction, true);
                    if (parent < 0) throw std::runtime_error("Original maze initial ring overlaps");
                }
            }
        if (level == 92)
            for (auto &room : rooms_) {
                const int corner = room.mask == 5 ? 735 : room.mask == 6 ? 736
                                 : room.mask == 9 ? 737 : room.mask == 10 ? 738 : 0;
                if (corner) {
                    room.preset = corner;
                    room.fixed = true;
                }
            }
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
        seed_.next(); // PlaceAct2LairStuff draws its direction even for the fixed third-floor branches.
        special(2, 507);
        special(3, 505);
        special(0, 497);
    } else if (level == 47)
        placeSewerEntrances();
    else if (level == 28)
        placeBarracks(entranceDirection);
    else if (level == 107) {
        auto extend = [&](bool north, const std::vector<int> &presets) {
            int parent = -1;
            const int direction = north ? 1 : 3;
            for (int index = int(rooms_.size()) - 1; index >= 0; --index) {
                const auto &candidate = rooms_[index];
                if (parent >= 0 && !(north ? candidate.y < rooms_[parent].y : candidate.y > rooms_[parent].y)) continue;
                if (candidate.fixed || (candidate.mask & bits[direction])) continue;
                seed_.next();
                if (std::any_of(rooms_.begin(), rooms_.end(), [&](const auto &room) {
                    return room.x == candidate.x + dx[direction] && room.y == candidate.y + dy[direction];
                })) continue;
                parent = index;
            }
            if (parent < 0) throw std::runtime_error("Native lava bridge has no free edge");
            for (int preset : presets) {
                const int added = add(parent, direction, false);
                if (added < 0) throw std::runtime_error("Original lava bridge overlaps");
                rooms_[added].preset = preset;
                rooms_[added].fixed = true;
                parent = added;
            }
            return parent;
        };
        extend(false, {852});
        lavaBridge = extend(true, {855, 856, 856});
        const bool east = seed_.next() & 1;
        special(east ? 1 : 3, 853, east ? 854 : 853);
        fillBlank(lavaBridge);
    }
    else if (catalog_.level(level).levelType == 32) {
        const int selected = seed_.below(3);
        constexpr int corners[]{1042, 1043, 1045, 1044};
        constexpr int down[]{1046, 1047, 1048};
        constexpr int waypoint[]{1049, 1050, 1052, 1051};
        for (auto &room : rooms_)
            if (room.preset == corners[selected]) {
                room.preset = down[selected];
                room.fixed = true;
            }
        if (level == 123)
            for (auto &room : rooms_)
                if (room.preset == corners[(selected + 1) % 4]) {
                    room.preset = waypoint[(selected + 1) % 4];
                    room.fixed = true;
                }
    } else if (catalog_.level(level).levelType == 34) {
        int direction = seed_.next() & 3;
        auto placeMapped = [&](const std::array<int, 4> &presets) {
            special(direction, 1078, presets[direction]);
            direction = (direction + 1) % 4;
        };
        placeMapped({1078, 1080, 1079, 1081});
        if (level == 129) placeMapped({1082, 1084, 1083, 1085});
    }
    else if (catalog_.level(level).levelType != 35 && catalog_.level(level).levelType != 14 &&
             catalog_.level(level).levelType != 15 && catalog_.level(level).levelType != 23) {
        if (catalog_.level(level).levelType == 13) seed_.next(); // Sewer vertical branch draw also occurs outside floor one.
        int direction = tombDirection >= 0 ? (tombDirection + 2) % 4 : seed_.next() & 3;
        auto place = [&](int first) {
            special(direction, first);
            direction = (direction + 1) % 4;
        };
        for (int preset : family_.specialRooms)
            place(preset);
        if (catalog_.level(level).levelType == 33) {
            if (level == 115) place(1030);
            if (level == 113 || level == 115 || level == 118) {
                constexpr int waypoint[]{1034, 1036, 1035, 1037};
                special(direction, 1034, waypoint[direction]);
            }
        }
    }
    } // Ordinary maze graph; Claw Viper Temple II retains its fixed chamber.
    // Native basic-to-theme substitution scan: shuffled 15-entry list, bounded scan.
    std::array<int, 15> offsets;
    std::iota(offsets.begin(), offsets.end(), 0);
    const int type = catalog_.level(level).levelType;
    const bool substitutes = level != 8 && (type == 3 || type == 4 || type == 7 || type == 8 ||
        type == 10 || type == 13 || type == 17 || type == 22 || type == 24 || type == 25);
    int cursor = 0;
    if (substitutes) {
        cursor = seed_.below(15);
        for (int i = 0; i < 15; ++i) {
            int a = seed_.below(15), b = seed_.below(15);
            std::swap(offsets[a], offsets[b]);
        }
    }
    int remaining = substitutes ? std::max(2, int(rooms_.size()) / 5 + 1) : 0;
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
    if (layout) {
        result.worldX = layout->levels.at(level).x;
        result.worldY = layout->levels.at(level).y;
        if (level == 107) {
            const auto &chaos = layout->levels.at(108);
            result.worldX = chaos.x + 2 * maze_.width - (rooms_.at(lavaBridge).x - minX) * maze_.width;
            result.worldY = chaos.y + chaos.height - (rooms_.at(lavaBridge).y - minY) * maze_.height;
        }
        if (level == 28) {
            const auto &courtPlacement = layout->levels.at(27);
            MapRecipe court;
            court.worldX = courtPlacement.x; court.worldY = courtPlacement.y;
            court.variant = entranceDirection;
            const auto entrance = std::find_if(rooms_.begin(), rooms_.end(),
                [](const auto &room) { return room.preset == 167; });
            if (entrance == rooms_.end()) throw std::runtime_error("Missing barracks entrance");
            result.pieces.push_back({(entrance->x - minX) * maze_.width,
                (entrance->y - minY) * maze_.height, maze_.width, maze_.height, 167, entranceDirection, {}, {}});
            connectBarracks(court, result, courtPlacement.width, courtPlacement.height);
            result.pieces.clear();
            result.boundaries.clear(); // The recipe adapter connects regions once.
        }
    }
    result.ds1 =
        "maze-v1/" + std::to_string(level) + "/" + std::to_string(seed) + "/" + std::to_string(difficulty);
    if (level == 28)
        result.ds1 += "/" + std::to_string(entranceDirection);
    std::map<int, int> variants;
    for (auto i = rooms_.rbegin(); i != rooms_.rend(); ++i) {
        const auto &preset = catalog_.presets().at(i->preset);
        if (i->variant < 0 && preset.files < 1 && level != 74)
            throw std::runtime_error("Maze room has no selectable DS1 variants");
        // AllocDrlgMap always draws, including a later forced-file override.
        int variant = seed_.below(preset.files);
        if (i->variant >= 0) variant = i->variant;
        else if (type != 32 && level != 74 && !(level >= 51 && level <= 54) && level != 84 && level != 85 &&
            i->preset > base_ && i->preset < base_ + 16) {
            auto [it, inserted] = variants.try_emplace(i->preset, 0);
            if (inserted) it->second = seed_.below(preset.files);
            it->second = (it->second + 1) % preset.files;
            variant = it->second;
        }
        auto source = catalog_.preset(i->preset, result.levelType, variant);
        result.pieces.push_back({(i->x - minX) * maze_.width, (i->y - minY) * maze_.height, maze_.width,
                                 maze_.height, i->preset, variant, source.ds1, source.tileLibraries,
                                 source.fillBlanks, preset.populate, -1, source.killEdge, source.animationSpeed, 0, source.pops, source.popPad});
        const auto &piece = result.pieces.back();
        result.width = std::max(result.width, piece.x + piece.width);
        result.height = std::max(result.height, piece.y + piece.height);
        if (convert) convert(result, piece, seed_);
    }
    return result;
}
} // namespace d2x::maze
