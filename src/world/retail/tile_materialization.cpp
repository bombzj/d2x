#include "tile_materialization.hpp"
#include <algorithm>
#include <iterator>
#include <stdexcept>

// Native RoomTile / OutRoom / Preset / Collision dispatch from D2MOO.
// MIT attribution: docs/licenses/D2MOO.txt. Corner creation/mutation additionally
// checked against 1.13c D2Common (MD5 ee1238806ef6d6d9801d12a09d128fe1),
// RVA 0x68f00 / 0x695d0. Runtime uses C++ and current MPQ, never that DLL.

namespace d2x {
namespace {
bool exitTile(int t) { return t == 10 || t == 11; }
bool doorTile(int t) { return t == 8 || t == 9; }
int style(uint32_t p) { return int((p >> 20) & 63); }
int sequence(uint32_t p) { return int((p >> 8) & 255); }
bool inside(const RetailRoom &r, int x, int y, bool border = false) {
    return x >= r.x && y >= r.y && x < r.x + r.width + int(border) &&
        y < r.y + r.height + int(border);
}
const WarpRecord *lookupWarp(const WorldCatalog &catalog, int id, char direction) {
    const auto it = catalog.warps().find(id);
    if (it == catalog.warps().end()) return nullptr;
    for (const auto &record : it->second)
        if (!record.direction.empty() && (direction == 'b' || record.direction[0] == 'b' ||
            record.direction[0] == direction)) return &record;
    throw std::runtime_error("Native warp direction is absent from current MPQ");
}
void validateNear(size_t current, std::span<const size_t> near, const std::deque<RetailTileRoom> &rooms) {
    if (std::find(near.begin(), near.end(), current) == near.end())
        throw std::runtime_error("Native near-room list must include its current room");
    for (auto index : near) {
        // Native warp insertion can append a room already present in the list.
        if (index >= rooms.size()) throw std::runtime_error("Invalid native near-room entry");
    }
}
}

size_t RetailTileMaterializer::registerRoom(int level, const RetailRoom &room,
    std::shared_ptr<RetailTileSelector> libraries, const std::array<int, 8> &warpSlots,
    std::span<const int> linkedWarpIds) {
    if (failed_) throw std::runtime_error("Discard failed native tile session before registering rooms");
    if (catalog_.level(level).act != 0)
        throw std::runtime_error("Native tile materialization currently supports Act I only");
    if (!libraries || room.width <= 0 || room.height <= 0)
        throw std::runtime_error("Invalid native room tile inputs");
    RetailTileRoom state{level, room, std::move(libraries), warpSlots};
    std::set<int> uniqueWarps;
    for (int id : linkedWarpIds) {
        if (!uniqueWarps.insert(id).second)
            throw std::runtime_error("Duplicate native room warp link");
        const auto *definition = lookupWarp(catalog_, id, 'b');
        if (!definition) throw std::runtime_error("Native linked warp ID is absent from current MPQ");
        state.warps.push_back({id, definition, {}, {}});
    }
    rooms_.push_back(std::move(state));
    return rooms_.size() - 1;
}

std::vector<size_t> RetailTileMaterializer::establishNear(size_t current, const NativeActLayout &act,
    const std::function<void(int)> &ensureLevel) {
    if (failed_) throw std::runtime_error("Discard failed native tile session before linking rooms");
    auto &source = rooms_.at(current);
    if (source.loaded || !source.warps.empty() || !source.near.empty())
        throw std::runtime_error("Native room links must be established once before tile loading");
    if (!ensureLevel) throw std::runtime_error("Native destination level allocator is required");
    failed_ = true;
    // The allocator may register additional rooms, so temporarily permit that
    // operation; any exception below poisons the partially linked session.
    try {
        auto slots = [&](int level) {
            if (const auto found = act.connections.find(level); found != act.connections.end())
                return found->second;
            const auto &record = catalog_.level(level);
            return NativeActLayout::ConnectionSlots{record.visible, record.warps};
        };
        auto levelRooms = [&](int level) {
            std::vector<size_t> result;
            for (size_t i = rooms_.size(); i > 0; --i)
                if (rooms_[i - 1].level == level) result.push_back(i - 1);
            if (result.empty()) throw std::runtime_error("Native linked level has no registered rooms");
            return result;
        };
        auto close = [&](size_t other) {
            const auto &a = source.room, &b = rooms_.at(other).room;
            const int dx = a.x >= b.x ? a.x - b.width - b.x : b.x - a.width - a.x;
            const int dy = a.y >= b.y ? a.y - b.height - b.y : b.y - a.height - a.y;
            return dx < 6 && dy < 6;
        };
        auto near = levelRooms(source.level);
        std::erase_if(near, [&](size_t other) { return !close(other); });
        sortRetailNearRooms(near, rooms_);
        const auto from = slots(source.level);
        if (source.warpSlots != from.warps)
            throw std::runtime_error("Native room warp slots disagree with act placement");
        bool paired = false; // Preserves the native function's cumulative flag.
        for (size_t slot = 0; slot < 8; ++slot) {
            if (!(source.room.flags & (1u << (slot + 4)))) continue;
            const int destination = from.visible[slot];
            if (!destination) throw std::runtime_error("Native flagged room has no Vis destination");
            failed_ = false;
            ensureLevel(destination);
            failed_ = true;
            const auto to = slots(destination);
            const auto candidates = levelRooms(destination);
            auto attach = [&](size_t back) {
                bool found = false;
                for (const auto other : candidates) {
                    if (!(rooms_.at(other).room.flags & (1u << (back + 4)))) continue;
                    if (from.warps[slot] == -1 && !close(other)) continue;
                    near.push_back(other);
                    sortRetailNearRooms(near, rooms_);
                    found = true;
                    if (from.warps[slot] != -1) {
                        const auto *definition = lookupWarp(catalog_, from.warps[slot], 'b');
                        if (!definition) throw std::runtime_error("Native linked warp is absent from MPQ");
                        source.warps.insert(source.warps.begin(),
                            {from.warps[slot], definition, {}, {}, other});
                        return true;
                    }
                }
                return found;
            };
            if (from.warps[slot] != -1) {
                int occurrence = 0, backOccurrence = 0;
                for (size_t previous = 0; previous < slot; ++previous)
                    if (from.visible[previous] == destination) ++occurrence;
                for (size_t back = 0; back < 8; ++back) {
                    if (to.visible[back] != source.level) continue;
                    if (occurrence == backOccurrence++) { if (attach(back)) paired = true; break; }
                }
            }
            if (!paired) for (size_t back = 0; back < 8; ++back)
                if (to.visible[back] == source.level && attach(back)) break;
        }
        if (!catalog_.level(source.level).town)
            for (const auto other : near)
                if (catalog_.level(rooms_.at(other).level).town) { source.room.flags |= 0x800000; break; }
        source.near = near;
        failed_ = false;
        return near;
    } catch (...) { failed_ = true; throw; }
}

void RetailTileMaterializer::door(size_t current, std::optional<size_t> tile, int type,
    uint32_t packed, int x, int y) {
    auto &state = rooms_.at(current);
    if (tile && (tiles_.at(*tile).flags & 0x20)) return;
    // Native Act-I door identity/offset dispatch, not object stats or spawn rates.
    // Actual object definitions continue to come from the MPQ/server.
    struct Entry { int style, sequence; bool right; int id, x, y; };
    static constexpr Entry entries[]{
        {7,0,true,14,5,0}, {7,0,false,13,0,5},
        {5,0,true,16,0,0}, {5,0,false,15,0,0}, {6,0,true,27,5,-2},
        {4,0,true,24,1,2}, {4,0,false,23,0,0}, {4,3,true,25,1,0},
        {1,2,false,62,0,3}, {1,2,true,63,3,0},
        {0,0,true,16,0,0}, {0,0,false,64,0,0}, {2,0,true,47,5,0}
    };
    int first = 0, last = -1;
    if (state.level >= 28 && state.level <= 31) last = 3;
    else if (state.level == 26 || state.level == 27) { first = 4; last = 6; }
    else if (state.level == 32 || state.level == 33) { first = 5; last = 9; }
    else if (state.level >= 34 && state.level <= 37) { first = 10; last = state.level == 37 ? 12 : 11; }
    const bool right = (tile ? tiles_.at(*tile).type : type) == 9;
    for (int i = first; i <= last; ++i) {
        const auto &entry = entries[i];
        if (entry.style != style(packed) || entry.sequence != sequence(packed) || entry.right != right) continue;
        const int localX = (x - state.room.x) * 5 + entry.x;
        const int localY = (y - state.room.y) * 5 + entry.y;
        if (localX >= 0 && localY >= 0 && localX < state.room.width * 5 && localY < state.room.height * 5) {
            state.units.insert(state.units.begin(), {2, entry.id,
                state.room.x * 5 + localX, state.room.y * 5 + localY});
            if (tile) tiles_.at(*tile).flags |= 0x20;
        }
        return;
    }
}

void RetailTileMaterializer::initializeFlags(size_t current, size_t index, uint32_t p) {
    auto &t = tiles_.at(index);
    if (doorTile(t.type)) door(current, index, t.type, p, t.x, t.y);
    if (t.type != 13) t.flags |= (((p >> 18) & 3) + 1) << 14;
    // 1.13c InitializeTileDataFlags RVA 0x68acf uses tree type 14.
    if (t.type == 14) t.flags |= 4;
    else if (doorTile(t.type) || exitTile(t.type)) t.flags |= 2;
    if (p & 0x80) t.flags |= 1;
    if (p & 0x10000000) t.flags |= 0x102;
    if (p & 0x20000) t.flags |= 0x40;
    if (p & 0x10000) t.flags |= 0x80;
    if (p & 8) t.flags |= 4;
    t.flags = (t.flags & ~8u) | ((p & 0x80000000) ? 8u : 0u);
    if (p & 0x4000000) t.flags |= 0x20c;
    if (p & 0x20000000) t.flags |= 0x800;
    if (p & 4) t.flags |= 0x2000;
    if (t.tile->materialFlags & 1) t.flags |= 4;
    if (t.tile->materialFlags & 4) t.flags |= 0x800;
    t.packed = p;
}

void RetailTileMaterializer::changeCollision(size_t owner, size_t index, const Tile *after) {
    const auto &tile = tiles_.at(index);
    changes_.push_back({index, tile.tile, after});
    if (!collisions_.contains(owner)) return;
    // The native routine starts from the owning active room and resolves the
    // tile origin into its active adjacent rooms before AND-removing old DT1
    // flags and OR-adding the replacement. DS1 flags are not recomputed here.
    auto apply = [&](size_t candidate) {
        auto found = collisions_.find(candidate);
        if (found == collisions_.end()) return false;
        auto &grid = found->second;
        const int x = tile.x * 5 - grid.x, y = tile.y * 5 - grid.y;
        if (x < 0 || y < 0 || x >= grid.width || y >= grid.height) return false;
        for (int dy = 0; dy < 5 && y + dy < grid.height; ++dy)
            for (int dx = 0; dx < 5 && x + dx < grid.width; ++dx) {
                auto &flags = grid.flags[size_t(y + dy) * size_t(grid.width) + size_t(x + dx)];
                const auto offset = size_t(4 - dy) * 5 + size_t(dx);
                flags &= uint16_t(~uint16_t(tile.tile->flags[offset]));
                if (after) flags |= after->flags[offset];
            }
        return true;
    };
    // 1.13c RVA 0x1330 returns the source active room first when it
    // contains the origin; only then does it walk its active near list.
    if (apply(owner)) return;
    for (auto candidate : rooms_.at(owner).activeNear) if (apply(candidate)) return;
}

size_t RetailTileMaterializer::create(size_t current, int type, uint32_t p, int x, int y,
    std::optional<size_t> *head, const Tile *selected, bool wallArray) {
    auto &r = rooms_.at(current);
    if (!selected) selected = &r.libraries->pick(type, p, r.room.seed.random);
    const auto index = tiles_.size();
    tiles_.push_back({current, x, y, type, p, 0, selected, head ? *head : std::nullopt});
    if (head) *head = index;
    (wallArray ? r.walls : type == 0 ? r.floors : type == 13 ? r.shadows : r.walls).push_back(index);
    initializeFlags(current, index, p);
    if (type == 3) create(current, 4, p, x, y, head, nullptr, true);
    return index;
}

std::optional<size_t> RetailTileMaterializer::find(size_t current, std::span<const size_t> near,
    int type, uint32_t p, int x, int y) const {
    for (auto owner : near) {
        const auto &r = rooms_.at(owner);
        if (owner == current || !r.loaded || !inside(r.room, x, y, true)) continue;
        auto index = r.mapLinks[type == 0];
        size_t traversed = 0;
        while (index) {
            if (++traversed > tiles_.size()) throw std::runtime_error("Cyclic native map/warp tile chain");
            const auto &t = tiles_.at(*index);
            if (t.x == x && t.y == y && t.type != 4 && (t.type == 13 || !(p & 0x8000000)) &&
                (!(t.flags & 0x1c000) || int((t.flags & 0x1c000) >> 14) - 1 == int((p >> 18) & 3)))
                return index;
            index = t.next;
        }
    }
    return {};
}

void RetailTileMaterializer::linked(size_t current, int type, uint32_t p, int x, int y) {
    auto &r = rooms_.at(current);
    if (exitTile(type) && !inside(r.room, x, y)) return;
    const auto index = create(current, type, p, x, y, &r.mapLinks[type == 0]);
    if (exitTile(type)) wallWarp(current, index, p, type);
}

void RetailTileMaterializer::update(size_t current, size_t index, int type, uint32_t p, int x, int y) {
    auto &t = tiles_.at(index);
    auto &owner = rooms_.at(t.owner);
    const auto &r = rooms_.at(current);
    static constexpr int indices[]{-1,0,1,2,-1,3,4,5,-2,-2,-1,-1,-1,-2,-1,-1,-1,-1,-1};
    static constexpr int remap[6][7]{
        {1,3,3,4,1,3,1}, {1,2,3,4,3,2,2}, {3,3,3,4,3,3,3},
        {1,3,3,4,5,6,1}, {3,2,3,4,3,6,2}, {1,2,3,4,1,2,7}
    };
    if (type < 0 || type >= int(std::size(indices))) throw std::runtime_error("Unknown native tile type");
    if (t.flags & 1) {
        if (doorTile(t.type)) initializeFlags(current, index, p);
        return;
    }
    if (!(p & 0x80)) {
        const bool nonDoor = !doorTile(type);
        if (nonDoor && doorTile(t.type) && (x == owner.room.x || y == owner.room.y)) return;
        if (nonDoor || (x != r.room.x && y != r.room.y)) {
            const int row = indices[type];
            if (row < 0 || t.type > 7) { if (row != -1) return; }
            else {
                if (t.type < 1) throw std::runtime_error("Invalid native wall remap to floor");
                type = remap[row][t.type - 1];
            }
        }
    }
    if (t.type == 3 && type != 3) {
        // 1.13c RVA 0x69722 follows +0x20, the actual shared chain link,
        // rather than looking up a type-4 tile by position or array index.
        if (!t.next) throw std::runtime_error("Native corner replacement has no chain successor");
        auto &successor = tiles_.at(*t.next);
        successor.flags |= 8;
        changeCollision(t.owner, *t.next, nullptr);
    } else if (t.type != 3 && type == 3) {
        t.flags |= 0xc008;
        changeCollision(t.owner, index, nullptr);
        linked(current, 3, p, x, y);
    }
    if (type != t.type || (t.type == 0 && t.tile->main == 30 && t.tile->sub == 0)) {
        const auto *selected = &owner.libraries->pick(type, p, owner.room.seed.random);
        if (selected != t.tile) changeCollision(t.owner, index, selected);
        t.tile = selected;
        t.type = type;
    }
    initializeFlags(current, index, p);
}

const WarpRecord *RetailTileMaterializer::warpDefinition(size_t current, uint32_t p, int type) const {
    const auto &r = rooms_.at(current);
    const int slot = style(p);
    if (slot >= 8) return nullptr;
    return lookupWarp(catalog_, r.warpSlots[slot], type == 11 ? 'r' : 'l');
}

RetailTileRoom::Warp *RetailTileMaterializer::warpLink(size_t current, uint32_t p, int type) {
    auto &r = rooms_.at(current);
    const int slot = style(p);
    if (slot >= 8) return nullptr;
    const int id = r.warpSlots[slot];
    for (auto &link : r.warps) if (link.id == id) {
        return &link;
    }
    return nullptr;
}

bool RetailTileMaterializer::addWarpUnit(size_t current, uint32_t p, int type, int x, int y) {
    auto &r = rooms_.at(current);
    const auto *definition = warpDefinition(current, p, type);
    if (!definition || x == r.room.x + r.room.width || y == r.room.y + r.room.height) return false;
    r.units.insert(r.units.begin(), {5, definition->id, x * 5 + definition->offsetX, y * 5 + definition->offsetY});
    return true;
}

void RetailTileMaterializer::wallWarp(size_t current, size_t index, uint32_t p, int type) {
    auto *link = warpLink(current, p, type);
    if (!link) return; // Native missing-room-link path makes no additional draws.
    auto &t = tiles_.at(index);
    if ((sequence(p) != 0 && sequence(p) != 4) || addWarpUnit(current, p, type, t.x, t.y)) {
        t.next = link->visible;
        link->visible = index;
        if (link->definition->direction != "b")
            link->definition = lookupWarp(catalog_, link->id, type == 11 ? 'r' : 'l');
        if (link->definition->litVersion) {
            if (link->definition->tiles < 0 || link->definition->tiles > 255)
                throw std::runtime_error("Invalid MPQ warp tile sequence mask");
            const uint32_t lit = p | (uint32_t(link->definition->tiles) << 8);
            const auto added = create(current, type, lit, t.x, t.y, &link->lit);
            tiles_.at(added).flags |= 8;
        }
    }
}

void RetailTileMaterializer::floorWarp(size_t current, uint32_t p, int type, int x, int y) {
    auto &r = rooms_.at(current);
    r.room.flags |= 0x800000;
    auto *link = warpLink(current, p, type);
    if (!link) return;
    if (link->definition->direction != "b")
        link->definition = lookupWarp(catalog_, link->id, type == 11 ? 'r' : 'l');
    if (!link->definition->litVersion) return;
    for (int i = 0; i < 4; ++i) {
        const uint32_t lit = (uint32_t(sequence(p) & 63) << 20) | (uint32_t(i | 4) << 8);
        const auto index = create(current, 0, lit, x - 1 + (i & 1), y - 1 + (i >> 1), &link->lit);
        tiles_.at(index).flags |= 8;
    }
    for (auto index : r.floors) {
        auto &t = tiles_.at(index);
        if (t.tile->main == sequence(p) && unsigned(t.tile->sub) < 4) {
            t.next = link->visible;
            link->visible = index;
        }
    }
}

void RetailTileMaterializer::layer(size_t current, std::span<const size_t> near,
    std::span<const MapCell> cells, int stride, bool orientations, bool fillBlanks, bool killX, bool killY) {
    const auto &room = rooms_.at(current).room;
    const int width = room.width + !killX, height = room.height + !killY;
    if (stride < width || cells.size() < size_t(stride) * size_t(height))
        throw std::runtime_error("Incomplete native room overlap layer");
    for (int cy = 0; cy < height; ++cy) for (int cx = 0; cx < width; ++cx) {
        const auto &cell = cells[size_t(cy) * size_t(stride) + size_t(cx)];
        uint32_t p = cell.value;
        const int type = orientations ? cell.orientation : 0;
        const int x = room.x + cx, y = room.y + cy;
        if (exitTile(type) && style(p) >= 8) continue;
        if (type == 0 && style(p) == 30 && sequence(p) <= 1) p |= 0x80000000;
        if (p & 0x80000000) {
            if (doorTile(type)) { door(current, {}, type, p, x, y); continue; }
            if (exitTile(type)) { addWarpUnit(current, p, type, x, y); floorWarp(current, p, type, x, y); continue; }
        }
        if (p & 4) {
            std::optional<int> linkedType;
            if (p & 2) {
                linkedType = 0;
                if (style(p) == 30 && sequence(p) <= 1) p &= ~0x80u;
            } else if (p & 1) linkedType = type;
            else if ((p & 0x8000000) && !(p & 0x80000000)) linkedType = 13;
            if (linkedType) {
                if (const auto old = find(current, near, *linkedType, p, x, y)) update(current, *old, *linkedType, p, x, y);
                else linked(current, *linkedType, p, x, y);
                continue;
            }
        }
        if (p & 2) create(current, 0, p, x, y);
        else if (fillBlanks && inside(room, x, y)) {
            auto &r = rooms_.at(current);
            const uint32_t blank = (30u << 20) | (uint32_t(r.level == 74) << 8);
            const auto &tile = r.libraries->pick(0, blank, r.room.seed.random);
            create(current, 0, (p & ~0x80u) | 0x80000000, x, y, nullptr, &tile);
        }
        if (p & 1) {
            const auto index = create(current, type, p, x, y, nullptr, nullptr, true);
            if (exitTile(type)) wallWarp(current, index, p, type);
        }
        if (p & 0x8000000) create(current, 13, p, x, y);
    }
}

void RetailTileMaterializer::animate(size_t current, int speed) {
    if (speed < 0) return;
    auto &room = rooms_.at(current);
    if (!speed) speed = 80;
    const auto append = [&](const std::vector<size_t> &array) {
        const auto initial = array; // Native loop bounds precede frame allocation.
        for (const auto index : initial) {
            auto &tile = tiles_.at(index);
            if (!tile.tile->animated()) continue;
            const auto candidates = room.libraries->matching(tile.type, tile.packed);
            if (candidates.empty()) throw std::runtime_error("Native animation has no original frames");
            const auto frame = [&](int rarity) -> const Tile * {
                for (const auto *candidate : candidates)
                    if (candidate->rarity == rarity) return candidate;
                throw std::runtime_error("Native animation has a missing DT1 frame identity");
            };
            RetailTileRoom::Animation animation{{index}, speed};
            tile.tile = frame(0);
            for (size_t ordinal = 1; ordinal < candidates.size(); ++ordinal) {
                const auto next = create(current, tile.type, tile.packed, tile.x, tile.y,
                    nullptr, frame(int(ordinal)));
                tiles_.at(next).flags |= 8;
                animation.frames.push_back(next);
            }
            room.animations.insert(room.animations.begin(), std::move(animation));
        }
    };
    append(room.walls); append(room.floors); append(room.shadows);
}
void RetailTileMaterializer::loadOutdoor(size_t index, const RetailOutdoorRoomData &data,
    std::span<const size_t> near) {
    if (failed_) throw std::runtime_error("Discard failed native tile session before loading rooms");
    auto &r = rooms_.at(index);
    validateNear(index, near, rooms_);
    if (r.loaded || data.room.preset || data.room.x != r.room.x || data.room.y != r.room.y ||
        data.room.width != r.room.width || data.room.height != r.room.height ||
        data.tiles != r.libraries) throw std::runtime_error("Mismatched or duplicate native outdoor room");
    failed_ = true;
    r.near.assign(near.begin(), near.end());
    const auto linkedFlags = r.room.flags & 0x800000;
    r.room = data.room; // Includes the stream consumed by themes/immediate shadows.
    r.room.flags |= linkedFlags;
    r.authoredUnits = data.units;
    r.grids.width = data.grids.width; r.grids.height = data.grids.height;
    r.grids.act = 0;
    r.grids.floors = {data.grids.floors}; r.grids.walls = {data.grids.walls};
    r.grids.shadows = data.grids.shadows;
    for (const auto &shadow : data.shadows) create(index, 13, shadow.value, shadow.x, shadow.y, nullptr, shadow.tile);
    // DRLGOUTROOM_InitializeDrlgOutdoorRoom: wall grid precedes floor grid.
    layer(index, near, data.grids.walls, data.grids.width, true, false, false, false);
    layer(index, near, data.grids.floors, data.grids.width, false, false, false, false);
    r.loaded = true;
    failed_ = false;
}

void RetailTileMaterializer::preparePresetUnits(size_t index, bool preloaded,
    const std::function<std::vector<RetailPresetUnit>(Seed &)> &loadLazyUnits,
    std::span<const RetailPresetUnit> preloadedUnits,
    const std::function<std::vector<RetailPresetUnit>()> &clientUnits) {
    if (failed_) throw std::runtime_error("Discard failed native tile session before preparing units");
    auto &r = rooms_.at(index);
    if (!r.room.preset) throw std::runtime_error("Native preset unit preparation requires a preset room");
    const auto key = std::tuple{r.level, r.room.preset, r.room.file, r.room.mapX, r.room.mapY};
    if (presetUnits_.contains(key)) return;
    failed_ = true;
    if (preloaded) presetUnits_[key] = {preloadedUnits.begin(), preloadedUnits.end()};
    else {
        if (!loadLazyUnits) throw std::runtime_error("Native lazy preset units require an MPQ loader");
        presetUnits_[key] = loadLazyUnits(r.room.seed.random);
    }
    if (clientUnits) {
        auto extra = clientUnits();
        auto &pending = presetUnits_.at(key);
        pending.insert(pending.begin(), extra.begin(), extra.end());
    }
    failed_ = false;
}
void RetailTileMaterializer::loadPreset(size_t index, const RetailPresetRoomData &data,
    std::span<const size_t> near,
    const std::function<std::vector<RetailPresetUnit>(Seed &)> &loadLazyUnits,
    std::span<const RetailPresetUnit> preloadedUnits) {
    if (failed_) throw std::runtime_error("Discard failed native tile session before loading rooms");
    auto &r = rooms_.at(index);
    validateNear(index, near, rooms_);
    if (r.loaded || !data.room.preset || data.room.x != r.room.x || data.room.y != r.room.y ||
        data.room.width != r.room.width || data.room.height != r.room.height ||
        data.room.preset != r.room.preset || data.room.file != r.room.file)
        throw std::runtime_error("Mismatched or duplicate native preset room");
    preparePresetUnits(index, data.unitsPreloaded, loadLazyUnits, preloadedUnits);
    failed_ = true;
    r.near.assign(near.begin(), near.end());
    const auto linkedFlags = r.room.flags & 0x800000;
    r.room = data.room;
    r.room.flags |= linkedFlags;
    r.roofPopups = data.roofPopups;
    r.grids = data.grids;
    const auto key = std::tuple{r.level, r.room.preset, r.room.file, r.room.mapX, r.room.mapY};
    // InitPresetRoomGrids moves accepted map units into the current room and
    // prepends them. Half-open subtile bounds prevent shared-edge duplicates.
    auto &pending = presetUnits_.at(key);
    for (auto unit = pending.begin(); unit != pending.end();) {
        const auto &position = unit->unit;
        if (position.x >= r.room.x * 5 && position.y >= r.room.y * 5 &&
            position.x < (r.room.x + r.room.width) * 5 &&
            position.y < (r.room.y + r.room.height) * 5) {
            r.authoredUnits.insert(r.authoredUnits.begin(), std::move(*unit));
            unit = pending.erase(unit);
        } else ++unit;
    }
    // InitRoomGrids resets after lazy unit preparation, before any DT1 draw.
    // Keep the stream advanced by themes for outdoor rooms in loadOutdoor.
    r.room.seed.random = Seed(r.room.seed.initial);
    for (size_t i = 0; i < data.grids.floors.size(); ++i)
        layer(index, near, data.grids.floors[i], data.grids.width, false,
            i == 0 && data.fillBlanks, data.killEdgeX, data.killEdgeY);
    for (const auto &wall : data.grids.walls)
        layer(index, near, wall, data.grids.width, true, false, data.killEdgeX, data.killEdgeY);
    if (!data.grids.shadows.empty())
        layer(index, near, data.grids.shadows, data.grids.width, false, false, data.killEdgeX, data.killEdgeY);
    const auto &preset = catalog_.presets().at(r.room.preset);
    if (preset.animate) animate(index, preset.animationSpeed);
    r.loaded = true;
    failed_ = false;
}

void RetailTileMaterializer::releaseRoom(size_t index) {
    if (failed_) throw std::runtime_error("Discard failed native tile session before releasing rooms");
    auto &r = rooms_.at(index);
    for (auto adjacent : r.activeNear) {
        if (adjacent == index) continue;
        auto &list = rooms_.at(adjacent).activeNear;
        const auto found = std::find(list.begin(), list.end(), index);
        if (found != list.end()) { *found = list.back(); list.pop_back(); }
    }
    r.activeNear.clear();
    collisions_.erase(index);
    r.floors.clear(); r.walls.clear(); r.shadows.clear();
    r.mapLinks = {};
    r.animations.clear(); r.roofPopups.clear(); r.units.clear();
    r.grids = {};
    for (auto &warp : r.warps) { warp.visible.reset(); warp.lit.reset(); }
    r.loaded = false;
    // Unit ownership, loaded libraries and the established near graph survive.
    // The next InitRoomGrids resets the tile stream to the original init seed.
}

void sortRetailNearRooms(std::vector<size_t> &near, const std::deque<RetailTileRoom> &rooms) {
    for (size_t pass = near.size(); pass > 1; --pass) for (size_t i = 0; i + 1 < near.size(); ++i) {
        const auto &a = rooms.at(near[i]).room, &b = rooms.at(near[i + 1]).room;
        if (a.x >= b.x + b.width || a.y >= b.y + b.height) std::swap(near[i], near[i + 1]);
    }
}

void RetailTileMaterializer::activateCollision(size_t room, std::span<const size_t> activeNear) {
    if (collisions_.contains(room)) throw std::runtime_error("Native room collision already active");
    auto &current = rooms_.at(room);
    current.activeNear.assign(activeNear.begin(), activeNear.end());
    for (auto adjacent : current.activeNear) {
        if (adjacent == room) continue;
        auto &other = rooms_.at(adjacent);
        other.activeNear.clear();
        for (auto candidate : other.near)
            if (candidate == room || collisions_.contains(candidate)) other.activeNear.push_back(candidate);
    }
    auto grid = projectRetailRoomCollision(*this, room, activeNear);
    collisions_.emplace(room, std::move(grid));
}
const RetailRoomCollision *RetailTileMaterializer::collision(size_t room) const {
    if (!valid()) throw std::runtime_error("Cannot read failed native tile session collision");
    const auto found = collisions_.find(room);
    return found == collisions_.end() ? nullptr : &found->second;
}

RetailRoomCollision projectRetailRoomCollision(const RetailTileMaterializer &session, size_t index,
    std::span<const size_t> activeNear) {
    if (!session.valid()) throw std::runtime_error("Cannot project collision from a failed native tile session");
    validateNear(index, activeNear, session.rooms());
    const auto &room = session.rooms().at(index).room;
    RetailRoomCollision result{room.x * 5, room.y * 5, room.width * 5, room.height * 5, {}};
    result.flags.resize(size_t(result.width) * size_t(result.height));
    for (const auto owner : activeNear) {
        const auto &r = session.rooms().at(owner);
        if (!r.loaded) throw std::runtime_error("Collision projection requires a materialized active room");
        const auto apply = [&](const auto &indices) {
            for (auto tileIndex : indices) {
                const auto &t = session.tiles().at(tileIndex);
                const int x = t.x * 5 - result.x, y = t.y * 5 - result.y;
                // Original initial-grid routine tests tile origin against this
                // room before copying the flipped five-by-five DT1 flag block.
                if (x < 0 || y < 0 || x >= result.width || y >= result.height) continue;
                const uint16_t extra = uint16_t(((t.flags & 2) ? 0x10 : 0) |
                    ((t.flags & 0x40) ? 1 : 0) | ((t.flags & 0x80) ? 4 : 0));
                for (int dy = 0; dy < 5 && y + dy < result.height; ++dy)
                    for (int dx = 0; dx < 5 && x + dx < result.width; ++dx)
                        result.flags[size_t(y + dy) * size_t(result.width) + size_t(x + dx)] |=
                            uint16_t(t.tile->flags[size_t(4 - dy) * 5 + size_t(dx)]) | extra;
            }
        };
        apply(r.floors); apply(r.walls); apply(r.shadows);
    }
    return result;
}
} // namespace d2x
