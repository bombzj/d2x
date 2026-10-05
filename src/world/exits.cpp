#include "region.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <set>

namespace d2x {
void linkLevelExits(std::span<Region> regions, const WorldCatalog &catalog) {
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
    // Exit topology must survive an operable door being closed. Validate
    // connectivity with those door obstacles omitted; live movement still
    // uses the actual closed-door collision and requires an operation.
    std::map<RegionId, Grid> connections;
    for (const auto &region : regions) if (region.loaded) {
        auto &grid = connections.emplace(region.definition.id, region.map.grid).first->second;
        auto obstacles = grid.obstacles;
        std::erase_if(obstacles, [&](const auto &obstacle) {
            return std::any_of(region.objects.begin(), region.objects.end(), [&](const auto &object) {
                return object.id == obstacle.id && object.interaction == Interaction::Door &&
                    !object.hasCollision[2];
            });
        });
        grid.setObstacles(std::move(obstacles));
        grid.neighbours.clear();
    }
    for (const auto &region : regions) if (region.loaded) {
        auto &grid = connections.at(region.definition.id);
        for (const auto &neighbour : region.map.grid.neighbours) {
            const auto other = std::find_if(regions.begin(), regions.end(), [&](const auto &candidate) {
                return &candidate.map.grid == neighbour.grid;
            });
            auto copy = neighbour;
            copy.grid = &connections.at(other->definition.id);
            grid.neighbours.push_back(copy);
        }
    }
    std::map<RegionId, Bytes> reachable;
    for (const auto &region : regions)
        if (region.loaded && !region.recipe.boundaries.empty())
            reachable.emplace(region.definition.id, connections.at(region.definition.id).reachableFrom(region.map.spawn, playerMovement));
    for (auto &region : regions) {
        int id = int(region.definition.id);
        if (!region.loaded || !catalog.levels().contains(id))
            continue;
        const auto &level = catalog.level(id);
        const auto &data = region.map.terrain.data;
        std::set<int> seen;
        std::vector<MapTerrain::Exit> candidates;
        if (region.recipe.native) candidates = region.map.terrain.exits;
        else for (const auto &layer : data.walls)
            for (int y = 0; y < data.height; ++y)
                for (int x = 0; x < data.width; ++x) {
                    const auto &cell = layer[y * data.width + x];
                    if (!cell.occupied() || (cell.orientation != 10 && cell.orientation != 11)) continue;
                    const int sequence = (cell.value >> 8) & 255, slot = (cell.value >> 20) & 63;
                    if ((sequence != 0 && sequence != 4 && !(cell.value & 0x80000000u)) || slot >= 8 ||
                        !level.visible[slot] || level.warps[slot] < 0) continue;
                    const auto &warps = catalog.warps().at(level.warps[slot]);
                    const auto record = std::find_if(warps.begin(), warps.end(), [&](const auto &w) {
                        return w.direction == "b" || w.direction == (cell.orientation == 11 ? "r" : "l");
                    });
                    if (record == warps.end()) throw std::runtime_error("Missing LvlWarp direction");
                    candidates.push_back({slot, level.visible[slot], *record,
                        {float(x * 5 + record->offsetX), float(y * 5 + record->offsetY)}});
                }
        for (const auto &candidate : candidates) {
                    const int slot = candidate.slot;
                    if (seen.contains(slot)) continue;
                    const auto *record = &candidate.selection;
                    LevelExit exit;
                    exit.slot = slot;
                    exit.warp = record->id;
                    exit.destination = RegionId(candidate.destination);
                    exit.name = catalog.level(candidate.destination).name;
                    exit.selection = *record;
                    exit.position = candidate.position;
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
                        const auto connected = connections.at(region.definition.id).reachableFrom(region.map.spawn, playerMovement);
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
                            throw std::runtime_error("No reachable original stair approach: " + region.map.terrain.path +
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
                if (!connections.at(region.definition.id).walkable(pos, playerMovement) ||
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
                if (!paired || !connections.at(target->definition.id).walkable(across, playerMovement) ||
                    !reachable.at(target->definition.id)[size_t(int(across.y) * target->map.grid.width + int(across.x))])
                    continue;
                exit.passages.push_back({pos, across});
            }
            // Retail Vis includes touching jungle rectangles even when their
            // river/clearing branches do not connect. Preserve the sealed edge.
            if (exit.passages.empty() && region.recipe.native && catalog.level(int(region.definition.id)).levelType == 21 &&
                catalog.level(b.destination).levelType == 21) continue;
            if (exit.passages.empty())
                throw std::runtime_error(
                    "Original outdoor border is not passable: " + std::to_string(int(region.definition.id)) +
                    " -> " + std::to_string(b.destination) + " side=" + std::to_string(b.side) +
                    " plane=" + std::to_string(plane) + " span=" + std::to_string(b.start * 5) +
                    ":" + std::to_string(b.end * 5));
            std::stable_sort(exit.passages.begin(), exit.passages.end(), [&](const auto &left, const auto &right) {
                return (left.departure - region.map.spawn).length() < (right.departure - region.map.spawn).length();
            });
            bool selected = false;
            for (const auto &passage : exit.passages) {
                const auto approach = connections.at(region.definition.id).path(region.map.spawn, passage.departure, false, playerMovement);
                if (approach.empty()) continue;
                exit.position = passage.departure;
                exit.arrival = approach.size() > 1 ? approach[approach.size() - 2] : region.map.spawn;
                selected = true;
                break;
            }
            if (!selected)
                throw std::runtime_error("Disconnected boundary passages: " + region.map.terrain.path +
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
                if (exit.enabled && connections.at(region.definition.id).path(region.map.spawn, exit.arrival, false, playerMovement).empty()) {
                    std::cerr << "Disconnected warp position=" << exit.position.x << ',' << exit.position.y
                        << " arrival=" << exit.arrival.x << ',' << exit.arrival.y
                        << " spawn=" << region.map.spawn.x << ',' << region.map.spawn.y << '\n';
                    const auto connected = connections.at(region.definition.id).reachableFrom(region.map.spawn, playerMovement);
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
                    throw std::runtime_error("Disconnected original level exit: " + region.map.terrain.path +
                                             " slot=" + std::to_string(exit.slot));
                }
        }
    }
}
} // namespace d2x
