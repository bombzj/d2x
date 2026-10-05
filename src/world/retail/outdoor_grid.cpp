#include "outdoor_grid.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <stdexcept>

// Rule adaptation: D2MOO DrlgOutdoors.cpp (TestOutdoorLevelPreset,
// SpawnOutdoorLevelPresetEx, SpawnOutdoorLevelPreset, SpawnRandomOutdoorDS1,
// SpawnPresetFarAway) and DrlgDrlgRoom.cpp. MIT: docs/licenses/D2MOO.txt.
namespace d2x {
namespace {
std::pair<int, int> footprint(const WorldCatalog &catalog, int preset) {
    if (!preset) return {1, 1};
    const auto &record = catalog.presets().at(preset);
    const int width = record.width / 8, height = record.height / 8;
    if (width <= 0 || height <= 0)
        throw std::runtime_error("Outdoor preset has no eight-tile footprint");
    return {width, height};
}
} // namespace
RetailRoomSeed allocateRetailRoomSeed(Seed &levelRandom) {
    Seed random(levelRandom.next());
    const auto initial = random.next();
    return {random, initial};
}
RetailOutdoorGrid::RetailOutdoorGrid(int tileWidth, int tileHeight,
                                     uint32_t startSeed, int level)
    : width_(tileWidth / 8), height_(tileHeight / 8),
      random_(startSeed + uint32_t(level)) {
    if (level <= 0 || tileWidth <= 0 || tileHeight <= 0 || width_ <= 0 || height_ <= 0 ||
        width_ > 2048 || height_ > 2048)
        throw std::invalid_argument("Invalid retail outdoor grid dimensions");
    cells_.resize(size_t(width_) * size_t(height_));
}
bool RetailOutdoorGrid::contains(int x, int y) const {
    return x >= 0 && y >= 0 && x < width_ && y < height_;
}
RetailOutdoorCell &RetailOutdoorGrid::cell(int x, int y) {
    if (!contains(x, y)) throw std::out_of_range("Retail outdoor cell");
    return cells_[size_t(y) * size_t(width_) + size_t(x)];
}
const RetailOutdoorCell &RetailOutdoorGrid::cell(int x, int y) const {
    if (!contains(x, y)) throw std::out_of_range("Retail outdoor cell");
    return cells_[size_t(y) * size_t(width_) + size_t(x)];
}
void RetailOutdoorGrid::clear(int x, int y) {
    auto &value = cell(x, y);
    value.preset = 0;
    value.flags = 0;
}
void RetailOutdoorGrid::blank(int x, int y) {
    auto &value = cell(x, y);
    value.preset = 0;
    value.flags = 0x100;
}
bool RetailOutdoorGrid::canPlace(const WorldCatalog &catalog, int preset, int x, int y,
                                int clearance, uint8_t edges) const {
    if (clearance < 0) throw std::invalid_argument("Negative preset clearance");
    auto [width, height] = footprint(catalog, preset);
    if (edges & 1) { y -= clearance; height += clearance; }
    if (edges & 2) width += clearance;
    if (edges & 4) height += clearance;
    if (edges & 8) { x -= clearance; width += clearance; }
    if (!contains(x, y) || width > width_ - x || height > height_ - y) return false;
    for (int row = y; row < y + height; ++row)
        for (int column = x; column < x + width; ++column)
            if (cell(column, row).flags & 0x1B81) return false;
    return true;
}
void RetailOutdoorGrid::place(const WorldCatalog &catalog, int preset, int x, int y,
                             int pickedFile, bool markBorder) {
    if (!preset && pickedFile >= 0) { cell(x, y).preset = 0; return; }
    if (!catalog.presets().contains(preset))
        throw std::runtime_error("Native outdoor preset is absent from MPQ: " + std::to_string(preset));
    const auto &record = catalog.presets().at(preset);
    const auto [width, height] = footprint(catalog, preset);
    if (!contains(x, y) || width > width_ - x || height > height_ - y)
        throw std::out_of_range("Outdoor preset footprint exceeds the grid");
    if (pickedFile == -1) {
        if (record.files <= 0 || record.files > int(record.variants.size()))
            throw std::runtime_error("Invalid outdoor preset random file count");
        auto found = pickedFiles_.find(preset);
        if (found == pickedFiles_.end())
            found = pickedFiles_.emplace(preset, random_.below(record.files)).first;
        found->second = (found->second + 1) % record.files;
        pickedFile = found->second;
    }
    // Files limits random cycling only. Native connectors explicitly select
    // File4/File5 even when Files=3 (and town transitions use Files=0).
    if (pickedFile < 0 || pickedFile >= int(record.variants.size()))
        throw std::invalid_argument("Invalid outdoor preset file selection");
    if (record.variants[size_t(pickedFile)].empty())
        throw std::runtime_error("Selected outdoor preset file is missing from the current MPQ table");
    for (int row = y; row < y + height; ++row)
        for (int column = x; column < x + width; ++column) {
            auto &value = cell(column, row);
            value.flags = (value.flags & ~uint32_t(0xF0000)) | 0x200 |
                          (uint32_t(pickedFile) << 16);
            if (markBorder) value.flags |= 1;
            value.preset = 0;
        }
    cell(x, y).preset = preset;
}
std::vector<std::pair<int, int>> RetailOutdoorGrid::shuffledCells(bool interior) {
    std::vector<std::pair<int, int>> coordinates;
    const int edge = interior ? 1 : 0;
    if (width_ <= 2 * edge || height_ <= 2 * edge) return coordinates;
    for (int y = edge; y < height_ - edge; ++y)
        for (int x = edge; x < width_ - edge; ++x) coordinates.emplace_back(x, y);
    const int count = int(coordinates.size());
    // Native: count pairs of draws over the entire array, including self swaps.
    for (int i = 0; i < count; ++i) {
        const int a = random_.below(count);
        const int b = random_.below(count);
        std::swap(coordinates[size_t(a)], coordinates[size_t(b)]);
    }
    return coordinates;
}
bool RetailOutdoorGrid::placeRandom(const WorldCatalog &catalog, int preset, int pickedFile,
                                   int clearance, uint8_t edges) {
    for (const auto &[x, y] : shuffledInterior())
        if (canPlace(catalog, preset, x, y, clearance, edges)) {
            place(catalog, preset, x, y, pickedFile);
            return true;
        }
    return false;
}
bool RetailOutdoorGrid::placeNearMarked(const WorldCatalog &catalog, int preset, int pickedFile) {
    constexpr std::array<std::pair<int, int>, 8> offsets{{
        {-1,0}, {0,-1}, {0,1}, {1,0}, {-1,-1}, {1,1}, {1,-1}, {-1,1}}};
    const auto coordinates = shuffledInterior();
    if (coordinates.empty()) return false;
    for (const auto &[x, y] : coordinates) {
        if (!(cell(x, y).flags & 0x80)) continue;
        for (const auto &[dx, dy] : offsets)
            if (canPlace(catalog, preset, x + dx, y + dy)) {
                place(catalog, preset, x + dx, y + dy, pickedFile);
                return true;
            }
    }
    // The fallback consumes a new shuffle, not the first shuffle reused.
    return placeRandom(catalog, preset, pickedFile);
}
bool RetailOutdoorGrid::placeFarAway(const WorldCatalog &catalog, int preset, int pickedFile,
                                    int worldX, int worldY, int referenceX, int referenceY,
                                    int referenceWidth, int referenceHeight,
                                    int clearance, uint8_t edges) {
    const int width = width_ - 2, height = height_ - 2;
    if (width <= 0 || height <= 0) return false;
    const int startX = random_.below(width), startY = random_.below(height);
    const int baseX = referenceX + referenceWidth / 2;
    const int baseY = referenceY + referenceHeight / 2;
    int bestDistance = 0, bestX = -1, bestY = -1;
    // Native <= bounds deliberately visit the starting row/column twice.
    for (int row = 0; row <= height; ++row)
        for (int column = 0; column <= width; ++column) {
            const int x = (column + startX) % width + 1;
            const int y = (row + startY) % height + 1;
            if (!canPlace(catalog, preset, x, y, clearance, edges)) continue;
            const int dx = std::abs(8 * x - baseX + worldX + 4);
            const int dy = std::abs(8 * y - baseY + worldY + 4);
            const int distance = (2 * std::max(dx, dy) + std::min(dx, dy)) / 2;
            if (distance > bestDistance) {
                bestDistance = distance; bestX = x; bestY = y;
            }
        }
    if (bestX < 0) return false;
    place(catalog, preset, bestX, bestY, pickedFile);
    return true;
}
} // namespace d2x
