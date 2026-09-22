#include "region.hpp"
#include <algorithm>
#include <iostream>
#include <set>

namespace d2x {
void linkLevelExits(std::vector<Region> &regions, const WorldCatalog &catalog) {
    // Levels 1..26 form the implemented route; later indoor families remain separate.
    for (auto &region : regions) {
        int id = int(region.definition.id);
        if (id < 1 || id > 25)
            continue;
        const auto &level = catalog.level(id);
        const auto &data = region.map.data;
        std::set<int> seen;
        for (const auto &layer : data.walls)
            for (int y = 0; y < data.height; ++y)
                for (int x = 0; x < data.width; ++x) {
                    const auto &cell = layer[y * data.width + x];
                    if (!cell.occupied() || (cell.orientation != 10 && cell.orientation != 11))
                        continue;
                    int sequence = (cell.value >> 8) & 255, slot = (cell.value >> 20) & 63;
                    if ((sequence != 0 && sequence != 4 && !(cell.value & 0x80000000u)) || slot >= 8 ||
                        !level.visible[slot] || level.warps[slot] < 0 || seen.contains(slot))
                        continue;
                    const auto &warps = catalog.warps().at(level.warps[slot]);
                    auto record = std::find_if(warps.begin(), warps.end(), [&](const auto &w) {
                        return w.direction == "b" || w.direction == (cell.orientation == 11 ? "r" : "l");
                    });
                    if (record == warps.end())
                        throw std::runtime_error("Missing LvlWarp direction");
                    LevelExit exit;
                    exit.slot = slot;
                    exit.warp = record->id;
                    exit.destination = RegionId(level.visible[slot]);
                    exit.name = catalog.level(level.visible[slot]).name;
                    exit.selection = *record;
                    exit.position = {float(x * 5 + record->offsetX), float(y * 5 + record->offsetY)};
                    exit.arrival = region.map.grid.nearest(exit.position +
                                                           Vec{float(record->exitX), float(record->exitY)});
                    exit.accessPoint = exit.arrival;
                    region.exits.push_back(exit);
                    seen.insert(slot);
                }
    }
    for (auto &region : regions) {
        for (const auto &b : region.recipe.boundaries) {
            auto target = std::find_if(regions.begin(), regions.end(),
                                       [&](const auto &r) { return int(r.definition.id) == b.destination; });
            if (target == regions.end())
                continue;
            LevelExit exit;
            exit.slot = 8 + b.destination;
            exit.warp = -1;
            exit.boundary = b;
            exit.destination = target->definition.id;
            exit.name = target->definition.name;
            const auto &r = region.recipe;
            int best = -1;
            float score = 1e9f;
            for (int t = b.start * 5 + 2; t < b.end * 5 - 2; ++t) {
                Vec pos{b.side == 1   ? .5f
                        : b.side == 3 ? r.width * 5 - .5f
                                      : t + .5f,
                        b.side == 2   ? .5f
                        : b.side == 0 ? r.height * 5 - .5f
                                      : t + .5f};
                if (!region.map.grid.walkable(pos))
                    continue;
                float value = std::abs(t - (b.start + b.end) * 2.5f);
                if (value < score) {
                    score = value;
                    best = t;
                    exit.position = pos;
                }
            }
            if (best < 0)
                throw std::runtime_error(
                    "Original outdoor border is not passable: " + std::to_string(int(region.definition.id)) +
                    " -> " + std::to_string(b.destination));
            exit.accessPoint = exit.position;
            constexpr int inwardX[]{0, 1, 0, -1}, inwardY[]{-1, 0, 1, 0};
            exit.arrival =
                region.map.grid.nearest(exit.position + Vec{inwardX[b.side] * 8.f, inwardY[b.side] * 8.f});
            region.exits.push_back(exit);
        }
    }
    for (auto &region : regions) {
        std::sort(region.exits.begin(), region.exits.end(),
                  [](const auto &a, const auto &b) { return a.slot < b.slot; });
        for (auto &exit : region.exits) {
            auto destination = std::find_if(regions.begin(), regions.end(), [&](const auto &r) {
                return r.definition.id == exit.destination;
            });
            if (destination != regions.end())
                exit.enabled =
                    std::any_of(destination->exits.begin(), destination->exits.end(),
                                [&](const auto &back) { return back.destination == region.definition.id; });
            std::cout << "  Exit " << int(region.definition.id) << ':' << exit.slot << " -> "
                      << int(exit.destination) << " warp=" << exit.warp << " at=" << exit.position.x << ','
                      << exit.position.y << " arrival=" << exit.arrival.x << ',' << exit.arrival.y
                      << (exit.enabled ? " linked" : " destination unavailable") << '\n';
            region.map.warpArrivals.push_back(exit.arrival);
        }
        if (!region.exits.empty() && !region.definition.safe)
            region.map.spawn = region.exits.front().arrival;
        if (!region.recipe.pieces.empty()) {
            for (const auto &exit : region.exits)
                if (exit.enabled && region.map.grid.path(region.map.spawn, exit.arrival).empty())
                    throw std::runtime_error("Disconnected original level exit: " + region.map.path +
                                             " slot=" + std::to_string(exit.slot));
        }
    }
}
} // namespace d2x
