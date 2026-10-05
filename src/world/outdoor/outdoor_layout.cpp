#include "outdoor_layout.hpp"
#include "native_act_layout.hpp"
#include "world/generation_seed.hpp"
#include <algorithm>
#include <functional>

// Act I link tables / rectangle placement adapted from D2MOO DrlgOutPlace.cpp, MIT.
// Copyright (c) 2020-2025 The Phrozen Keep community. See docs/licenses/D2MOO.txt.
namespace d2x {
namespace {
void attach(const OutdoorPosition &a, OutdoorPosition &b, int direction, int offset, bool opposite) {
    b.direction = direction;
    switch (direction) {
    case 0:
        b.x = opposite ? a.x + a.width - b.width : a.x;
        b.y = a.y + a.height;
        if (offset == 1)
            b.x += opposite ? 16 : -16;
        break;
    case 1:
        b.x = a.x - b.width;
        b.y = opposite ? a.y + a.height - b.height : a.y;
        if (offset == 1)
            b.y += opposite ? 16 : -16;
        if (offset == 2)
            b.y += opposite ? -8 : 8;
        break;
    case 2:
        b.x = opposite ? a.x : a.x + a.width - b.width;
        b.y = a.y - b.height;
        if (offset == 1)
            b.x += opposite ? -16 : 16;
        break;
    case 3:
        b.x = a.x + a.width;
        b.y = opposite ? a.y : a.y + a.height - b.height;
        if (offset == 1)
            b.y += opposite ? -16 : 16;
        if (offset == 2)
            b.y += opposite ? 8 : -8;
        break;
    }
}
bool overlaps(const OutdoorPosition &a, const OutdoorPosition &b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}
// Native directions: 0 south, 1 west, 2 north, 3 east.
void connect(OutdoorPosition &a, OutdoorPosition &b, int side) {
    bool vertical = side % 2;
    int from = std::max(vertical ? a.y : a.x, vertical ? b.y : b.x);
    int to = std::min(vertical ? a.y + a.height : a.x + a.width, vertical ? b.y + b.height : b.x + b.width);
    if (to - from < 24)
        throw std::runtime_error("Outdoor link has insufficient overlap");
    // The native border generator opens the midpoint macrocell of the overlap.
    int start = from + ((to - from - 8) / 16) * 8;
    a.boundaries.push_back({b.level, side, start - (vertical ? a.y : a.x), start + 8 - (vertical ? a.y : a.x),
                            from - (vertical ? a.y : a.x), to - (vertical ? a.y : a.x)});
    b.boundaries.push_back({a.level, (side + 2) % 4, start - (vertical ? b.y : b.x),
                            start + 8 - (vertical ? b.y : b.x), from - (vertical ? b.y : b.x),
                            to - (vertical ? b.y : b.x)});
}
} // namespace
std::map<int, OutdoorPosition> layoutAct2(const WorldCatalog &catalog, uint32_t seed) {
    Seed random(seed);
    std::map<int, OutdoorPosition> result;
    for (int id = 40; id <= 46; ++id) {
        const auto &level = catalog.level(id);
        result.emplace(id, OutdoorPosition{id, level.offsetX, level.offsetY, level.width, level.height, 0, {}});
    }
    int attempts = 0;
    std::function<bool(int)> place = [&](int id) {
        if (++attempts > 10000)
            throw std::runtime_error("Act II outdoor layout exhausted");
        if (id == 46)
            return true;
        int first = id == 41 ? 1 + random.below(2) : random.below(4);
        for (int candidate = 0; candidate < (id == 41 ? 2 : 4); ++candidate) {
            int direction = id == 41 ? 1 + (first - 1 + candidate) % 2 : (first + candidate) % 4;
                 attach(result.at(id - 1), result.at(id), direction, id == 41 || id == 45 ? 0 : 1,
                     id == 41 && direction == 2);
            bool valid = true;
            for (int previous = 40; previous < id - 1; ++previous)
                if (overlaps(result.at(id), result.at(previous)))
                    valid = false;
            if (valid && place(id + 1))
                return true;
        }
        return false;
    };
    if (!place(41))
        throw std::runtime_error("Cannot place original Act II outdoor links");
    result.at(40).direction = result.at(41).direction;
    for (int id = 41; id <= 45; ++id)
        connect(result.at(id - 1), result.at(id), result.at(id).direction);
    return result;
}
std::map<int, OutdoorPosition> layoutAct4(const WorldCatalog &catalog, uint32_t seed) {
    Seed random(seed);
    std::map<int, OutdoorPosition> result;
    for (int id = 103; id <= 106; ++id) {
        const auto &level = catalog.level(id);
        result.emplace(id, OutdoorPosition{id, level.offsetX, level.offsetY,
            level.width, level.height, 0, {}});
    }
    const bool opposite = !(random.next() & 1);
    attach(result.at(103), result.at(104), 3, 0, opposite);
    result.at(104).y += opposite ? -8 : 8;
    result.at(104).flags = opposite ? 0x400000 : 0x800000;
    int attempts = 0;
    std::function<bool(int)> place = [&](int id) {
        if (++attempts > 10000) throw std::runtime_error("Act IV outdoor layout exhausted");
        if (id == 107) return true;
        const int first = random.next() & 3;
        for (int candidate = 0; candidate < 4; ++candidate) {
            attach(result.at(id - 1), result.at(id), (first + candidate) % 4, 1, false);
            bool valid = true;
            for (int previous = 103; previous < id - 1; ++previous)
                if (overlaps(result.at(id), result.at(previous))) valid = false;
            if (valid && place(id + 1)) return true;
        }
        return false;
    };
    if (!place(105)) throw std::runtime_error("Cannot place original Act IV outdoor links");
    for (int id = 104; id <= 106; ++id) connect(result.at(id - 1), result.at(id), result.at(id).direction);
    auto &townBoundary = result.at(103).boundaries.front();
    auto &mesaBoundary = result.at(104).boundaries.front();
    const int row = opposite ? 8 : 32;
    mesaBoundary.start = row;
    mesaBoundary.end = row + 24;
    townBoundary.start = result.at(104).y + row - result.at(103).y;
    townBoundary.end = townBoundary.start + 24;
    return result;
}
std::map<int, OutdoorPosition> layoutAct1(const WorldCatalog &catalog, uint32_t seed) {
    const auto act = placeNativeAct(catalog, 0, seed);
    std::map<int, OutdoorPosition> result;
    // Keep the recipe adapter's existing scope. Interior levels and the secret
    // cow area use their own generation stages, but share this act placement.
    for (const int id : {1, 2, 3, 4, 5, 6, 7, 17, 26}) {
        const auto &p = act.levels.at(id);
        result.emplace(id, OutdoorPosition{id, p.x, p.y, p.width, p.height,
            p.direction, {}, p.outdoorFlags});
    }
    for (auto &[id, a] : result) {
        const auto &slots = act.connections.at(id);
        for (size_t slot = 0; slot < slots.visible.size(); ++slot) {
            const int destination = slots.visible[slot];
            if (slots.warps[slot] != -1 || id >= destination || !result.contains(destination)) continue;
            auto &b = result.at(destination);
            // The legacy recipe carries south/west/north/east boundaries. It
            // does not determine the DRLG link graph or consume random draws.
            int side;
            if (b.x == a.x + a.width) side = 3;
            else if (a.x == b.x + b.width) side = 1;
            else if (b.y == a.y + a.height) side = 0;
            else if (a.y == b.y + b.height) side = 2;
            else throw std::runtime_error("Native Act I recipe link has no shared edge");
            connect(a, b, side);
        }
    }
    return result;
}
} // namespace d2x
