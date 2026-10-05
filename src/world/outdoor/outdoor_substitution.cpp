#include "outdoor_substitution.hpp"
#include "world/map_assembly.hpp"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <set>
#include <stdexcept>

namespace d2x {
void applyOutdoorThemes(Archives &archives, const WorldCatalog &catalog, const LevelRecord &level,
                        MapRecipe &recipe, Seed &seed) {
    if (!recipe.baseFloor || level.subtype < 0 || level.theme < 0) return;
    if (level.theme >= 5) throw std::runtime_error("Unsupported Levels.SubTheme");
    struct Pattern { const SubstitutionRecord *record; MapData data; };
    std::vector<Pattern> patterns;
    for (const auto &record : catalog.substitutions()) {
        if (record.type != level.subtype) continue;
        // Macro border replacement is a different stage, not a room decoration.
        if (record.gridSize != 1) continue;
        auto data = decodeDs1(archives.read(record.file), record.file);
        if (data.substitutionMethod != 1 && data.substitutionMethod != 2)
            throw std::runtime_error("Unsupported room substitution method: " + record.file);
        if (data.zeroFilledSubstitutionGroups) {
            static std::set<std::string> reported;
            if (reported.insert(record.file).second)
                std::cerr << "MPQ substitution " << record.file << ": retained "
                          << data.substitutionGroups.size() << " declared groups; zero-filled "
                          << data.zeroFilledSubstitutionGroups << " incomplete trailing group\n";
        }
        patterns.push_back({&record, std::move(data)});
    }
    if (patterns.empty()) return;
    auto terrain = assembleMap(archives, recipe);
    const auto authoredPieces = recipe.pieces.size();
    for (int roomY = 0; roomY < recipe.height; roomY += 8)
        for (int roomX = 0; roomX < recipe.width; roomX += 8) {
            const int roomWidth = std::min(8, recipe.width - roomX);
            const int roomHeight = std::min(8, recipe.height - roomY);
            bool authored = false;
            for (size_t i = 0; i < authoredPieces; ++i) {
                const auto &piece = recipe.pieces[i];
                authored |= piece.substitutionGroup < 0 && piece.x < roomX + roomWidth &&
                    piece.y < roomY + roomHeight && roomX < piece.x + piece.width && roomY < piece.y + piece.height;
            }
            if (authored) continue;
            std::vector<bool> selected;
            for (const auto &pattern : patterns)
                selected.push_back(seed.below(100) < pattern.record->probability[level.theme]);
            for (size_t patternIndex = 0; patternIndex < patterns.size(); ++patternIndex) {
                if (!selected[patternIndex]) continue;
                const auto &[record, pattern] = patterns[patternIndex];
                auto matches = [&](const auto &group, int x, int y, bool random) {
                    for (int row = 0; row < group.height; ++row)
                        for (int column = 0; column < group.width; ++column) {
                            const size_t source = size_t(group.y + row) * pattern.width + group.x + column;
                            const size_t target = size_t(roomY + y + row) * terrain.width + roomX + x + column;
                            const MapCell floor = pattern.floors.empty() ? MapCell{} : pattern.floors.front()[source];
                            const MapCell wall = pattern.walls.empty() ? MapCell{} : pattern.walls.front()[source];
                            const auto &existingFloor = terrain.floors.front()[target];
                            // Biome translation follows substitution in native DRLGOUTPLACE.
                            const uint32_t floorValue = existingFloor.libraryScope == 0 && existingFloor.value == recipe.baseFloor
                                ? 0x40002u : existingFloor.value;
                            if (random) {
                                if ((floor.value & 2) && (!(floorValue & 2) || ((floor.value ^ floorValue) & 0x3F0FF00))) return false;
                                if (wall.value & 1) {
                                    if (terrain.walls.empty()) return false;
                                    const auto &existing = terrain.walls.front()[target];
                                    if (!(existing.value & 1) || ((existing.value ^ wall.value) & 0x3F0FF00) ||
                                        existing.orientation != wall.orientation) return false;
                                }
                            } else if ((floor.value & 2) || (wall.value & 1)) {
                                if (!(floorValue & 2) || (floorValue & 0x3F0FF00)) return false;
                                for (const auto &layer : terrain.walls) if (layer[target].value & 1) return false;
                            }
                        }
                    return true;
                };
                auto stamp = [&](size_t groupIndex, int x, int y, int variant) {
                    const auto &group = pattern.substitutionGroups[groupIndex];
                    // Position draws and fitting precede this successful no-op.
                    if (!group.width || !group.height) return;
                    MapPiece piece;
                    piece.x = roomX + x; piece.y = roomY + y;
                    piece.width = group.width; piece.height = group.height;
                    piece.ds1 = record->file;
                    piece.tileLibraries = recipe.tileLibraries;
                    const auto libraries = catalog.terrainLibraries(level.levelType, record->dt1Mask);
                    piece.tileLibraries.insert(piece.tileLibraries.end(), libraries.begin(), libraries.end());
                    piece.populate = false;
                    piece.substitutionGroup = int(groupIndex);
                    piece.substitutionVariant = variant;
                    recipe.pieces.push_back(std::move(piece));
                    auto merge = [&](const auto &from, auto &to, bool floor) {
                        while (to.size() < from.size()) to.emplace_back(size_t(terrain.width) * terrain.height);
                        for (size_t layer = 0; layer < from.size(); ++layer)
                            for (int row = 0; row < group.height; ++row)
                                for (int column = 0; column < group.width; ++column) {
                                    auto cell = from[layer].at(size_t(group.y + row) * pattern.width + group.x +
                                        variant * (group.width + 1) + column);
                                    if (!cell.occupied()) continue;
                                    if (floor) cell.value |= 0x80;
                                    cell.libraryScope = recipe.pieces.size();
                                    to[layer][size_t(roomY + y + row) * terrain.width + roomX + x + column] = cell;
                                }
                    };
                    merge(pattern.floors, terrain.floors, true);
                    merge(pattern.walls, terrain.walls, false);
                };
                if (record->checkAll) {
                    for (size_t i = 0; i < pattern.substitutionGroups.size(); ++i) {
                        const auto &group = pattern.substitutionGroups[i];
                        const bool random = pattern.substitutionMethod == 2;
                        for (int y = random ? 0 : 1; y <= roomHeight - group.height; ++y)
                            for (int x = random ? 0 : 1; x <= roomWidth - group.width; ++x)
                                if (matches(group, x, y, random) && (!random || record->probability[level.theme] < seed.below(100)))
                                    stamp(i, x, y, random ? 1 + seed.below(group.variants) : 0);
                    }
                } else if (!pattern.substitutionGroups.empty()) {
                    for (int count = 0; count < record->maximum[level.theme]; ++count) {
                        const size_t i = size_t(seed.below(int(pattern.substitutionGroups.size())));
                        const auto &group = pattern.substitutionGroups[i];
                        const int columns = roomWidth - group.width, rows = roomHeight - group.height;
                        if (columns <= 0 || rows <= 0) continue;
                        std::vector<int> positions;
                        if (record->trials[level.theme] == -1) {
                            positions.resize(size_t(columns) * rows);
                            std::iota(positions.begin(), positions.end(), 0);
                            for (size_t j = 0; j < positions.size(); ++j) {
                                const int left = seed.below(int(positions.size())), right = seed.below(int(positions.size()));
                                std::swap(positions[left], positions[right]);
                            }
                        } else {
                            for (int attempt = 0; attempt < record->trials[level.theme]; ++attempt) {
                                const int x = 1 + seed.below(columns), y = 1 + seed.below(rows);
                                if (matches(group, x, y, false)) { stamp(i, x, y, 0); break; }
                            }
                        }
                        for (int position : positions) {
                            const int x = 1 + position % columns, y = 1 + position / columns;
                            if (matches(group, x, y, false)) { stamp(i, x, y, 0); break; }
                        }
                    }
                }
            }
        }
}
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
