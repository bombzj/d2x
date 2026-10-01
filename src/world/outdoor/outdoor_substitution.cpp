#include "outdoor_substitution.hpp"
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace d2x {
void applyOutdoorBorder(const SubstitutionRecord &record, const MapData &pattern, int width, int height,
                        std::span<OutdoorCell> cells, Seed &seed) {
    if (width < 1 || height < 1 || cells.size() != size_t(width) * height || record.gridSize != 1 ||
        record.borderType < 0 || record.borderType > 2 || pattern.substitutionGroups.empty())
        throw std::runtime_error("Unsupported outdoor border substitution: " + record.file);
    auto wallAt = [&](int x, int y) {
        return pattern.walls.empty() ? 0u : pattern.walls.front().at(size_t(y) * pattern.width + x).value;
    };
    auto floorAt = [&](int x, int y) {
        return pattern.floors.front().at(size_t(y) * pattern.width + x).value;
    };
    int first = record.borderType == 0 ? seed.below(int(pattern.substitutionGroups.size())) : 0;
    for (size_t step = 0; step < pattern.substitutionGroups.size(); ++step) {
        const auto &group = pattern.substitutionGroups[(first + step) % pattern.substitutionGroups.size()];
        if (group.variants < 1 ||
            int64_t(group.x) + int64_t(group.variants) * (group.width + 1) + group.width > pattern.width)
            throw std::runtime_error("Invalid outdoor replacement variants: " + record.file);
        int columns = width - group.width + 1, rows = height - group.height + 1;
        if (columns <= 0 || rows <= 0)
            continue;
        std::vector<int> positions(size_t(columns) * rows);
        std::iota(positions.begin(), positions.end(), 0);
        for (size_t shuffle = 0; shuffle < positions.size(); ++shuffle) {
            int left = seed.below(int(positions.size()));
            int right = seed.below(int(positions.size()));
            std::swap(positions[left], positions[right]);
        }
        for (int position : positions) {
            int originX = position % columns, originY = position / columns;
            bool matches = true;
            for (int row = 0; row < group.height && matches; ++row)
                for (int column = 0; column < group.width; ++column) {
                    auto wall = wallAt(group.x + column, group.y + row);
                    auto floor = floorAt(group.x + column, group.y + row);
                    const auto &cell = cells[(originY + row) * width + originX + column];
                    int code = int((wall >> 8) & 255) - 1;
                    if ((wall & 1) ? ((code != 62 && cell.preset != code + 4) || cell.levelLink)
                                   : ((floor & 2) && (cell.preset || cell.blank || cell.levelLink))) {
                        matches = false;
                        break;
                    }
                }
            if (!matches)
                continue;
            int offset = (seed.below(group.variants) + 1) * (group.width + 1);
            for (int row = 0; row < group.height; ++row)
                for (int column = 0; column < group.width; ++column) {
                    auto wall = wallAt(group.x + offset + column, group.y + row);
                    auto floor = floorAt(group.x + offset + column, group.y + row);
                    auto &cell = cells[(originY + row) * width + originX + column];
                    if (wall & 1) {
                        int code = int((wall >> 8) & 255) - 1;
                        if (code != 62)
                            cell = {code + 4, 0, false, false};
                    } else
                        cell = {0, -1, !(floor & 2), false};
                }
            if (record.borderType == 0)
                return;
            if (record.borderType == 1)
                break;
        }
    }
}
} // namespace d2x