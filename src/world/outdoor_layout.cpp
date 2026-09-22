#include "outdoor_layout.hpp"
#include "generation_seed.hpp"
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
void connect(OutdoorPosition &a, OutdoorPosition &b) {
    int side = b.direction;
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
std::map<int, OutdoorPosition> layoutAct1(const WorldCatalog &catalog, uint32_t seed) {
    Seed rng(seed);
    std::map<int, OutdoorPosition> result;
    auto group = [&](std::vector<int> ids, std::vector<int> parents, bool wilderness) {
        std::vector<OutdoorPosition> positions;
        std::vector<int> offsets(ids.size());
        for (int id : ids) {
            const auto &r = catalog.level(id);
            positions.push_back({id, r.offsetX, r.offsetY, r.width, r.height, 0, {}});
        }
        int attempts = 0;
        std::function<bool(int)> place = [&](int i) {
            if (++attempts > 10000)
                throw std::runtime_error("Act I outdoor layout exhausted");
            if (i == int(ids.size()))
                return true;
            int id = ids[i];
            bool paired = id == 1 || id == 2;
            int first = rng.below(4), offset = paired ? rng.below(2) : 0;
            if (id == 7)
                first = 0;
            for (int candidate = 0; candidate < (id == 7 ? 1 : paired ? 8 : 4); ++candidate) {
                int d = (first + (paired ? (candidate + offset) / 2 : candidate)) % 4;
                bool opposite = paired && ((candidate + offset) % 2 == 0);
                offsets[i] = paired ? !opposite : 0;
                auto &p = positions[i];
                if (id == 2) {
                    p.width = d % 2 ? 96 : 56;
                    p.height = d % 2 ? 56 : 96;
                }
                attach(positions[parents[i]], p, d, id == 1 ? 2 : id == 7 ? 0 : 1, opposite);
                bool valid = true;
                for (int j = 0; j < i; ++j)
                    if (j != parents[i] && overlaps(p, positions[j]))
                        valid = false;
                if (wilderness && id == 1) {
                    constexpr int townAllowed[]{1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                                                0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 1,
                                                0, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0,
                                                0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1, 1};
                    int parent = parents[i];
                    valid = valid && townAllowed[d + 4 * (offsets[i] + 2 * (positions[parent].direction +
                                                                            4 * offsets[parent]))];
                }
                if (valid && place(i + 1))
                    return true;
            }
            return false;
        };
        if (!place(1))
            throw std::runtime_error("Cannot place original Act I outdoor links");
        for (int i = 1; i < int(ids.size()); ++i)
            connect(positions[parents[i]], positions[i]);
        for (auto &p : positions)
            result.emplace(p.level, std::move(p));
    };
    group({4, 3, 2, 1, 17}, {-1, 0, 1, 2, 1}, true);
    group({26, 7, 6, 5}, {-1, 0, 1, 2}, false);
    return result;
}
} // namespace d2x
