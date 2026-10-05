#include "world/outdoor/native_act_layout.hpp"
#include "native_jungle_layout.hpp"
#include "world/outdoor/native_connections.hpp"
#include "world/generation_seed.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>

// Rule evidence: D2MOO DrlgOutPlace/DrlgDrlg; MIT attribution in
// docs/licenses/D2MOO.txt. This value-based placement walk does not execute DLLs.
namespace d2x {
namespace {
enum class Rule { Fixed, Four, Eight, EightAligned, Two, Moor, Town, South, Mirror,
                  OrientedRelative, OrientedAbsolute, OrientedTable };
struct Node { int level, parent; Rule rule; };
struct WalkState {
    NativeLevelPlacement value;
    int initial{-1}, initialAlignment{-1};
};
bool separate(const NativeLevelPlacement &a, const NativeLevelPlacement &b) {
    return a.x + a.width <= b.x || b.x + b.width <= a.x ||
           a.y + a.height <= b.y || b.y + b.height <= a.y;
}
// Coordinate transforms are executable rules; all ordinary sizes and absolute
// offsets are taken from the current MPQ, including the selected difficulty.
void edge(NativeLevelPlacement &a, const NativeLevelPlacement &b, int side,
          bool reverse, int nudge) {
    switch (side) {
    case 0: a.x = reverse ? b.x + b.width - a.width + nudge : b.x - nudge;
            a.y = b.y + b.height; break;
    case 1: a.x = b.x - a.width;
            a.y = reverse ? b.y + b.height - a.height + nudge : b.y - nudge; break;
    case 2: a.x = reverse ? b.x - nudge : b.x + b.width - a.width + nudge;
            a.y = b.y - a.height; break;
    case 3: a.x = b.x + b.width;
            a.y = reverse ? b.y - nudge : b.y + b.height - a.height + nudge; break;
    }
}
void placeList(const WorldCatalog &catalog, NativeActLayout &out, std::span<const Node> nodes,
               Seed random, int gate) {
    std::vector<WalkState> states;
    for (const auto &n : nodes) {
        const auto &record = catalog.level(n.level);
        WalkState state;
        state.value = {n.level, record.offsetX, record.offsetY, record.width, record.height};
        states.push_back(state);
    }
    int cursor = 0;
    size_t dispatches = 0;
    while (cursor >= 0 && cursor < int(nodes.size())) {
        if (++dispatches > 1000000)
            throw std::runtime_error("Retail DRLG placement exhausted its bounded walk");
        auto &state = states[size_t(cursor)];
        auto &a = state.value;
        const auto &node = nodes[size_t(cursor)];
        const auto rule = node.rule;
        const bool flipped = rule == Rule::Moor || rule == Rule::Town;
        bool exhausted = false;
        if (rule == Rule::Fixed) {
            const auto &record = catalog.level(node.level);
            a.x = record.offsetX; a.y = record.offsetY;
        } else {
            const auto &b = node.parent >= 0 ? states.at(size_t(node.parent)).value : a;
            if (rule == Rule::South) {
                a.direction = state.initial = 0;
            } else if (rule == Rule::Mirror) {
                a.direction = state.initial = 3;
                a.alignment = int(random.next() & 1);
            } else if (rule == Rule::OrientedRelative || rule == Rule::OrientedAbsolute) {
                a.direction = state.initial = int(random.next() & 1);
            } else if (state.initial < 0) {
                const int count = rule == Rule::Two ? 2 : rule == Rule::Eight ||
                                  rule == Rule::EightAligned ? 8 : rule == Rule::OrientedTable ? 2 : 4;
                a.direction = state.initial = int(random.next() & uint32_t(count - 1));
                if (rule == Rule::Two) ++a.direction, ++state.initial;
                if (flipped)
                    a.alignment = state.initialAlignment = int(random.next() & 1);
            } else if (flipped) {
                a.direction = (a.direction + a.alignment) & 3;
                a.alignment = (a.alignment + 1) & 1;
                exhausted = a.direction == state.initial && a.alignment == state.initialAlignment;
            } else {
                const int count = rule == Rule::Eight || rule == Rule::EightAligned ? 8 :
                                  rule == Rule::Two || rule == Rule::OrientedTable ? 2 : 4;
                a.direction = rule == Rule::Two ? 3 - a.direction : (a.direction + 1) & (count - 1);
                exhausted = a.direction == state.initial;
            }
            if (!exhausted) {
                switch (rule) {
                case Rule::Four: edge(a, b, a.direction, false, 16); break;
                case Rule::Moor:
                    // DRLG's orientation-dependent Blood Moor dimensions, not item/map data.
                    a.width = a.direction & 1 ? 96 : 56;
                    a.height = a.direction & 1 ? 56 : 96;
                    edge(a, b, a.direction, a.alignment == 0, 16); break;
                case Rule::Town:
                    edge(a, b, a.direction, a.alignment == 0, 0);
                    if (a.direction == 1)
                        a.y += a.alignment ? 8 : -8;
                    if (a.direction == 3)
                        a.y += a.alignment ? -8 : 8;
                    break;
                case Rule::Two: edge(a, b, a.direction, a.direction == 2, 0); break;
                case Rule::Eight:
                case Rule::EightAligned: {
                    const int side = a.direction / 2;
                    a.x = side == 1 ? b.x - a.width : side == 3 ? b.x + b.width : b.x;
                    a.y = side == 0 ? b.y + b.height : side == 2 ? b.y - a.height : b.y;
                    if (rule == Rule::Eight) {
                        const int delta = a.direction & 1 ? 1 : -1;
                        if (side & 1) a.y += delta * (a.height / 2 + 8);
                        else a.x += delta * (a.width / 2 + 8);
                    }
                    break;
                }
                case Rule::South: a.x = b.x; a.y = b.y + b.height; break;
                case Rule::Mirror: a.x = b.x + b.width;
                    a.y = a.alignment ? b.y + b.height - a.height + 8 : b.y - 8; break;
                case Rule::OrientedRelative:
                case Rule::OrientedAbsolute:
                case Rule::OrientedTable: {
                    a.width = a.direction ? 160 : 64; a.height = a.direction ? 64 : 160;
                    if (rule == Rule::OrientedRelative) {
                        a.x = b.x - a.width; a.y = b.y + b.height - a.height - 16;
                    } else if (rule == Rule::OrientedTable) {
                        constexpr std::array dx{0, -96, -64, -160}, dy{-160, -64, -96, 0};
                        const auto index = size_t(a.direction + b.direction * 2);
                        a.x = b.x + dx.at(index); a.y = b.y + dy.at(index);
                    }
                    break;
                }
                case Rule::Fixed: break;
                }
            }
        }
        if (exhausted) {
            state.initial = state.initialAlignment = a.direction = a.alignment = -1;
            --cursor;
            continue;
        }
        bool valid = true;
        if (gate) {
            for (int previous = 0; previous < cursor; ++previous)
                if (previous != node.parent && !separate(a, states[size_t(previous)].value))
                    valid = false;
        }
        if (valid && gate == 2 && node.level == 1) {
            constexpr std::array<uint8_t, 64> allowed{
                1,1,0,0,0,0,0,0, 0,1,0,0,0,0,0,0,
                0,0,0,1,0,1,0,0, 1,0,0,0,0,0,1,1,
                0,1,0,0,0,0,0,1, 0,1,0,0,0,0,0,0,
                0,0,0,0,0,1,1,0, 1,0,0,0,0,0,1,1};
            const auto &b = states.at(size_t(node.parent)).value;
            valid = allowed.at(size_t(a.direction + 4 * (a.alignment + 2 *
                                      (b.direction + 4 * b.alignment)))) != 0;
        }
        if (valid && gate == 2 && node.level == 17)
            for (size_t i = 0; i < nodes.size(); ++i)
                if (i != size_t(cursor) && nodes[i].parent == node.parent &&
                    states[i].value.direction == a.direction) valid = false;
        if (valid && gate == 3 && cursor) {
            auto clearance = states.front().value;
            clearance.y -= 200; clearance.height += 200;
            valid = separate(a, clearance);
        }
        if (valid) ++cursor;
    }
    if (cursor < 0)
        throw std::runtime_error("Retail DRLG placement has no valid arrangement");
    for (size_t i = 0; i < nodes.size(); ++i) {
        out.levels[nodes[i].level] = states[i].value;
        if (gate == 2 || gate == 3) {
            // sub_6FD82360 compares the current and following DIRECTIONS,
            // not the alignment bits. The terminal entry's direction is -1.
            struct RoadRule { int level, excluded1, excluded2, direction, next; uint32_t flags; };
            constexpr std::array<RoadRule, 15> roads{{
                {0,2,3,1,0,0x04}, {0,2,3,2,3,0x04},
                {0,3,17,2,1,0x08}, {0,3,17,3,0,0x08},
                {0,3,17,1,1,0x10}, {0,3,17,3,3,0x10},
                {2,0,0,0,0,0x08}, {2,0,0,2,2,0x08},
                {2,0,0,3,0,0x08}, {2,0,0,3,2,0x08},
                {2,0,0,0,1,0x400}, {2,0,0,1,1,0x400},
                {2,0,0,2,1,0x200}, {2,0,0,2,2,0x80}, {2,0,0,3,2,0x100}}};
            const int level = nodes[i].level;
            const int next = i + 1 < nodes.size() ? states[i + 1].value.direction : -1;
            if (catalog.level(level).generation == GenerationKind::Outdoor)
                for (const auto &road : roads)
                    if ((!road.level || road.level == level) && level != road.excluded1 &&
                        level != road.excluded2 && states[i].value.direction == road.direction &&
                        next == road.next) out.levels.at(level).outdoorFlags |= road.flags;
        }
        if (nodes[i].rule == Rule::Mirror)
            out.levels.at(nodes[i].level).outdoorFlags = states[i].value.alignment ? 0x800000 : 0x400000;
        if (nodes[i].parent >= 0) {
            const int parent = nodes[size_t(nodes[i].parent)].level;
            out.links.emplace(nodes[i].level, parent);
            out.links.emplace(parent, nodes[i].level);
            if (catalog.level(nodes[i].level).act != 4) {
                connectNativeLevels(catalog, out, nodes[i].level, parent);
                connectNativeLevels(catalog, out, parent, nodes[i].level);
            }
        }
    }
}
} // namespace
NativeActLayout placeNativeAct(const WorldCatalog &catalog, int act, uint32_t initialSeed) {
    if (act < 0 || act > 4) throw std::invalid_argument("Invalid native act");
    NativeActLayout result;
    Seed random(initialSeed);
    result.startSeed = random.next();
    if (act == 1) {
        uint32_t staff, boss;
        do { staff = random.next() % 7; boss = random.next() % 7; } while (staff == boss);
        result.staffTomb = 66 + int(staff);
        result.bossTomb = 66 + int(boss);
    }
    if (act == 2) result.jungleInterlink = (random.next() & 1) != 0;
    auto list = [&](std::initializer_list<Node> nodes, int gate) {
        placeList(catalog, result, {nodes.begin(), nodes.size()}, random, gate);
    };
    if (act == 0) {
        list({{4,-1,Rule::Fixed},{3,0,Rule::Four},{2,1,Rule::Moor},
              {1,2,Rule::Town},{17,1,Rule::Four}}, 2);
        list({{39,-1,Rule::Fixed},{26,-1,Rule::Fixed},{7,1,Rule::South},
              {6,2,Rule::Four},{5,3,Rule::Four}}, 3);
        result.levels.at(1).presetVariant = result.levels.at(1).direction;
        const auto marshDirection = result.levels.at(6).direction;
        if (marshDirection == 1 || marshDirection == 3) {
            const auto bit = random.next() & 1;
            result.levels[27].presetVariant = marshDirection == 1 ? 2 - int(bit) : int(~bit & 1);
        }
    } else if (act == 1) {
        list({{40,-1,Rule::Fixed},{41,0,Rule::Two},{42,1,Rule::Eight},
              {43,2,Rule::Eight},{44,3,Rule::Eight},{45,4,Rule::EightAligned}}, 1);
        list({{46,-1,Rule::Fixed}}, 1);
        result.levels.at(40).presetVariant = result.levels.at(41).direction;
    } else if (act == 2) {
        placeNativeJungleAct(catalog, result, random);
    } else if (act == 3) {
        list({{103,-1,Rule::Fixed},{104,0,Rule::Mirror},{105,1,Rule::Four},
              {106,2,Rule::Four}}, 1);
        list({{108,-1,Rule::Fixed}}, 1);
    } else {
        list({{109,-1,Rule::Fixed},{110,0,Rule::Fixed},{111,1,Rule::OrientedRelative},
              {112,2,Rule::OrientedTable}}, 0);
        list({{117,-1,Rule::OrientedAbsolute}}, 0);
        list({{134,-1,Rule::Fixed},{136,-1,Rule::Fixed}}, 1);
    }
    // Depend chains are evaluated lazily with cycle detection, not capped by a
    // guessed depth or attached to the act town.
    std::set<int> active;
    std::function<void(int)> depend = [&](int id) {
        const auto found = result.levels.find(id);
        if (found != result.levels.end() && found->second.width > 0) return;
        const auto &record = catalog.level(id);
        if (record.act != act || !active.insert(id).second)
            throw std::runtime_error("Invalid native Levels.Depend chain");
        NativeLevelPlacement value{id,record.offsetX,record.offsetY,record.width,record.height};
        if (record.depend) {
            depend(record.depend);
            value.x += result.levels.at(record.depend).x;
            value.y += result.levels.at(record.depend).y;
        }
        if (found != result.levels.end()) value.presetVariant = found->second.presetVariant;
        result.levels[id] = value;
        active.erase(id);
    };
    for (const auto &[id, record] : catalog.levels())
        if (record.act == act) depend(id);
    finalizeNativeConnections(catalog, result, act);
    return result;
}
} // namespace d2x
