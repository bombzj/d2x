#include "object_population.hpp"
#include "generation_seed.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace d2x {
namespace {
int integer(const std::map<std::string, std::string> &row, const std::string &key) {
    auto found = row.find(key);
    return found == row.end() || found->second.empty() ? 0 : std::stoi(found->second);
}
const std::map<std::string, std::string> *byNumber(const Table &rows, const char *column, int value) {
    auto found = std::find_if(rows.begin(), rows.end(), [&](const auto &row) {
        return integer(row, column) == value;
    });
    return found == rows.end() ? nullptr : &*found;
}
bool clear(const Region &region, Vec point, int extent, bool avoidObjects = true) {
    const auto &grid = region.map.grid;
    for (int y = int(point.y) - extent; y <= int(point.y) + extent; ++y)
        for (int x = int(point.x) - extent; x <= int(point.x) + extent; ++x)
            if (!grid.walkable(x, y)) return false;
    if (avoidObjects)
        for (const auto &object : region.objects)
            if ((object.pos - point).length() < float(extent + 3)) return false;
    return true;
}
bool clearChest(const Region &region, Vec point, int width, int height) {
    // OBJECTS_CreateObject / COLLISION_CheckMaskWithSizeXY: native size plus six.
    // Objects have not yet been installed in Grid, so include their real footprints.
    const int left = int(point.x) - width / 2, top = int(point.y) - height / 2;
    const auto &grid = region.map.grid;
    constexpr uint16_t placementMask = 0x3f11;
    for (int y = top; y < top + height; ++y)
        for (int x = left; x < left + width; ++x) {
            if (x < 0 || y < 0 || x >= grid.width || y >= grid.height) return false;
            const auto index = size_t(y) * grid.width + x;
            const uint16_t collision = grid.fullTerrainCollision.empty()
                ? grid.terrainCollision[index] : grid.fullTerrainCollision[index];
            if (collision & placementMask) return false;
        }
    for (const auto &object : region.objects) {
        if (object.questHidden || !object.hasCollision[size_t(object.modeAt(0))] ||
            !(object.collisionMask & placementMask)) continue;
        const int x = int(object.pos.x) - object.collisionWidth / 2;
        const int y = int(object.pos.y) - object.collisionHeight / 2;
        if (left < x + object.collisionWidth && left + width > x &&
            top < y + object.collisionHeight && top + height > y) return false;
    }
    return true;
}
} // namespace
void populateAct1WorldObjects(Region &region, EntityIds &ids, const WorldCatalog &catalog,
                              const Table &objectRows, const Table &groupRows, uint32_t worldSeed,
                              std::vector<int> *deferred) {
    const auto levelId = int(region.definition.id);
    const auto level = catalog.levels().find(levelId);
    if (level == catalog.levels().end() || level->second.act != 0 || region.definition.safe)
        return;
    Seed parent(worldSeed);
    std::vector<Vec> shrineCoordinates;
    std::vector<Vec> wellCoordinates;
    const int populatedRooms = int(std::count_if(region.map.rooms.begin(), region.map.rooms.end(),
        [](const Map::RoomBounds &room) { return room.populate; }));
    for (size_t roomIndex = 0; roomIndex < region.map.rooms.size(); ++roomIndex) {
        const auto &room = region.map.rooms[roomIndex];
        if (!room.populate || room.width < 8 || room.height < 8) continue;
        if (std::any_of(region.objects.begin(), region.objects.end(), [&](const WorldObject &object) {
                return object.operateFn == 23 && object.pos.x >= room.x && object.pos.y >= room.y &&
                    object.pos.x < room.x + room.width && object.pos.y < room.y + room.height;
            })) continue;
        Seed selection(parent.next());
        for (size_t slot = 0; slot < level->second.objectGroups.size(); ++slot) {
            const int groupId = level->second.objectGroups[slot];
            const int probability = selection.below(100);
            if (!groupId || probability > level->second.objectProbabilities[slot]) continue;
            const auto *group = byNumber(groupRows, "Offset", groupId);
            if (!group) continue;
            const int choice = selection.below(100);
            int total = 0, objectId = 0, density = 0;
            for (int candidate = 0; candidate < 8; ++candidate) {
                const auto suffix = std::to_string(candidate);
                const int id = integer(*group, "ID" + suffix);
                if (!id) break;
                total += integer(*group, "PROB" + suffix);
                const auto *candidateRecord = byNumber(objectRows, "Id", id);
                if (choice < total && candidateRecord && integer(*candidateRecord, "Gore") <= 2) {
                    objectId = id;
                    density = integer(*group, "DENSITY" + suffix);
                    break;
                }
            }
            const auto *record = objectId ? byNumber(objectRows, "Id", objectId) : nullptr;
            if (!record) continue;
            const int operation = integer(*record, "OperateFn");
            const int populate = integer(*record, "PopulateFn");
            const bool shrine = operation == 2 && populate == 2 &&
                                normalize(record->at("Name")) == "shrine";
            const bool well = operation == 22 && populate == 8;
            const bool casket = operation == 1 && (populate == 1 || populate == 3) &&
                                (normalize(record->at("Name")) == "casket" ||
                                 normalize(record->at("Name")) == "sarcophagus");
            const bool barrel = operation == 5 && populate == 4;
            const bool common = populate == 3 || populate == 6 || populate == 9;
            if (!(common || casket || barrel || shrine || well ||
                  (operation == 14 && populate == 7))) {
                if (populate) {
                    ++region.unsupportedObjects;
                    if (deferred && std::find(deferred->begin(), deferred->end(), objectId) == deferred->end())
                        deferred->push_back(objectId);
                }
                continue;
            }
            if (shrine) {
                const int existing = int(shrineCoordinates.size());
                if (existing >= 10 || existing > populatedRooms / 8) continue;
            }
            if (well) {
                const int existing = int(wellCoordinates.size());
                if (existing >= 4 || existing > populatedRooms / 8) continue;
            }
            int count = std::clamp(density * ((room.width * room.height) >> 7) >> 8, 0, 32);
            if (common) count = std::max(0, density * ((room.width * room.height) >> 7) >> 8);
            if (barrel) count = std::max(0, density * ((room.width * room.height) >> 7) >> 8);
            Seed placement(selection.next());
            placement.below(100);
            const int size = std::max(integer(*record, "SizeX"), integer(*record, "SizeY"));
            const int sizeX = integer(*record, "SizeX"), sizeY = integer(*record, "SizeY");
            const int placementSizeX = well ? 2 * sizeX + 1 : sizeX;
            const int placementSizeY = well ? 2 * sizeY + 1 : sizeY;
            if ((common || shrine || well) &&
                (room.width <= placementSizeX + 1 || room.height <= placementSizeY + 1)) continue;
            const int extent = well ? size + 4 : size / 2 + 3;
            const auto explodingBarrel = barrel ? std::find_if(objectRows.begin(), objectRows.end(),
                [](const auto &row) {
                    return normalize(row.at("Name")) == "barrel" && integer(row, "OperateFn") == 7;
                }) : objectRows.end();
            auto add = [&](Vec point, int index, bool clustered = false,
                           const std::map<std::string, std::string> *appearance = nullptr) {
                if (barrel ? !clearChest(region, point, sizeX + 2, sizeY + 2)
                    : (common || shrine || well)
                    ? !clearChest(region, point, placementSizeX + 6, placementSizeY + 6)
                    : !clear(region, point, extent, !clustered)) return false;
                if (!appearance) appearance = record;
                WorldObject object;
                object.act = region.map.terrain.data.act;
                const auto level = catalog.levels().find(int(region.definition.id));
                object.palette = level == catalog.levels().end() ? object.act : level->second.palette;
                object.id = ids.allocate();
                object.contentKey = "objgroup." + std::to_string(roomIndex) + "." +
                                    std::to_string(slot) + "." + std::to_string(index);
                object.pos = {std::floor(point.x), std::floor(point.y)};
                object.accessPoint = region.map.grid.nearest(point);
                object.appearance = {"objects", normalize(appearance->at("Token")), "nu", "hth", {}};
                object.objectClass = integer(*appearance, "Id");
                configureWorldObject(object, objectRows);
                object.facing = (int(point.x) + int(point.y)) % 8;
                region.objects.push_back(std::move(object));
                return true;
            };
            if (barrel) {
                static constexpr int directionX[]{-1, 0, 1, -1, 1, -1, 0, 1};
                static constexpr int directionY[]{-1, -1, -1, 0, 0, 1, 1, 1};
                const int spacingX = integer(*record, "XSpace"), spacingY = integer(*record, "YSpace");
                const auto eligible = [&](Vec point) {
                    return point.x > room.x + 2 && point.y > room.y + 2 &&
                        point.x < room.x + room.width - sizeX && point.y < room.y + room.height - sizeY &&
                        clearChest(region, point, sizeX + 2, sizeY + 2);
                };
                int created = 0;
                for (int budget = 2 * count; count > 0 && budget > 0 && created < 8; --budget) {
                    const bool exploding = placement.below(3) == 0;
                    Vec point{float(room.x + placement.below(room.width)),
                              float(room.y + placement.below(room.height))};
                    if (!eligible(point)) continue;
                    const auto *appearance = exploding && explodingBarrel != objectRows.end()
                        ? &*explodingBarrel : record;
                    if (!add(point, created, false, appearance)) continue;
                    if (++created >= 8) break;
                    int clusterSize = 1;
                    bool placed = true;
                    while (placement.below(clusterSize / 2) == 0 && placed) {
                        placed = false;
                        for (int attempt = 0; attempt < 15 && !placed; ++attempt) {
                            const int direction = placement.below(8);
                            point.x += float(spacingX * directionX[direction]);
                            point.y += float(spacingY * directionY[direction]);
                            placed = eligible(point);
                        }
                        if (placed) {
                            const bool explodingMember = placement.below(4) == 0;
                            appearance = explodingMember && explodingBarrel != objectRows.end()
                                ? &*explodingBarrel : record;
                            if (!add(point, created, false, appearance)) break;
                            ++clusterSize;
                            if (++created >= 8) break;
                        }
                    }
                    --count;
                }
                continue;
            }
            if (populate == 7) {
                if (room.width <= 18 || room.height <= 18) continue;
                // D2MOO PopulateFn7 arranges staked rogues in one of four authored shapes.
                static constexpr int offsets[4][7][2] = {
                    {{-4,0},{0,0},{4,0},{8,0},{0,4},{0,-4},{100,100}},
                    {{-8,0},{-4,0},{0,0},{4,0},{8,0},{100,100},{100,100}},
                    {{0,-8},{0,-4},{0,0},{0,4},{0,8},{100,100},{100,100}},
                    {{-7,-5},{-5,-3},{-3,-1},{0,0},{3,-1},{5,-3},{7,-5}}
                };
                const int shape = placement.below(4);
                for (int attempt = 0; attempt < 8; ++attempt) {
                    const int x = room.x + 9 + placement.below(room.width - 18);
                    const int y = room.y + 9 + placement.below(room.height - 18);
                    const Vec base{float(x) + .5f, float(y) + .5f};
                    if (!clear(region, base, 6)) continue;
                    for (int index = 0; index < 7; ++index) {
                        const auto [dx, dy] = offsets[shape][index];
                        if (dx == 100) break;
                        add(base + Vec{float(dx), float(dy)}, index, true);
                    }
                    break;
                }
                continue;
            }
            if (shrine) {
                for (int attempt = 0; attempt < 15; ++attempt) {
                    const int orientation = integer(*record, "Orientation");
                    const int x = orientation == 1 ? room.x + room.width / 4 + placement.below(room.width / 2)
                        : orientation == 2 ? room.x + 1 + placement.below(1)
                        : room.x + placement.below(room.width - sizeX - 1);
                    const int y = orientation == 1 ? room.y + 1 + placement.below(1)
                        : orientation == 2 ? room.y + room.height / 4 + placement.below(room.height / 2)
                        : room.y + placement.below(room.height - sizeY - 1);
                    if (x < room.x + 1 || y < room.y + 1 ||
                        x >= room.x + room.width - 1 || y >= room.y + room.height - 1) continue;
                    const Vec point{float(x), float(y)};
                    bool nearShrine = std::any_of(shrineCoordinates.begin(), shrineCoordinates.end(),
                        [&](Vec existing) {
                            return std::abs(existing.x - point.x) < 50 ||
                                std::abs(existing.y - point.y) < 50;
                        });
                    if (!nearShrine && add(point, 0)) {
                        shrineCoordinates.push_back(point);
                        break;
                    }
                }
                continue;
            }
            if (well) {
                for (int attempt = 0; attempt < 5; ++attempt) {
                    const int x = room.x + placement.below(room.width - placementSizeX - 1);
                    const int y = room.y + placement.below(room.height - placementSizeY - 1);
                    if (x < room.x + 1 || y < room.y + 1 ||
                        x >= room.x + room.width - 1 || y >= room.y + room.height - 1) continue;
                    const Vec point{float(x), float(y)};
                    bool nearWell = std::any_of(wellCoordinates.begin(), wellCoordinates.end(),
                        [&](Vec existing) {
                            return std::abs(existing.x - point.x) < 100 ||
                                std::abs(existing.y - point.y) < 100;
                        });
                    if (!nearWell && add(point, 0)) {
                        wellCoordinates.push_back(point);
                        break;
                    }
                }
                continue;
            }
            bool soulCreated = false;
            Vec lastCreated{};
            bool hasLastCreated = false;
            for (int index = 0; index < count && !soulCreated; ++index) {
                hasLastCreated = false;
                for (int attempt = 0; attempt < 5; ++attempt) {
                    const int x = common ? room.x + placement.below(room.width - sizeX - 1)
                                        : room.x + 1 + placement.below(room.width - 2);
                    const int y = common ? room.y + placement.below(room.height - sizeY - 1)
                                        : room.y + 1 + placement.below(room.height - 2);
                    if (common && (x < room.x + 1 || y < room.y + 1 ||
                                  x >= room.x + room.width - 1 || y >= room.y + room.height - 1)) continue;
                    const Vec point{float(x) + .5f, float(y) + .5f};
                    if (add(point, index)) {
                        lastCreated = point;
                        hasLastCreated = true;
                        soulCreated = populate == 9;
                        break;
                    }
                }
            }
            if (populate == 6 && hasLastCreated && placement.below(100) > 70) {
                const auto *flies = byNumber(objectRows, "Id", 103);
                if (!flies) {
                    ++region.unsupportedObjects;
                    continue;
                }
                WorldObject object;
                object.id = ids.allocate();
                object.act = region.map.terrain.data.act;
                object.palette = level->second.palette;
                object.objectClass = 103;
                object.contentKey = "objgroup." + std::to_string(roomIndex) + "." +
                    std::to_string(slot) + ".flies";
                object.pos = {std::floor(lastCreated.x), std::floor(lastCreated.y)};
                object.accessPoint = region.map.grid.nearest(object.pos);
                object.appearance = {"objects", normalize(flies->at("Token")), "nu", "hth", {}};
                configureWorldObject(object, objectRows);
                region.objects.push_back(std::move(object));
            }
        }
    }
}
} // namespace d2x
