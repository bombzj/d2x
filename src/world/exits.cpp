#include "region.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <set>

namespace d2x {
void linkLevelExits(std::vector<Region> &regions, const WorldCatalog &catalog) {
    for (auto &region : regions) {
        region.exits.clear();
        region.map.warpArrivals.clear();
        auto &neighbours = region.map.grid.neighbours;
        neighbours.clear();
        if (!region.loaded) continue;
        for (const auto &boundary : region.recipe.boundaries) {
            auto other = std::find_if(regions.begin(), regions.end(), [&](const auto &candidate) {
                return int(candidate.definition.id) == boundary.destination;
            });
            if (other == regions.end() || !other->loaded || !std::any_of(other->recipe.boundaries.begin(), other->recipe.boundaries.end(),
                [&](const auto &back) { return back.destination == int(region.definition.id) &&
                    back.side == (boundary.side + 2) % 4; })) continue;
            neighbours.push_back({&other->map.grid,
                (region.recipe.worldX - other->recipe.worldX) * 5,
                (region.recipe.worldY - other->recipe.worldY) * 5,
                boundary.side, boundary.coordinate(region.recipe.width, region.recipe.height) * 5,
                boundary.start * 5, boundary.end * 5});
        }
    }
    std::map<RegionId, Bytes> reachable;
    for (const auto &region : regions)
        if (region.loaded && !region.recipe.boundaries.empty())
            reachable.emplace(region.definition.id, region.map.grid.reachableFrom(region.map.spawn, playerMovement));
    for (auto &region : regions) {
        int id = int(region.definition.id);
        if (!region.loaded || !catalog.levels().contains(id))
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
                                                           Vec{float(record->exitX), float(record->exitY)}, playerMovement);
                    exit.accessPoint = exit.arrival;
                    const auto *warpRoom = region.map.activation.room(exit.position);
                    const WorldObject *stair = nullptr;
                    for (const auto &object : region.objects)
                        if (object.interaction == Interaction::Stair &&
                            region.map.activation.room(object.pos) == warpRoom &&
                            (!stair || (object.pos - exit.position).length() < (stair->pos - exit.position).length()))
                            stair = &object;
                    if (stair) {
                        exit.stairObject = stair->id;
                        const auto connected = region.map.grid.reachableFrom(region.map.spawn, playerMovement);
                        float bestDistance = std::numeric_limits<float>::infinity();
                        std::optional<Vec> approach;
                        const int radius = std::max(stair->collisionWidth, stair->collisionHeight) / 2 + 3;
                        for (int row = int(stair->pos.y) - radius; row <= int(stair->pos.y) + radius; ++row)
                            for (int column = int(stair->pos.x) - radius; column <= int(stair->pos.x) + radius; ++column) {
                                if (!region.map.grid.walkable(column, row, playerMovement)) continue;
                                if (!connected[size_t(row) * region.map.grid.width + column]) continue;
                                const Vec point{column + .5f, row + .5f};
                                const float distance = (point - exit.arrival).length();
                                if (distance >= bestDistance || !region.map.grid.interactionSegment(point,
                                    stair->pos, stair->collisionWidth, stair->id)) continue;
                                approach = point;
                                bestDistance = distance;
                            }
                        if (!approach) {
                            throw std::runtime_error("No reachable original stair approach: " + region.map.path +
                            " slot=" + std::to_string(slot) + " destination=" + std::to_string(int(exit.destination)) +
                            " object=" + std::to_string(stair->objectClass) +
                            " stair=" + std::to_string(stair->pos.x) + "," + std::to_string(stair->pos.y) +
                            " warp=" + std::to_string(exit.position.x) + "," + std::to_string(exit.position.y));
                        }
                        exit.accessPoint = exit.arrival = *approach;
                    }
                    region.exits.push_back(exit);
                    seen.insert(slot);
                }
    }
    for (auto &region : regions) {
        if (!region.loaded) continue;
        for (const auto &b : region.recipe.boundaries) {
            auto target = std::find_if(regions.begin(), regions.end(),
                                       [&](const auto &r) { return int(r.definition.id) == b.destination; });
            if (target == regions.end() || !target->loaded)
                continue;
            LevelExit exit;
            exit.slot = 8 + b.destination;
            exit.warp = -1;
            exit.boundary = b;
            exit.destination = target->definition.id;
            exit.name = target->definition.name;
            const auto &r = region.recipe;
            int plane = b.coordinate(r.width, r.height) * 5;
            for (int t = b.start * 5; t < b.end * 5; ++t) {
                Vec pos{b.side == 1   ? plane + .5f
                        : b.side == 3 ? plane - .5f
                                      : t + .5f,
                        b.side == 2   ? plane + .5f
                        : b.side == 0 ? plane - .5f
                                      : t + .5f};
                if (!region.map.grid.walkable(pos, playerMovement) ||
                    !reachable.at(region.definition.id)[size_t(int(pos.y) * region.map.grid.width + int(pos.x))])
                    continue;
                Vec across = pos + Vec{float((r.worldX - target->recipe.worldX) * 5),
                                       float((r.worldY - target->recipe.worldY) * 5)};
                constexpr int outwardX[]{0, -1, 0, 1}, outwardY[]{1, 0, -1, 0};
                across = across + Vec{float(outwardX[b.side]), float(outwardY[b.side])};
                const auto &other = target->recipe;
                const bool paired = std::any_of(other.boundaries.begin(), other.boundaries.end(),
                    [&](const auto &back) {
                        if (back.destination != int(region.definition.id) || back.side != (b.side + 2) % 4)
                            return false;
                        const float lateral = back.side % 2 ? across.y : across.x;
                        const float normal = back.side % 2 ? across.x : across.y;
                        const float plane = back.coordinate(other.width, other.height) * 5.f;
                        const float inside = back.side == 1 || back.side == 2 ? .5f : -.5f;
                        return lateral >= back.start * 5 && lateral < back.end * 5 &&
                            std::abs(normal - plane - inside) < .01f;
                    });
                if (!paired || !target->map.grid.walkable(across, playerMovement) ||
                    !reachable.at(target->definition.id)[size_t(int(across.y) * target->map.grid.width + int(across.x))])
                    continue;
                exit.passages.push_back({pos, across});
            }
            if (exit.passages.empty())
                throw std::runtime_error(
                    "Original outdoor border is not passable: " + std::to_string(int(region.definition.id)) +
                    " -> " + std::to_string(b.destination));
            std::stable_sort(exit.passages.begin(), exit.passages.end(), [&](const auto &left, const auto &right) {
                return (left.departure - region.map.spawn).length() < (right.departure - region.map.spawn).length();
            });
            bool selected = false;
            for (const auto &passage : exit.passages) {
                const auto approach = region.map.grid.path(region.map.spawn, passage.departure, false, playerMovement);
                if (approach.empty()) continue;
                exit.position = passage.departure;
                exit.arrival = approach.size() > 1 ? approach[approach.size() - 2] : region.map.spawn;
                selected = true;
                break;
            }
            if (!selected)
                throw std::runtime_error("Disconnected boundary passages: " + region.map.path +
                                         " -> " + std::to_string(b.destination));
            exit.accessPoint = exit.position;
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
                exit.enabled = !destination->loaded ||
                    std::any_of(destination->exits.begin(), destination->exits.end(),
                                [&](const auto &back) {
                                    return back.destination == region.definition.id &&
                                        bool(back.boundary) == bool(exit.boundary);
                                });
            std::cout << "  Exit " << int(region.definition.id) << ':' << exit.slot << " -> "
                      << int(exit.destination) << " warp=" << exit.warp << " at=" << exit.position.x << ','
                      << exit.position.y << " arrival=" << exit.arrival.x << ',' << exit.arrival.y
                      << (exit.enabled ? " linked" : " destination unavailable") << '\n';
            region.map.warpArrivals.push_back(exit.arrival);
        }
        if (!region.exits.empty() && !region.definition.safe)
            region.map.spawn = region.exits.front().arrival;
        if (!region.recipe.pieces.empty() || int(region.definition.id) == 26 ||
            int(region.definition.id) == 27 || int(region.definition.id) == 32 ||
            int(region.definition.id) == 33) {
            for (const auto &exit : region.exits)
                if (exit.enabled && region.map.grid.path(region.map.spawn, exit.arrival, false, playerMovement).empty()) {
                    std::cerr << "Disconnected warp position=" << exit.position.x << ',' << exit.position.y
                        << " arrival=" << exit.arrival.x << ',' << exit.arrival.y
                        << " spawn=" << region.map.spawn.x << ',' << region.map.spawn.y << '\n';
                    const auto connected = region.map.grid.reachableFrom(region.map.spawn, playerMovement);
                    for (int row = int(exit.position.y) - 8; row <= int(exit.position.y) + 12; ++row) {
                        for (int column = int(exit.position.x) - 8; column <= int(exit.position.x) + 12; ++column)
                            std::cerr << (region.map.grid.walkable(column, row, playerMovement)
                                ? connected[size_t(row) * region.map.grid.width + column] ? '.' : 'o' : '#');
                        std::cerr << '\n';
                    }
                    for (const auto &object : region.objects)
                        if ((object.pos - exit.position).length() < 20)
                            std::cerr << "  nearby object=" << object.objectClass << " at=" << object.pos.x
                                << ',' << object.pos.y << " collision=" << object.collisionMask << '\n';
                    throw std::runtime_error("Disconnected original level exit: " + region.map.path +
                                             " slot=" + std::to_string(exit.slot));
                }
        }
    }
}
} // namespace d2x
