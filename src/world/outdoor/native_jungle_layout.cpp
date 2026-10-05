#include "native_jungle_layout.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
// D2MOO DrlgOutPlace::GenerateJungles: shared original act placement and
// attachment graph. MPQ defines dimensions; no room files are selected here.
// MIT: docs/licenses/D2MOO.txt.
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
}
void placeNativeJungleAct(const WorldCatalog &catalog, NativeActLayout &result, Seed &random) {
    const auto &town = catalog.level(75);
    const auto &forest = catalog.level(76);
    const int width = forest.width, height = forest.height;
    if (width != 64 || height < 128 || height % 96)
        throw std::runtime_error("Unsupported original jungle block dimensions");
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
    for (int index = 0; index < 3; ++index) {
        const auto &region = jungles[index];
        const int id = 76 + index;
        result.levels[id] = {id,region.x,region.y,width,height};
        auto &data = result.jungles[id];
        for (int row = 0; row < blockRows; ++row) for (int column = 0; column < width / 32; ++column) {
            const int preset = junglePreset(flags[(region.row + row) * columns + region.column + column]);
            data.presets.push_back(preset);
            if (preset > 574) ++data.clearings;
        }
    }
    result.levels[75] = {75,town.offsetX,town.offsetY,town.width,town.height};
    const auto &last = result.levels.at(78);
    int worldY = last.y;
    for (int id = 79; id <= 83; ++id) {
        const auto &level = catalog.level(id);
        worldY -= level.height;
        result.levels[id] = {id,last.x + last.width / 2 - level.width / 2,worldY,level.width,level.height};
    }
}
} // namespace d2x
