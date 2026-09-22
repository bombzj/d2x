// Cave/Crypt branches adapted from D2MOO DrlgMaze.cpp / D2Seed.h, MIT.
// Copyright (c) 2020-2025 The Phrozen Keep community. See docs/licenses/D2MOO.txt.
// Room graph rules follow the original branch; full engine RNG consumption is not reproduced.
#include "room_graph.hpp"
#include <algorithm>
#include <numeric>

namespace d2x::maze {
MapRecipe RoomMaze::build(int level, uint32_t seed, int difficulty) {
        if (difficulty < 0 || difficulty > 2 || maze_.width != family_.roomSize ||
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
        for (int preset : family_.specialRooms)
            place(preset);
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
}
