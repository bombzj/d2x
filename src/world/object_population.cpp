#include "object_population.hpp"
#include "generation_seed.hpp"
#include <algorithm>
#include <cmath>
#include <string>

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
        for (int x = left; x < left + width; ++x)
            if (x < 0 || y < 0 || x >= grid.width || y >= grid.height ||
                (grid.terrainCollision[size_t(y) * grid.width + x] & placementMask)) return false;
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
                              const Table &objectRows, const Table &groupRows, uint32_t worldSeed) {
    const auto levelId = int(region.definition.id);
    const auto level = catalog.levels().find(levelId);
    if (level == catalog.levels().end() || level->second.act != 0 || region.definition.safe)
        return;
    for (size_t roomIndex = 0; roomIndex < region.map.rooms.size(); ++roomIndex) {
        const auto &room = region.map.rooms[roomIndex];
        if (!room.populate || room.width < 8 || room.height < 8) continue;
        Seed selection(worldSeed ^ (uint32_t(levelId) * 0x9e3779b9u) ^ uint32_t(roomIndex));
        for (size_t slot = 0; slot < level->second.objectGroups.size(); ++slot) {
            const int groupId = level->second.objectGroups[slot];
            if (!groupId || selection.below(100) > level->second.objectProbabilities[slot]) continue;
            const auto *group = byNumber(groupRows, "Offset", groupId);
            if (!group) continue;
            const int choice = selection.below(100);
            int total = 0, objectId = 0, density = 0;
            for (int candidate = 0; candidate < 8; ++candidate) {
                const auto suffix = std::to_string(candidate);
                const int id = integer(*group, "ID" + suffix);
                if (!id) break;
                total += integer(*group, "PROB" + suffix);
                if (choice < total) {
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
            const bool chest = operation == 4 && populate == 3;
            if (chest && std::any_of(region.objects.begin(), region.objects.end(),
                [&](const WorldObject &object) {
                    return object.operateFn == 23 && object.pos.x >= room.x && object.pos.y >= room.y &&
                        object.pos.x < room.x + room.width && object.pos.y < room.y + room.height;
                })) continue; // Native object population skips waypoint rooms.
            if (!((operation == 4 && populate == 3) || casket || barrel || shrine || well ||
                  (operation == 14 && (populate == 3 || populate == 6 || populate == 7))))
                continue;
            if (shrine) {
                const int existing = int(std::count_if(region.objects.begin(), region.objects.end(),
                    [](const WorldObject &object) { return object.interaction == Interaction::Shrine; }));
                if (existing >= 10 || existing > int(region.map.rooms.size()) / 8) continue;
            }
            if (well) {
                const int existing = int(std::count_if(region.objects.begin(), region.objects.end(),
                    [](const WorldObject &object) { return object.operateFn == 22; }));
                if (existing >= 4 || existing > int(region.map.rooms.size()) / 8) continue;
            }
            int count = std::clamp(density * ((room.width * room.height) >> 7) >> 8, 0, 32);
            if (chest) count = std::max(0, density * ((room.width * room.height) >> 7) >> 8);
            if (barrel) count = std::min(8, count * 2);
            Seed placement(worldSeed ^ (uint32_t(levelId) * 0x85ebca6bu) ^
                           (uint32_t(roomIndex) * 0xc2b2ae35u) ^ uint32_t(slot));
            const int size = std::max(integer(*record, "SizeX"), integer(*record, "SizeY"));
            const int sizeX = integer(*record, "SizeX"), sizeY = integer(*record, "SizeY");
            if (chest && (room.width <= sizeX + 1 || room.height <= sizeY + 1)) continue;
            const int extent = well ? size + 4 : size / 2 + 3;
            const auto explodingBarrel = barrel ? std::find_if(objectRows.begin(), objectRows.end(),
                [](const auto &row) {
                    return normalize(row.at("Name")) == "barrel" && integer(row, "OperateFn") == 7;
                }) : objectRows.end();
            auto add = [&](Vec point, int index, bool clustered = false) {
                if (chest ? !clearChest(region, point, sizeX + 6, sizeY + 6)
                          : !clear(region, point, extent, !clustered)) return false;
                const auto *appearance = barrel && explodingBarrel != objectRows.end() &&
                    placement.below(3) == 0 ? &*explodingBarrel : record;
                WorldObject object;
                object.id = ids.allocate();
                object.contentKey = "objgroup." + std::to_string(roomIndex) + "." +
                                    std::to_string(slot) + "." + std::to_string(index);
                object.pos = point;
                object.accessPoint = region.map.grid.nearest(point);
                object.appearance = {"objects", normalize(appearance->at("Token")), "nu", "hth", {}};
                object.objectClass = integer(*appearance, "Id");
                configureWorldObject(object, objectRows);
                object.facing = (int(point.x) + int(point.y)) % 8;
                region.objects.push_back(std::move(object));
                return true;
            };
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
                    const int x = room.x + 1 + placement.below(room.width - 2);
                    const int y = room.y + 1 + placement.below(room.height - 2);
                    const Vec point{float(x) + .5f, float(y) + .5f};
                    bool nearShrine = std::any_of(region.objects.begin(), region.objects.end(),
                        [&](const WorldObject &object) {
                            return object.interaction == Interaction::Shrine &&
                                (std::abs(object.pos.x - point.x) < 50 ||
                                 std::abs(object.pos.y - point.y) < 50);
                        });
                    if (!nearShrine && add(point, 0)) break;
                }
                continue;
            }
            if (well) {
                for (int attempt = 0; attempt < 5; ++attempt) {
                    const int x = room.x + 1 + placement.below(room.width - 2);
                    const int y = room.y + 1 + placement.below(room.height - 2);
                    const Vec point{float(x) + .5f, float(y) + .5f};
                    bool nearWell = std::any_of(region.objects.begin(), region.objects.end(),
                        [&](const WorldObject &object) {
                            return object.operateFn == 22 &&
                                (std::abs(object.pos.x - point.x) < 100 ||
                                 std::abs(object.pos.y - point.y) < 100);
                        });
                    if (!nearWell && add(point, 0)) break;
                }
                continue;
            }
            for (int index = 0; index < count; ++index) {
                for (int attempt = 0; attempt < 5; ++attempt) {
                    const int x = chest ? room.x + placement.below(room.width - sizeX - 1)
                                        : room.x + 1 + placement.below(room.width - 2);
                    const int y = chest ? room.y + placement.below(room.height - sizeY - 1)
                                        : room.y + 1 + placement.below(room.height - 2);
                    if (chest && (x < room.x + 1 || y < room.y + 1 ||
                                  x >= room.x + room.width - 1 || y >= room.y + room.height - 1)) continue;
                    const Vec point{float(x) + .5f, float(y) + .5f};
                    if (add(point, index)) break;
                }
            }
        }
    }
}
} // namespace d2x
