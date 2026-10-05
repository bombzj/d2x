#include "room_themes.hpp"
#include <algorithm>
#include <stdexcept>

// D2MOO DrlgTileSub and InitOutdoorRoomGrids. MIT: docs/licenses/D2MOO.txt.
namespace d2x {
namespace {
MapCell pattern(const std::vector<MapCell> &layer, const MapData &data, int x, int y) {
    if (layer.empty()) return {};
    if (x < 0 || y < 0 || x >= data.width || y >= data.height)
        throw std::runtime_error("Native theme group exceeds its DS1");
    return layer.at(size_t(y) * size_t(data.width) + size_t(x));
}
MapCell floor(const MapData &data, int x, int y) {
    return data.floors.empty() ? MapCell{} : pattern(data.floors.front(), data, x, y);
}
MapCell wall(const MapData &data, int x, int y) {
    return data.walls.empty() ? MapCell{} : pattern(data.walls.front(), data, x, y);
}
class Substitution {
    RetailRoom &room;
    RetailRoomGrids &grid;
    const RetailShadowEmitter &shadow;
    size_t offset(int x, int y) const {
        if (x < 0 || y < 0 || x >= grid.width || y >= grid.height)
            throw std::runtime_error("Native room theme exceeds logical grids");
        return size_t(y) * size_t(grid.width) + size_t(x);
    }
  public:
    Substitution(RetailRoom &r, RetailRoomGrids &g, const RetailShadowEmitter &s)
        : room(r), grid(g), shadow(s) {}
    bool fits(const MapData &data, const SubstitutionGroup &group, int x, int y, bool exact) const {
        for (int j = 0; j < group.height; ++j)
            for (int i = 0; i < group.width; ++i) {
                const auto f = floor(data, group.x + i, group.y + j);
                const auto w = wall(data, group.x + i, group.y + j);
                const auto index = offset(x + i, y + j);
                const auto &targetF = grid.floors.at(index), &targetW = grid.walls.at(index);
                if (exact) {
                    if (w.orientation != targetW.orientation) return false;
                    if ((f.value & 2) && (!(targetF.value & 2) || ((f.value ^ targetF.value) & 0x3F0FF00)))
                        return false;
                    if ((w.value & 1) && (!(targetW.value & 1) || ((w.value ^ targetW.value) & 0x3F0FF00)))
                        return false;
                } else if ((f.value & 2) || (w.value & 1)) {
                    if ((targetF.value & 0x3F0FF00) || !(targetF.value & 2) || (targetW.value & 1))
                        return false;
                }
            }
        return true;
    }
    void stamp(const MapData &data, const SubstitutionGroup &group, int x, int y, int variant) {
        // Position draws and fitting precede this successful no-op.
        if (!group.width || !group.height) return;
        const int variantX = variant * (group.width + 1);
        for (int j = 0; j < group.height; ++j)
            for (int i = 0; i < group.width; ++i) {
                const int px = group.x + variantX + i, py = group.y + j;
                const auto index = offset(x + i, y + j);
                const auto f = floor(data, px, py), w = wall(data, px, py);
                if (f.value & 2) { grid.floors.at(index) = f; grid.floors.at(index).value |= 0x80; }
                // An outdoor room has a single wall slot. Extra pattern layers
                // have no destination in the native outdoor call.
                if (w.value & 1) grid.walls.at(index).value = w.value;
                if (w.orientation) grid.walls.at(index).orientation = w.orientation;
                const auto s = pattern(data.shadows, data, px, py);
                if (s.value & 0x8000000) {
                    if (!shadow) throw std::runtime_error("Native theme shadow requires immediate DT1 selection");
                    shadow(room.x + x + i, room.y + y + j, s.value, room.seed.random);
                }
            }
        // Native substitution copies units from the unshifted matching group,
        // with strict interior bounds, independently of the chosen tile variant.
        for (auto source = data.objects.rbegin(); source != data.objects.rend(); ++source) {
            if (source->x <= group.x * 5 || source->y <= group.y * 5 ||
                source->x >= (group.x + group.width) * 5 ||
                source->y >= (group.y + group.height) * 5) continue;
            auto unit = *source;
            unit.x += (x - group.x) * 5;
            unit.y += (y - group.y) * 5;
            unit.path.clear(); // AllocPresetUnit does not copy the file's MapAI here.
            grid.units.insert(grid.units.begin(), {std::move(unit), data.version, data.act});
        }
    }
    void apply(const SubstitutionRecord &record, const MapData &data, int theme) {
        if (theme < 0 || theme >= 5) throw std::runtime_error("Invalid native room theme");
        if (data.substitutionGroups.empty()) return;
        auto &random = room.seed.random;
        if (record.checkAll) {
            for (const auto &group : data.substitutionGroups) {
                const int width = room.width - group.width + 1, height = room.height - group.height + 1;
                if (width <= 0 || height <= 0) continue;
                if (data.substitutionMethod != 1 && data.substitutionMethod != 2) continue;
                const bool exact = data.substitutionMethod == 2;
                const int begin = exact ? 0 : 1;
                for (int y = begin; y < height; ++y)
                    for (int x = begin; x < width; ++x) {
                        if (!fits(data, group, x, y, exact)) continue;
                        if (!exact) stamp(data, group, x, y, 0);
                        // Original comparison is Prob < roll, not roll < Prob.
                        else if (record.probability[size_t(theme)] < random.below(100))
                            stamp(data, group, x, y, random.below(group.variants) + 1);
                    }
            }
        } else {
            for (int attempt = 0; attempt < record.maximum[size_t(theme)]; ++attempt) {
                const auto &group = data.substitutionGroups.at(size_t(random.below(int(data.substitutionGroups.size()))));
                const int width = room.width - group.width, height = room.height - group.height;
                if (width <= 0 || height <= 0) continue;
                const int trials = record.trials[size_t(theme)];
                if (trials != -1) {
                    for (int trial = 0; trial < trials; ++trial) {
                        const int x = random.below(width) + 1, y = random.below(height) + 1;
                        if (fits(data, group, x, y, false)) { stamp(data, group, x, y, 0); break; }
                    }
                } else {
                    std::vector<std::pair<int, int>> positions;
                    for (int y = 0; y < height; ++y)
                        for (int x = 0; x < width; ++x) positions.emplace_back(x + 1, y + 1);
                    const int count = int(positions.size());
                    for (int i = 0; i < count; ++i) {
                        const int a = random.below(count), b = random.below(count);
                        std::swap(positions[size_t(a)], positions[size_t(b)]);
                    }
                    for (const auto &[x, y] : positions)
                        if (fits(data, group, x, y, false)) { stamp(data, group, x, y, 0); break; }
                }
            }
        }
    }
};
}
void applyRetailOutdoorRoomThemes(const WorldCatalog &catalog, const LevelRecord &level,
    RetailRoom &room, RetailRoomGrids &grid, const RetailPatternReader &reader,
    const RetailShadowEmitter &shadow) {
    if (room.preset || !reader)
        throw std::runtime_error("Native room themes require outdoor room data");
    Substitution substitution(room, grid, shadow);
    auto apply = [&](int type, int theme, uint32_t mask) {
        if (type == -1 || !mask) return;
        unsigned index = 0;
        for (const auto &record : catalog.substitutions()) {
            if (record.type != type) continue;
            if (index >= 32) throw std::runtime_error("Native substitution mask exceeds 32 bits");
            if (mask & (1u << index)) substitution.apply(record, reader(record.file), theme);
            ++index;
        }
        if (index < 32 && (mask >> index))
            throw std::runtime_error("Native substitution mask refers to absent MPQ records");
    };
    apply(level.waypointSubstitution, 0, (room.flags >> 16) & 3);
    apply(level.shrineSubstitution, 0, (room.flags >> 12) & 15);
    apply(level.subtype, level.theme, room.themeMask);
    uint32_t biome = 0;
    switch (level.levelType) {
    case 16: biome = 0x100; break;
    case 21: biome = 0x120000; break;
    case 22: biome = 0x100000; break;
    case 27: biome = 0xA00000; break;
    case 28: biome = 0x1600000; break;
    case 31: if (level.id == 117) biome = 0x600000; break;
    }
    for (auto &floor : grid.floors) if (!(floor.value & 0x3F0FF80)) floor.value |= biome;
    // Native edge tagging happens after all substitutions and does not consume RNG.
    for (int y = 0; y < grid.height; ++y)
        for (int x = 0; x < grid.width; ++x)
            if (!x || !y || x == grid.width - 1 || y == grid.height - 1) {
                const auto index = size_t(y) * size_t(grid.width) + size_t(x);
                grid.floors.at(index).value |= 4;
                grid.walls.at(index).value |= 4;
            }
}
} // namespace d2x
