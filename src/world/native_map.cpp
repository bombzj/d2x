#include "native_map.hpp"
#include <algorithm>
#include <stdexcept>
#include <limits>

namespace d2x {
NativeMapGenerator::NativeMapGenerator(Archives &archives, const WorldCatalog &catalog,
    TileLibraryCache &cache, int act, uint32_t mapSeed, int difficulty)
    : archives_(archives), catalog_(catalog), cache_(cache), mapSeed_(mapSeed), difficulty_(difficulty),
      layout_(placeNativeAct(catalog, act, mapSeed)),
      identities_(archives), tiles_(catalog), activation_({
          [this](size_t room) { return near(room); },
          [this](size_t room) { prepare(room); },
          [this](size_t room) { create(room); },
          [this](size_t room) { tiles_.releaseRoom(room); }}) {
    if (act != 0) throw std::runtime_error("Native map session currently supports Act I");
    if (difficulty < 0 || difficulty > 2) throw std::invalid_argument("Invalid native map difficulty");
}
const MapData &NativeMapGenerator::pattern(const std::string &path) {
    const auto key = normalize(path);
    auto [entry, fresh] = patterns_.try_emplace(key);
    if (fresh) entry->second = decodeDs1(archives_.read(key), key);
    return entry->second;
}
void NativeMapGenerator::ensureLevel(int level) {
    if (levels_.contains(level)) return;
    const auto &record = catalog_.level(level);
    if (record.act != 0 || !allocating_.insert(level).second)
        throw std::runtime_error("Invalid or cyclic native level allocation");
    Level result;
    const std::vector<RetailRoom> *rooms = nullptr;
    if (record.generation == GenerationKind::Outdoor) {
        result.outdoor = buildRetailOutdoorLayout(archives_, catalog_, layout_, level);
        rooms = &result.outdoor->rooms;
    } else if (record.generation == GenerationKind::Preset) {
        result.preset = buildRetailPresetLevel(archives_, catalog_, layout_, level);
        layout_.levels.at(level) = result.preset->placement;
        rooms = &result.preset->rooms;
    } else if (record.generation == GenerationKind::Maze) {
        int direction = 0;
        if (level == 28) {
            ensureLevel(27);
            direction = levels_.at(27).preset->file;
        }
        result.maze = buildNativeMazeLevel(archives_, catalog_, layout_, level, mapSeed_, difficulty_, direction);
        if (level == 28) {
            auto &placement = layout_.levels.at(level);
            placement.x = result.maze->recipe.worldX; placement.y = result.maze->recipe.worldY;
            placement.width = result.maze->recipe.width; placement.height = result.maze->recipe.height;
        }
        rooms = &result.maze->rooms;
    } else throw std::runtime_error("Unsupported native level generation kind");
    const auto found = layout_.connections.find(level);
    const auto slots = found == layout_.connections.end()
        ? NativeActLayout::ConnectionSlots{record.visible, record.warps} : found->second;
    for (const auto &room : *rooms) {
        auto libraries = std::make_shared<RetailTileSelector>(cache_, catalog_, record.levelType, room.dt1Mask);
        result.rooms.push_back(tiles_.registerRoom(level, room, std::move(libraries), slots.warps));
    }
    levels_.emplace(level, std::move(result));
    allocating_.erase(level);
    const auto &allocated = levels_.at(level);
    if (allocated.preset && catalog_.presets().at(allocated.preset->preset).automap) {
        // The native client automap callback first allocates visible levels,
        // then initializes the complete prepend room list without sight refs.
        for (int destination : slots.visible) if (destination) ensureLevel(destination);
        for (auto room = allocated.rooms.rbegin(); room != allocated.rooms.rend(); ++room)
            activation_.initialize(*room);
    }
}
std::vector<size_t> NativeMapGenerator::near(size_t index) {
    const auto &room = tiles_.rooms().at(index);
    if (!room.near.empty()) return room.near;
    return tiles_.establishNear(index, layout_, [this](int level) { ensureLevel(level); });
}
void NativeMapGenerator::prepare(size_t index) {
    const auto &r = tiles_.rooms().at(index);
    if (!r.room.preset) return;
    const auto &preset = catalog_.presets().at(r.room.preset);
    const RetailPresetKey key{r.level, r.room.preset, r.room.file, r.room.mapX, r.room.mapY};
    const auto &level = levels_.at(r.level);
    std::span<const RetailPresetUnit> preloaded;
    if (preset.scan || preset.pops) {
        if (level.outdoor) preloaded = level.outdoor->presetUnits.at(key);
        else if (level.maze) preloaded = level.maze->presetUnits.at(key);
        else preloaded = level.preset->units;
    }
    tiles_.preparePresetUnits(index, preset.scan || preset.pops,
        [&](Seed &seed) {
            return identities_.buildUnits(pattern(preset.variants.at(size_t(r.room.file))),
                catalog_.level(r.level).act, r.room.mapX, r.room.mapY, seed);
        }, preloaded, [&] {
            return identities_.buildClientUnits(pattern(preset.variants.at(size_t(r.room.file))),
                r.level, r.room.preset, r.room.file, r.room.mapX, r.room.mapY,
                preset.width, preset.height);
        });
}
void NativeMapGenerator::create(size_t index) {
    auto nearby = near(index);
    prepare(index);
    const auto &r = tiles_.rooms().at(index);
    const auto &record = catalog_.level(r.level);
    if (r.room.preset) {
        const auto &preset = catalog_.presets().at(r.room.preset);
        const auto data = buildRetailPresetRoomData(preset, r.room,
            pattern(preset.variants.at(size_t(r.room.file))));
        tiles_.loadPreset(index, data, nearby);
    } else {
        const auto &outdoor = *levels_.at(r.level).outdoor;
        auto data = buildRetailOutdoorRoomData(cache_, catalog_, record, r.room, outdoor.paths,
            [this](const std::string &path) -> const MapData & { return pattern(path); }, identities_, r.libraries);
        tiles_.loadOutdoor(index, data, nearby);
    }
    std::erase_if(nearby, [&](size_t other) { return other != index && !tiles_.collision(other); });
    tiles_.activateCollision(index, nearby);
}
size_t NativeMapGenerator::roomAt(int level, int x, int y) {
    ensureLevel(level);
    // Native room lookup walks the level's prepend list, not creation order.
    const auto &rooms = levels_.at(level).rooms;
    for (auto at = rooms.rbegin(); at != rooms.rend(); ++at) {
        const auto &r = tiles_.rooms().at(*at).room;
        if (x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height) return *at;
    }
    throw std::runtime_error("Native room coordinates disagree with generated level");
}
std::vector<size_t> NativeMapGenerator::levelRooms(int level) {
    if (!valid()) throw std::runtime_error("Discard failed native map session");
    failed_ = true;
    ensureLevel(level);
    failed_ = false;
    return levels_.at(level).rooms;
}
size_t NativeMapGenerator::reveal(int level, int x, int y) {
    if (!valid()) throw std::runtime_error("Discard failed native map session");
    failed_ = true;
    const auto room = roomAt(level, x, y);
    const auto &extent = tiles_.rooms().at(room).room;
    if (extent.x != x || extent.y != y)
        throw std::runtime_error("Server room anchor disagrees with generated room origin");
    activation_.reveal(room);
    failed_ = false;
    return room;
}
void NativeMapGenerator::hide(int level, int x, int y) {
    if (!valid()) throw std::runtime_error("Discard failed native map session");
    failed_ = true;
    const auto room = roomAt(level, x, y);
    const auto &extent = tiles_.rooms().at(room).room;
    if (extent.x != x || extent.y != y)
        throw std::runtime_error("Server room removal anchor disagrees with generated room origin");
    activation_.hide(room);
    failed_ = false;
}
std::optional<size_t> NativeMapGenerator::clientRoom(int x, int y) const {
    std::optional<size_t> result;
    for (size_t index = 0; index < tiles_.rooms().size(); ++index) {
        const auto &room = tiles_.rooms()[index].room;
        if (x < room.x * 5 || y < room.y * 5 || x >= (room.x + room.width) * 5 ||
            y >= (room.y + room.height) * 5) continue;
        if (result) throw std::runtime_error("Ambiguous native player room coordinates");
        result = index;
    }
    return result;
}
void NativeMapGenerator::changeClientRoom(std::optional<size_t> previous, std::optional<size_t> next) {
    if (!valid()) throw std::runtime_error("Discard failed native map session");
    failed_ = true;
    activation_.changeClientRoom(previous, next);
    failed_ = false;
}
NativeMapSnapshot NativeMapGenerator::completeLevel(int level) {
    std::set<int> component{level};
    bool changed = true;
    while (changed) {
        changed = false;
        const auto current = component;
        for (const int id : current) {
            levelRooms(id);
            const auto &record = catalog_.level(id);
            const auto found = layout_.connections.find(id);
            const auto slots = found == layout_.connections.end()
                ? NativeActLayout::ConnectionSlots{record.visible, record.warps} : found->second;
            for (size_t slot = 0; slot < slots.visible.size(); ++slot)
                if (slots.visible[slot] && slots.warps[slot] == -1)
                    changed |= component.insert(slots.visible[slot]).second;
        }
    }
    // Materialize the continuous component in a stable order, so both sides
    // read the final shared seam choices and collision instead of a half seam.
    for (const int id : component) {
        const auto rooms = levelRooms(id);
        for (auto room = rooms.rbegin(); room != rooms.rend(); ++room) activation_.initialize(*room);
    }
    return snapshot(level, false);
}
MapRecipe NativeMapGenerator::recipe(int level) {
    const auto rooms = levelRooms(level);
    const auto &record = catalog_.level(level);
    MapRecipe result;
    result.native = MapRecipe::NativeRequest{level, mapSeed_, difficulty_};
    result.act = record.act; result.levelType = record.levelType;
    const auto &allocated = levels_.at(level);
    if (allocated.preset) { result.preset = allocated.preset->preset; result.variant = allocated.preset->file; }
    else if (allocated.maze) result.preset = allocated.maze->recipe.preset;
    else result.preset = 5; // Existing MPQ outdoor population family.
    for (const auto index : rooms) for (const auto &path : catalog_.terrainLibraries(record.levelType,
        tiles_.rooms().at(index).room.dt1Mask))
        if (std::find(result.tileLibraries.begin(), result.tileLibraries.end(), path) == result.tileLibraries.end())
            result.tileLibraries.push_back(path);
    result.ds1 = "native-act1/" + std::to_string(level) + "/" + std::to_string(mapSeed_);
    int right = 0, bottom = 0;
    result.worldX = result.worldY = std::numeric_limits<int>::max();
    for (const auto index : rooms) {
        const auto &room = tiles_.rooms().at(index).room;
        result.worldX = std::min(result.worldX, room.x); result.worldY = std::min(result.worldY, room.y);
        right = std::max(right, room.x + room.width); bottom = std::max(bottom, room.y + room.height);
    }
    result.width = right - result.worldX; result.height = bottom - result.worldY;
    for (const auto index : rooms) {
        const auto &room = tiles_.rooms().at(index).room;
        if (!room.preset) continue;
        const auto &preset = catalog_.presets().at(room.preset);
        auto source = catalog_.preset(room.preset, record.levelType, room.file);
        result.pieces.push_back({room.x - result.worldX, room.y - result.worldY, room.width,
            room.height, room.preset, room.file, source.ds1, source.tileLibraries,
            preset.fillBlanks, preset.populate, -1, preset.killEdge, source.animationSpeed,
            0, source.pops, source.popPad});
    }
    const auto foundSlots = layout_.connections.find(level);
    const auto slots = foundSlots == layout_.connections.end()
        ? NativeActLayout::ConnectionSlots{record.visible, record.warps} : foundSlots->second;
    for (size_t slot = 0; slot < slots.visible.size(); ++slot) {
        const int destination = slots.visible[slot];
        if (!destination || slots.warps[slot] != -1) continue;
        for (const auto index : rooms) {
            const auto nearby = near(index);
            const auto &a = tiles_.rooms().at(index).room;
            for (const auto adjacent : nearby) {
                const auto &other = tiles_.rooms().at(adjacent);
                if (other.level != destination) continue;
                const auto &b = other.room;
                int side = -1, start = 0, end = 0, plane = 0;
                if (a.x + a.width == b.x || b.x + b.width == a.x) {
                    side = a.x + a.width == b.x ? 3 : 1;
                    start = std::max(a.y, b.y) - result.worldY;
                    end = std::min(a.y + a.height, b.y + b.height) - result.worldY;
                    plane = (side == 3 ? a.x + a.width : a.x) - result.worldX;
                } else if (a.y + a.height == b.y || b.y + b.height == a.y) {
                    side = a.y + a.height == b.y ? 0 : 2;
                    start = std::max(a.x, b.x) - result.worldX;
                    end = std::min(a.x + a.width, b.x + b.width) - result.worldX;
                    plane = (side == 0 ? a.y + a.height : a.y) - result.worldY;
                }
                if (side >= 0 && start < end)
                    result.boundaries.push_back({destination, side, start, end, start, end, plane});
            }
        }
    }
    std::sort(result.boundaries.begin(), result.boundaries.end(), [](const auto &a, const auto &b) {
        return std::tie(a.destination, a.side, a.plane, a.start, a.end) <
            std::tie(b.destination, b.side, b.plane, b.start, b.end);
    });
    std::vector<MapRecipe::Boundary> merged;
    for (const auto &boundary : result.boundaries) {
        if (!merged.empty()) {
            auto &last = merged.back();
            if (last.destination == boundary.destination && last.side == boundary.side &&
                last.plane == boundary.plane && boundary.start <= last.end) {
                last.end = last.contactEnd = std::max(last.end, boundary.end);
                continue;
            }
        }
        merged.push_back(boundary);
    }
    result.boundaries = std::move(merged);
    return result;
}
NativeMapSnapshot NativeMapGenerator::snapshot(int currentLevel, bool continuous) const {
    if (!valid()) throw std::runtime_error("Cannot present a failed native map session");
    // Warp destinations may be active but thousands of tiles away. Include
    // only the current level's continuous walking component in one scene.
    std::set<int> component{currentLevel};
    bool changed = continuous;
    while (changed) {
        changed = false;
        for (const auto &room : tiles_.rooms()) {
            if (!room.loaded || !component.contains(room.level)) continue;
            const auto found = layout_.connections.find(room.level);
            const auto &record = catalog_.level(room.level);
            const auto slots = found == layout_.connections.end()
                ? NativeActLayout::ConnectionSlots{record.visible, record.warps} : found->second;
            for (const auto adjacent : room.near) {
                const auto &other = tiles_.rooms().at(adjacent);
                if (!other.loaded) continue;
                for (size_t slot = 0; slot < 8; ++slot)
                    if (slots.visible[slot] == other.level && slots.warps[slot] == -1)
                        changed |= component.insert(other.level).second;
            }
        }
    }
    int x = std::numeric_limits<int>::max(), y = x, right = 0, bottom = 0;
    std::vector<size_t> rooms;
    for (size_t index = 0; index < tiles_.rooms().size(); ++index) {
        const auto &room = tiles_.rooms()[index];
        if (!room.loaded || !component.contains(room.level) || !tiles_.collision(index)) continue;
        rooms.push_back(index);
        x = std::min(x, room.room.x); y = std::min(y, room.room.y);
        right = std::max(right, room.room.x + room.room.width);
        bottom = std::max(bottom, room.room.y + room.room.height);
    }
    if (rooms.empty()) throw std::runtime_error("Native scene has no active collision rooms");
    const int64_t width = int64_t(right) - x, height = int64_t(bottom) - y;
    if (x < 0 || y < 0 || right > 13107 || bottom > 13107 || width <= 0 || height <= 0 ||
        width * height > 4 * 1024 * 1024)
        throw std::runtime_error("Native scene extent exceeds coordinate or allocation limits");
    NativeMapSnapshot result;
    result.tileX = x; result.tileY = y;
    result.levels.assign(component.begin(), component.end());
    result.map.grid = Grid((right - x) * 5, (bottom - y) * 5);
    result.collision.assign(result.map.grid.blocked.size(), 0xffff);
    auto &terrain = result.map.terrain;
    terrain.preparedRooms = true;
    terrain.path = terrain.name = "native-act1/" + std::to_string(mapSeed_);
    terrain.data.act = 0;
    terrain.data.width = right - x + 1; terrain.data.height = bottom - y + 1;
    const size_t cells = size_t(terrain.data.width) * size_t(terrain.data.height);
    terrain.data.shadows.resize(cells);
    std::map<const Tile *, int> indices;
    std::set<const std::vector<Tile> *> retained;
    std::set<std::tuple<int, int, int, int, int, int, int>> popups;
    std::map<size_t, size_t> roomIndices;
    for (const auto index : rooms) {
        roomIndices.emplace(index, terrain.rooms.size());
        const auto &room = tiles_.rooms().at(index);
        const auto &r = room.room;
        terrain.rooms.push_back({room.level, r.x - x, r.y - y, r.width, r.height,
            r.preset, r.file, r.mapX - x, r.mapY - y, {}});
    }
    for (const auto index : rooms) {
        auto &output = terrain.rooms.at(roomIndices.at(index));
        for (const auto adjacent : tiles_.rooms().at(index).activeNear)
            if (const auto found = roomIndices.find(adjacent); found != roomIndices.end())
                output.near.push_back(found->second);
    }
    std::map<size_t, size_t> instanceIndices;
    const auto tileIndex = [&](const Tile *tile) {
        auto [entry, fresh] = indices.try_emplace(tile, int(terrain.tiles.size()));
        if (fresh) terrain.tiles.push_back(tile);
        return entry->second;
    };
    for (const auto index : rooms) {
        const auto &room = tiles_.rooms().at(index);
        for (const auto &unit : room.units) {
            if (unit.type == 2) {
                MapObject object;
                object.type = 2; object.id = unit.id; object.nativeIdentity = true;
                object.x = unit.x - x * 5; object.y = unit.y - y * 5;
                terrain.data.objects.push_back(std::move(object));
            } else if (unit.type == 5) {
                const auto link = std::find_if(room.warps.begin(), room.warps.end(),
                    [&](const auto &warp) { return warp.id == unit.id && warp.destination; });
                if (link == room.warps.end()) continue;
                const auto slot = std::find(room.warpSlots.begin(), room.warpSlots.end(), unit.id);
                if (slot == room.warpSlots.end()) throw std::runtime_error("Native exit lacks its Vis slot");
                terrain.exits.push_back({int(slot - room.warpSlots.begin()),
                    tiles_.rooms().at(*link->destination).level, *link->definition,
                    {float(unit.x - x * 5), float(unit.y - y * 5)}});
                auto chain = [&](std::optional<size_t> head, std::vector<size_t> &output) {
                    std::set<size_t> seen;
                    for (auto tile = head; tile; tile = tiles_.tiles().at(*tile).next) {
                        if (!seen.insert(*tile).second) throw std::runtime_error("Native warp tile chain cycles");
                        output.push_back(*tile);
                    }
                };
                chain(link->visible, terrain.exits.back().visible);
                chain(link->lit, terrain.exits.back().lit);
            }
        }
        for (const auto &unit : room.authoredUnits) {
            if (!unit.clientOnly) continue;
            auto object = unit.unit;
            object.x -= x * 5; object.y -= y * 5;
            terrain.clientObjects.push_back(std::move(object));
        }
        const auto merge = [&](const std::vector<MapCell> &source, std::vector<MapCell> &output) {
            for (int row = 0; row < room.grids.height; ++row)
                for (int column = 0; column < room.grids.width; ++column) {
                    const int tx = room.room.x - x + column, ty = room.room.y - y + row;
                    if (tx < 0 || ty < 0 || tx >= terrain.data.width || ty >= terrain.data.height) continue;
                    const auto &cell = source.at(size_t(row) * size_t(room.grids.width) + size_t(column));
                    if (cell.occupied()) output.at(size_t(ty) * size_t(terrain.data.width) + size_t(tx)) = cell;
                }
        };
        while (terrain.data.floors.size() < room.grids.floors.size()) terrain.data.floors.emplace_back(cells);
        while (terrain.data.walls.size() < room.grids.walls.size()) terrain.data.walls.emplace_back(cells);
        for (size_t i = 0; i < room.grids.floors.size(); ++i) merge(room.grids.floors[i], terrain.data.floors[i]);
        for (size_t i = 0; i < room.grids.walls.size(); ++i) merge(room.grids.walls[i], terrain.data.walls[i]);
        if (!room.grids.shadows.empty()) merge(room.grids.shadows, terrain.data.shadows);
        for (const auto &unit : room.authoredUnits) {
            if (unit.clientOnly) continue;
            auto object = unit.unit;
            object.x -= x * 5; object.y -= y * 5;
            for (auto &node : object.path) { node.x -= x * 5; node.y -= y * 5; }
            terrain.data.objects.push_back(std::move(object));
        }
        for (const auto &library : room.libraries->libraries())
            if (retained.insert(library.get()).second) terrain.libraries.push_back(library);
        for (auto popup : room.roofPopups) {
            popup.x -= x; popup.y -= y;
            popup.parentX -= x; popup.parentY -= y;
            if (popups.emplace(popup.x, popup.y, popup.width, popup.height, popup.roofMain, popup.pad,
                popup.group).second)
                terrain.data.roofPopups.push_back(popup);
        }
        std::map<size_t, const RetailTileRoom::Animation *> animations;
        std::set<size_t> extraFrames;
        for (const auto &animation : room.animations) {
            animations.emplace(animation.frames.front(), &animation);
            extraFrames.insert(animation.frames.begin() + 1, animation.frames.end());
        }
        const auto append = [&](const std::vector<size_t> &array, bool wallArray) {
            for (const auto chosen : array) {
                if (extraFrames.contains(chosen)) continue;
                const auto &tile = tiles_.tiles().at(chosen);
                MapTerrain::Instance instance{tile.x - x, tile.y - y, tile.type,
                    tileIndex(tile.tile), tile.flags};
                instance.room = roomIndices.at(index);
                instance.wallArray = wallArray;
                terrain.tileChoices.try_emplace(std::tuple{instance.x, instance.y, size_t{0}, tile.tile->key()},
                    instance.tile);
                if (const auto animation = animations.find(chosen); animation != animations.end()) {
                    instance.speed = animation->second->speed;
                    for (const auto frame : animation->second->frames)
                        instance.frames.push_back(tileIndex(tiles_.tiles().at(frame).tile));
                }
                instanceIndices.emplace(chosen, terrain.instances.size());
                terrain.instances.push_back(std::move(instance));
            }
        };
        append(room.floors, false); append(room.walls, true); append(room.shadows, false);
        const auto &collision = *tiles_.collision(index);
        for (int row = 0; row < collision.height; ++row)
            for (int column = 0; column < collision.width; ++column) {
                const auto destination = size_t(collision.y - y * 5 + row) * result.map.grid.width +
                    size_t(collision.x - x * 5 + column);
                result.collision.at(destination) = collision.flags[size_t(row) * collision.width + column];
            }
    }
    for (auto &exit : terrain.exits) {
        auto project = [&](std::vector<size_t> &chain) {
            std::vector<size_t> instances;
            for (const auto tile : chain)
                if (const auto found = instanceIndices.find(tile); found != instanceIndices.end())
                    instances.push_back(found->second);
            chain = std::move(instances);
        };
        project(exit.visible); project(exit.lit);
    }
    for (size_t index = 0; index < result.collision.size(); ++index) {
        const auto flags = result.collision[index];
        result.map.grid.terrainCollision[index] = uint8_t(flags);
        result.map.grid.blocked[index] = (flags & 0x09) != 0;
        result.map.grid.lightBlocked[index] = (flags & 0x22) != 0;
    }
    return result;
}
} // namespace d2x
