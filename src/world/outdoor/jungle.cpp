#include "outdoor.hpp"
#include "world/generation_seed.hpp"
#include <algorithm>
#include <array>
#include <numeric>

namespace d2x {
namespace {
struct JungleRegion {
    int x = 0, y = 0, attachment = 0, column = 0, row = 0;
    std::vector<int> children;
};
int junglePreset(int flags) {
    constexpr int branches[]{
        0,545,546,547,548,0,549,550,0,0,551,552,553,554,0,555,
        0,556,0,557,558,0,0,559,0,0,0,560,561,562,563,0,
        0,564,565,0,566,0,567,0,0,0,568,0,569,570,0,0,
        0,571,0,0,572,0,0,0,0,0,0,0};
    constexpr int clearings[]{0,575,576,577,578,579,580,0,581,582,583,0,584,0,0,0};
    const int mask = flags & 15;
    if (!mask) return flags >> 4 < 16 ? clearings[flags >> 4] : 0;
    if (flags < 16) return 529 + mask;
    int preset = 0;
    for (int direction = 0; direction < 4; ++direction)
        if (flags & (16 << direction)) preset = branches[(mask - 1) * 4 + direction];
    return preset;
}
void appendJunglePiece(const WorldCatalog &catalog, MapRecipe &recipe, int presetId,
                       int column, int row, int variant) {
    const auto &preset = catalog.presets().at(presetId);
    const auto source = catalog.preset(presetId, recipe.levelType, variant);
    recipe.pieces.push_back({column, row, preset.width, preset.height, presetId, variant,
        source.ds1, source.tileLibraries, source.fillBlanks, preset.populate});
}
}
std::map<int, MapRecipe> generateAct3Jungles(const WorldCatalog &catalog, uint32_t seed) {
    const auto &town = catalog.level(75);
    const auto &forest = catalog.level(76);
    const int width = forest.width, height = forest.height;
    if (width != 64 || height < 128 || height % 96)
        throw std::runtime_error("Unsupported original jungle block dimensions");
    Seed random(seed);
    const bool interlink = random.next() & 1;
    std::array<JungleRegion, 3> jungles;
    jungles[0].x = town.offsetX;
    jungles[0].y = town.offsetY - height;
    for (int index = 1, attempts = 0; index < 3; ++index) {
        if (++attempts > 10000) throw std::runtime_error("Original jungle placement exhausted");
        const int parent = random.below(index), direction = random.below(5);
        auto &region = jungles[index];
        region.x = jungles[parent].x + (direction == 1 || direction == 3 ? -width
                                      : direction == 2 || direction == 4 ? width : 0);
        region.y = jungles[parent].y - (direction == 0 ? height : direction <= 2 ? height / 3
                                                                                 : 2 * height / 3);
        region.attachment = direction;
        bool overlap = false;
        for (int previous = 0; previous < index; ++previous)
            if (region.x < jungles[previous].x + width && region.x + width > jungles[previous].x &&
                region.y < jungles[previous].y + height && region.y + height > jungles[previous].y)
                overlap = true;
        if (overlap) { --index; continue; }
        jungles[parent].children.push_back(index);
    }
    int minX = jungles[0].x, minY = jungles[0].y, maxX = jungles[0].x + width;
    for (const auto &region : jungles) {
        minX = std::min(minX, region.x);
        minY = std::min(minY, region.y);
        maxX = std::max(maxX, region.x + width);
    }
    const int columns = (maxX - minX) / 32 + 2;
    const int rows = (town.offsetY - minY) / 32 + 2;
    const int blockRows = height / 32;
    std::vector<int> owners(columns * rows), path(columns * rows), attach(columns * rows), flags(columns * rows);
    constexpr int directionBits[]{8,4,2,1}, oppositeBits[]{4,8,1,2};
    const std::array<int, 4> offsets{-columns,columns,1,-1};
    bool valid = false;
    for (int attempts = 0; !valid; ++attempts) {
        if (attempts > 10000) throw std::runtime_error("Original jungle attach points exhausted");
        std::fill(owners.begin(), owners.end(), 0);
        std::fill(path.begin(), path.end(), 0);
        std::fill(attach.begin(), attach.end(), 0);
        std::fill(flags.begin(), flags.end(), 0);
        for (int index = 0; index < 3; ++index) {
            auto &region = jungles[index];
            region.column = (region.x - minX) / 32 + 1;
            region.row = (region.y - minY) / 32 + 1;
            bool shifted = index ? region.attachment % 2 : random.below(2);
            int attachmentCount = 0;
            while (attachmentCount < 2) {
                for (int row = 0; row < blockRows; ++row)
                    for (int column = 0; column < 2; ++column) {
                        const int cell = (region.row + row) * columns + region.column + column;
                        owners[cell] = index + 1;
                        path[cell] = attach[cell] = flags[cell] = 0;
                    }
                if (index) {
                    const int bottom = (region.row + blockRows - 1) * columns + region.column;
                    attach[bottom + int(shifted)] = 1;
                    attach[bottom + int(!shifted)] = 2;
                }
                for (int child : region.children) {
                    const int direction = jungles[child].attachment;
                    const int column = direction == 2 || direction == 4 ? 1 : 0;
                    const int row = direction == 1 || direction == 2 ? 3
                                  : direction == 3 || direction == 4 ? 1 : 0;
                    attach[(region.row + row) * columns + region.column + column] = 1;
                }
                attachmentCount = index != 0;
                int sequence = 100 * (index + 1), run = 0;
                for (int row = region.row + blockRows - 1; row >= region.row;) {
                    const int start = row * columns + region.column;
                    path[start + int(shifted)] = sequence++;
                    if (!run || row == region.row || random.below(3)) {
                        ++run;
                        if (run >= 2 && row > 1 && !attach[start + int(!shifted)]) {
                            attach[start + int(!shifted)] = 2;
                            ++attachmentCount;
                        }
                        --row;
                    } else { run = 0; shifted = !shifted; }
                }
            }
            while (attachmentCount > 3) {
                int selected = random.below(attachmentCount);
                bool removed = false;
                for (int row = 0; row < blockRows && !removed; ++row)
                    for (int column = 0; column < 2 && !removed; ++column) {
                        const int cell = (region.row + row) * columns + region.column + column;
                        if (attach[cell] == 2 && selected-- == 0) {
                            attach[cell] = 0;
                            --attachmentCount;
                            removed = true;
                        }
                    }
            }
        }
        for (int cell = 0; cell < int(owners.size()); ++cell) {
            if (!owners[cell]) continue;
            if (attach[cell] == 1) {
                int minimum = 0x7fffffff, chosen = -1;
                for (int direction = 0; direction < 4; ++direction) {
                    const int next = cell + offsets[direction];
                    if (owners[next] == owners[cell] && path[next] && path[next] < minimum) {
                        minimum = path[next]; chosen = direction;
                    }
                }
                if (chosen >= 0) {
                    flags[cell] |= directionBits[chosen];
                    flags[cell + offsets[chosen]] |= oppositeBits[chosen];
                }
                for (int direction = 0; direction < 4; ++direction)
                    if (attach[cell + offsets[direction]] == 1 && owners[cell + offsets[direction]] != owners[cell])
                        flags[cell] |= directionBits[direction];
            }
            if (path[cell])
                for (int direction = 0; direction < 4; ++direction)
                    if (std::abs(path[cell + offsets[direction]] - path[cell]) == 1)
                        flags[cell] |= directionBits[direction];
        }
        valid = true;
        for (int cell = 0; cell < int(owners.size()); ++cell) {
            int first = random.below(4);
            if (attach[cell] != 2) continue;
            int current = flags[cell];
            const bool empty = current == 0;
            auto choose = [&](auto predicate) {
                for (int index = 0; index < 4; ++index) {
                    const int direction = (first + index) % 4;
                    if (predicate(cell + offsets[direction], directionBits[direction])) {
                        current |= directionBits[direction] << 4;
                        return true;
                    }
                }
                return false;
            };
            const bool found = choose([&](int next, int) {
                return owners[next] == owners[cell] && flags[next] > 0 && flags[next] < 15;
            });
            if (!current) { valid = false; break; }
            if (empty) {
                bool other = choose([&](int next, int) { return owners[next] != owners[cell] && attach[next] == 2; });
                const bool extra = random.below(2) && !other;
                first = random.below(4);
                if (extra) choose([&](int next, int bit) {
                    return flags[next] > 0 && flags[next] < 15 && !(current & (bit << 4));
                });
            }
            (void)found;
            for (int direction = 0; direction < 4; ++direction)
                if (current & (directionBits[direction] << 4))
                    flags[cell + offsets[direction]] |= oppositeBits[direction] << 4;
            flags[cell] |= current;
            attach[cell] = 0;
        }
    }
    std::sort(jungles.begin(), jungles.end(), [](const auto &left, const auto &right) { return left.y > right.y; });
    constexpr int clearingVariants[]{0,1,2,1,0,2,0,2,1,1,2,0,2,0,1,2,1,0};
    std::map<int, MapRecipe> result;
    for (int index = 0; index < 3; ++index) {
        const auto &region = jungles[index];
        auto &recipe = result[76 + index];
        recipe.act = 2; recipe.levelType = 21; recipe.preset = 530;
        recipe.width = width; recipe.height = height;
        recipe.worldX = region.x; recipe.worldY = region.y;
        recipe.ds1 = "jungle-v1/" + std::to_string(76 + index) + "/" + std::to_string(seed);
        int clearings = 0;
        for (int row = 0; row < blockRows; ++row)
            for (int column = 0; column < 2; ++column)
                if (junglePreset(flags[(region.row + row) * columns + region.column + column]) > 574) ++clearings;
        const int variants = random.below(clearings == 3 ? 6 : 2);
        int clearing = 0;
        for (int row = 0; row < blockRows; ++row) {
            if ((index == 0 && row == blockRows - 1) || (index == 2 && row == 0)) {
                const int endCell = (region.row + row) * columns + region.column + 1;
                appendJunglePiece(catalog, recipe, index == 0 ? 573 : 574, 0, row * 32,
                                  junglePreset(flags[endCell]) == 0 ? 1 : 0);
                continue;
            }
            for (int column = 0; column < 2; ++column) {
                int preset = junglePreset(flags[(region.row + row) * columns + region.column + column]);
                if (!preset) continue;
                int variant = -1;
                if (preset > 574) {
                    if (clearing >= 3) throw std::runtime_error("Too many original jungle clearings");
                    preset += index * 10;
                    variant = clearingVariants[clearing++ + 3 * variants];
                } else variant = random.below(catalog.presets().at(preset).files);
                appendJunglePiece(catalog, recipe, preset, column * 32, row * 32, variant);
            }
        }
    }
    const auto &last = result.at(78);
    int worldY = last.worldY;
    for (int levelId = 79; levelId <= 83; ++levelId) {
        const auto &level = catalog.level(levelId);
        worldY -= level.height;
        if (levelId >= 82) {
            auto &recipe = result[levelId];
            recipe.act = 2; recipe.levelType = level.levelType;
            recipe.width = level.width; recipe.height = level.height;
            recipe.worldX = last.worldX + width / 2 - level.width / 2;
            recipe.worldY = worldY;
            recipe.preset = levelId == 82 ? 652 : 653;
            recipe.ds1 = "kurast-fixed-v1/" + std::to_string(levelId) + "/" + std::to_string(seed);
            if (levelId == 82) appendJunglePiece(catalog, recipe, 652, 0, 0, 0);
            else {
                constexpr int columns[]{0,16,48};
                for (int index = 0; index < 6; ++index)
                    appendJunglePiece(catalog, recipe, 653 + index, columns[index % 3], index / 3 * 32, 0);
            }
            continue;
        }
        auto &recipe = result[levelId];
        recipe.act = 2; recipe.levelType = 22; recipe.preset = 615;
        recipe.width = level.width; recipe.height = level.height;
        recipe.worldX = last.worldX + width / 2 - level.width / 2;
        recipe.worldY = worldY;
        recipe.baseFloor = 0x140002;
        recipe.tileLibraries = catalog.terrainLibraries(22, 4095);
        recipe.ds1 = "kurast-v1/" + std::to_string(levelId) + "/" + std::to_string(seed);
        const int gridWidth = level.width / 8, gridHeight = level.height / 8;
        std::vector<int> occupied(gridWidth * gridHeight);
        auto place = [&](int presetId, int column, int row, int variant = -1) {
            const auto &preset = catalog.presets().at(presetId);
            const int pieceWidth = preset.width / 8, pieceHeight = preset.height / 8;
            if (column < 0 || row < 0 || column + pieceWidth > gridWidth || row + pieceHeight > gridHeight)
                return false;
            for (int vertical = row; vertical < row + pieceHeight; ++vertical)
                for (int horizontal = column; horizontal < column + pieceWidth; ++horizontal)
                    if (occupied[vertical * gridWidth + horizontal]) return false;
            if (variant < 0) variant = random.below(preset.files);
            appendJunglePiece(catalog, recipe, presetId, column * 8, row * 8, variant);
            for (int vertical = row; vertical < row + pieceHeight; ++vertical)
                for (int horizontal = column; horizontal < column + pieceWidth; ++horizontal)
                    occupied[vertical * gridWidth + horizontal] = presetId;
            return true;
        };
        const int border = levelId == 79 ? 605 : levelId == 80 ? 619 : 636;
        const int northGate = levelId == 81 ? (gridWidth - 1) / 2
                            : interlink ? (levelId == 79 ? 1 : gridWidth - 2)
                                        : (levelId == 79 ? gridWidth - 2 : 1);
        const int southGate = levelId == 79 ? (gridWidth - 1) / 2
                    : levelId == 81 ? (interlink ? gridWidth - 2 : 1)
                            : (interlink ? 1 : gridWidth - 2);
        for (int column = 1; column < gridWidth - 1;) {
            const int presetId = column == northGate ? border + 8 : border;
            if (!place(presetId, column, 0)) throw std::runtime_error("Original Kurast north border overlap");
            column += catalog.presets().at(presetId).width / 8;
        }
        for (int column = 1; column < gridWidth - 1;) {
            const int presetId = column == southGate ? border + 9 : border + 1;
            if (!place(presetId, column, gridHeight - 1))
                throw std::runtime_error("Original Kurast south border overlap");
            column += catalog.presets().at(presetId).width / 8;
        }
        for (int row = 1; row < gridHeight - 1; ++row) {
            place(border + 2, gridWidth - 1, row);
            place(border + 3, 0, row);
        }
        place(border + 5, 0, 0); place(border + 4, gridWidth - 1, 0);
        place(border + 7, 0, gridHeight - 1); place(border + 6, gridWidth - 1, gridHeight - 1);
        if (levelId != 79) {
            const int sewer = levelId == 80 ? 629 : 646;
            const int row = levelId == 80 ? 3 : gridHeight - 4;
            if (!place(sewer, 3, row, 0) || !place(sewer, gridWidth - 4, row, 1))
                throw std::runtime_error("Original Kurast sewer overlap");
        }
        auto randomPreset = [&](int first, int lastPreset, int maximum, int variant = -1, bool required = true) {
            std::vector<int> cells(gridWidth * gridHeight);
            std::iota(cells.begin(), cells.end(), 0);
            for (int cell = 0; cell < int(cells.size()); ++cell)
                std::swap(cells[random.below(int(cells.size()))], cells[random.below(int(cells.size()))]);
            int placed = 0;
            for (int cell : cells)
                if (place(first + random.below(lastPreset - first + 1), cell % gridWidth, cell / gridWidth, variant))
                    if (maximum > 0 && ++placed >= maximum) break;
            if (required && maximum > 0 && placed < maximum)
                throw std::runtime_error("Missing space for original Kurast special preset");
        };
        if (levelId != 79) {
            const int temple = levelId == 80 ? 630 : 647;
            randomPreset(temple, temple, 1, 0);
            randomPreset(temple, temple, 1, 1);
        }
        randomPreset(631, 631, 1, 0);
        const int small = levelId == 79 ? 615 : levelId == 80 ? 632 : 648;
        randomPreset(small + 3, small + 3, 4, -1, false);
        randomPreset(small + 1, small + 2, 0);
        randomPreset(small, small, 0);
    }
    auto &dock = result[75];
    dock = catalog.preset(529, town.levelType, 0);
    dock.act = 2; dock.width = town.width; dock.height = town.height;
    dock.worldX = town.offsetX; dock.worldY = town.offsetY;
    for (auto first = result.begin(); first != result.end(); ++first)
        for (auto second = std::next(first); second != result.end(); ++second) {
            auto &left = first->second;
            auto &right = second->second;
            int side = -1;
            if (left.worldX + left.width == right.worldX) side = 3;
            else if (right.worldX + right.width == left.worldX) side = 1;
            else if (left.worldY + left.height == right.worldY) side = 0;
            else if (right.worldY + right.height == left.worldY) side = 2;
            if (side < 0) continue;
            const bool vertical = side % 2;
            const int start = std::max(vertical ? left.worldY : left.worldX, vertical ? right.worldY : right.worldX);
            const int end = std::min(vertical ? left.worldY + left.height : left.worldX + left.width,
                                     vertical ? right.worldY + right.height : right.worldX + right.width);
            if (start >= end) continue;
            const int leftOrigin = vertical ? left.worldY : left.worldX;
            const int rightOrigin = vertical ? right.worldY : right.worldX;
            left.boundaries.push_back({second->first, side, start - leftOrigin, end - leftOrigin,
                start - leftOrigin, end - leftOrigin});
            right.boundaries.push_back({first->first, (side + 2) % 4, start - rightOrigin, end - rightOrigin,
                start - rightOrigin, end - rightOrigin});
        }
    return result;
}
}