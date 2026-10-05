#include "secondary_border.hpp"
#include <algorithm>
#include <stdexcept>

// D2MOO DrlgTileSub::AddSecondaryBorder/TestReplaceSubPreset/ReplaceSubPreset.
// MIT: docs/licenses/D2MOO.txt. Metadata and pattern groups come from current MPQ.
namespace d2x {
void replaceRetailSecondaryBorder(const WorldCatalog &catalog, const SubstitutionRecord &record,
                                 const MapData &pattern, int level, uint32_t outdoorFlags,
                                 int firstPreset, RetailOutdoorGrid &grid) {
    // Only 0 and 1 terminate early in the native loop; -1 is an authored
    // all-matches mode (for example Act I waypoints), not an invalid enum.
    if (record.gridSize < 1)
        throw std::runtime_error("Unsupported native secondary border: " + record.file);
    if (pattern.substitutionGroups.empty()) return;
    auto &random = grid.random();
    const int groups = int(pattern.substitutionGroups.size());
    const int first = record.borderType == 0 ? random.below(groups) : 0;
    const auto value = [&](const auto &layers, int x, int y) {
        return layers.empty() ? 0u : layers.front().at(size_t(y) * pattern.width + size_t(x)).value;
    };
    for (int index = 0; index < groups; ++index) {
        const auto &group = pattern.substitutionGroups[size_t((first + index) % groups)];
        const int width = (record.type == 1 && (outdoorFlags & 12) ? -1 : 1) +
                          grid.width() - record.gridSize * group.width;
        const int height = 1 + grid.height() - record.gridSize * group.height;
        if (width <= 0 || height <= 0) continue;
        if (group.variants < 1 || int64_t(group.x) + int64_t(group.variants) * (group.width + 1) +
            group.width > pattern.width)
            throw std::runtime_error("Invalid native secondary border variants: " + record.file);
        const bool small = record.type == 1 && level >= 2 && level <= 7 && width < 6 && height < 6;
        std::vector<std::pair<int, int>> positions;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) positions.emplace_back(x, y);
        const int count = int(positions.size());
        for (int i = 0; i < count; ++i) {
            const int left = random.below(count), right = random.below(count);
            std::swap(positions[size_t(left)], positions[size_t(right)]);
        }
        for (const auto &[candidateX, candidateY] : positions) {
            if (small && candidateX == 2 && candidateY == 2) continue;
            const int ox = candidateX - candidateX % record.gridSize;
            const int oy = candidateY - candidateY % record.gridSize;
            bool matches = true;
            for (int y = 0; y < group.height && matches; ++y)
                for (int x = 0; x < group.width; ++x) {
                    const int gx = ox + x * record.gridSize, gy = oy + y * record.gridSize;
                    if (!grid.contains(gx, gy)) { matches = false; break; }
                    const uint32_t wall = value(pattern.walls, group.x + x, group.y + y);
                    const uint32_t floor = value(pattern.floors, group.x + x, group.y + y);
                    const auto &cell = grid.cell(gx, gy);
                    if (wall & 1) {
                        const int code = int((wall >> 8) & 255) - 1;
                        if ((code != 62 && code + firstPreset != cell.preset) || (cell.flags & 0x400)) {
                            matches = false; break;
                        }
                    } else if ((floor & 2) && !grid.canPlace(catalog, 0, gx, gy)) {
                        matches = false; break;
                    }
                }
            if (!matches) continue;
            const int offset = (random.below(group.variants) + 1) * (group.width + 1);
            for (int y = 0; y < group.height; ++y)
                for (int x = 0; x < group.width; ++x) {
                    const int gx = ox + x * record.gridSize, gy = oy + y * record.gridSize;
                    const uint32_t wall = value(pattern.walls, group.x + offset + x, group.y + y);
                    const uint32_t floor = value(pattern.floors, group.x + offset + x, group.y + y);
                    if (wall & 1) {
                        const int code = int((wall >> 8) & 255) - 1;
                        if (code != 62 && code + firstPreset != -5) {
                            const int preset = code + firstPreset;
                            grid.place(catalog, preset, gx, gy, 0,
                                (preset >= 4 && preset <= 15) || (preset >= 364 && preset <= 375));
                        }
                    } else if (floor & 2) grid.clear(gx, gy);
                    else grid.blank(gx, gy);
                }
            if (record.borderType == 0) return;
            if (record.borderType == 1) break;
        }
    }
}
} // namespace d2x
