#include "outdoor.hpp"
#include "world/generation_seed.hpp"
#include "resources/formats.hpp"
#include <algorithm>
#include <numeric>

namespace d2x {
std::map<int, MapRecipe> generateAct5Barricades(Archives &archives, const WorldCatalog &catalog, uint32_t seed) {
    Seed random(seed);
    std::map<int, MapRecipe> result;
    const auto &siege = catalog.level(110);
    const int firstOrientation = random.next() & 1;
    const int secondOrientation = random.next() & 1;
    const int snowOrientation = random.next() & 1;
    auto initialize = [&](int id, int orientation, int column, int row) {
        MapRecipe recipe;
        recipe.act = 4; recipe.levelType = 31; recipe.preset = id == 117 ? 957 : 881;
        recipe.width = orientation ? 160 : 64;
        recipe.height = orientation ? 64 : 160;
        recipe.worldX = column; recipe.worldY = row;
        recipe.baseFloor = id == 117 ? 0x640002 : 0x40002;
        recipe.tileLibraries = catalog.terrainLibraries(31, catalog.presets().at(recipe.preset).dt1Mask);
        recipe.ds1 = "barricade-v1/" + std::to_string(id) + "/" + std::to_string(seed);
        result.emplace(id, std::move(recipe));
    };
    const int firstWidth = firstOrientation ? 160 : 64, firstHeight = firstOrientation ? 64 : 160;
    initialize(111, firstOrientation, siege.offsetX - firstWidth,
        siege.offsetY + siege.height - firstHeight - 16);
    constexpr int offsetsX[]{0,-96,-64,-160}, offsetsY[]{-160,-64,-96,0};
    const int offset = secondOrientation + 2 * firstOrientation;
    initialize(112, secondOrientation, result.at(111).worldX + offsetsX[offset],
        result.at(111).worldY + offsetsY[offset]);
    initialize(117, snowOrientation, catalog.level(117).offsetX, catalog.level(117).offsetY);
    auto link = [&](MapRecipe &parent, int parentId, MapRecipe &child, int childId) {
        int side = child.worldX + child.width == parent.worldX ? 1
                 : child.worldY + child.height == parent.worldY ? 2
                 : parent.worldX + parent.width == child.worldX ? 3 : 0;
        const bool vertical = side % 2;
        const int start = std::max(vertical ? parent.worldY : parent.worldX,
                                   vertical ? child.worldY : child.worldX);
        const int end = std::min(vertical ? parent.worldY + parent.height : parent.worldX + parent.width,
                                 vertical ? child.worldY + child.height : child.worldX + child.width);
        if (end <= start) throw std::runtime_error("Original barricade levels do not share a boundary");
        const int parentOffset = vertical ? parent.worldY : parent.worldX;
        const int childOffset = vertical ? child.worldY : child.worldX;
        const int opening = end - 32;
        parent.boundaries.push_back({childId,side,opening-parentOffset,end-parentOffset,
            start-parentOffset,end-parentOffset});
        child.boundaries.push_back({parentId,(side+2)%4,opening-childOffset,end-childOffset,
            start-childOffset,end-childOffset});
    };
    link(result.at(111),111,result.at(112),112);
    for (auto &[id, recipe] : result) {
        const bool snow = id == 117;
        const int columns = recipe.width / 16, rows = recipe.height / 16;
        std::vector<int> occupied(columns * rows);
        auto place = [&](int presetId, int column, int row, int variant = -1, bool replace = false) {
            const auto &preset = catalog.presets().at(presetId);
            const int width = preset.width / 16, height = preset.height / 16;
            if (column < 0 || row < 0 || column + width > columns || row + height > rows) return false;
            for (int vertical = row; vertical < row + height; ++vertical)
                for (int horizontal = column; horizontal < column + width; ++horizontal)
                    if (occupied[vertical * columns + horizontal] && !replace) return false;
            if (replace)
                std::erase_if(recipe.pieces, [&](const auto &piece) {
                    return piece.x < (column + width) * 16 && piece.x + piece.width > column * 16 &&
                           piece.y < (row + height) * 16 && piece.y + piece.height > row * 16;
                });
            if (variant < 0) {
                std::vector<int> variants;
                for (int index = 0; index < std::max(1, preset.files); ++index)
                    if (!preset.variants[size_t(index)].empty()) variants.push_back(index);
                if (variants.empty()) throw std::runtime_error("Original barricade preset has no DS1: " + std::to_string(presetId));
                variant = variants[size_t(random.below(int(variants.size())))];
            }
            const auto source = catalog.preset(presetId, 31, variant);
            recipe.pieces.push_back({column*16,row*16,preset.width,preset.height,presetId,variant,
                source.ds1,source.tileLibraries,source.fillBlanks,preset.populate});
            for (int vertical = row; vertical < row + height; ++vertical)
                for (int horizontal = column; horizontal < column + width; ++horizontal)
                    occupied[vertical * columns + horizontal] = presetId;
            return true;
        };
        const int cliff = snow ? 957 : 881, ravine = snow ? 969 : 893;
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < columns; ++column) {
                int preset = 0;
                if (row == 0) preset = cliff + 2;
                else if (column == 0) preset = cliff + 1;
                else if (row == rows - 1) preset = ravine;
                else if (column == columns - 1) preset = ravine + 3;
                if (!column && !row) preset = cliff + 5;
                if (column == columns - 1 && !row) preset = snow ? 982 : 906;
                if (!column && row == rows - 1) preset = snow ? 981 : 905;
                if (column == columns - 1 && row == rows - 1) preset = ravine + 7;
                if (preset && !place(preset,column,row)) throw std::runtime_error("Original barricade border overlap");
            }
        for (const auto &boundary : recipe.boundaries) {
            const bool vertical = boundary.side % 2;
            const int column = vertical ? (boundary.side == 1 ? 0 : columns - 1) : boundary.start / 16;
            const int row = vertical ? boundary.start / 16 : (boundary.side == 2 ? 0 : rows - 1);
            const int preset = boundary.side == 2 ? 909 : boundary.side == 0 ? 908
                             : boundary.side == 1 ? 910 : 907;
            if (!place(preset,column,row,-1,true)) throw std::runtime_error("Original barricade gate does not fit");
        }
        if (id == 111) {
            if (!place(880,columns-1,rows-2,0,true) || !place(896,columns-1,rows-3,-1,true))
                throw std::runtime_error("Original siege transition does not fit");
            const int start = recipe.height - 32;
            recipe.boundaries.push_back({110,3,start,recipe.height,start,recipe.height});
        }
        if (id == 112 || snow) {
            const bool vertical = recipe.width <= recipe.height;
            const int toCave = snow ? (vertical ? 985 : 986) : (vertical ? 913 : 914);
            if (!place(toCave,vertical ? 1 : 0,vertical ? 0 : 1,0,true))
                throw std::runtime_error("Original barricade cave does not fit");
            if (snow && !place(vertical ? 983 : 984,vertical ? 1 : columns-1,
                                vertical ? rows-1 : 1,0,true))
                throw std::runtime_error("Original tundra cave return does not fit");
        }
        auto rulePreset = [&](uint32_t value) {
            const int style = (value >> 20) & 63, sequence = (value >> 8) & 255;
            if (!(value & 1)) return 0;
            if (style == 49 && ((sequence >= 1 && sequence <= 16) ||
                               (sequence >= 31 && sequence <= 46)))
                return (snow ? 987 : 915) + sequence - (sequence >= 31 ? 31 : 1);
            if (style == 48) {
                if (sequence == 1) return cliff + 2;
                if (sequence >= 2 && sequence <= 3) return cliff + sequence - 2;
                if (sequence == 4) return cliff + 3;
                if (sequence == 5) return ravine + 2;
                if (sequence >= 6 && sequence <= 7) return ravine + sequence - 6;
                if (sequence == 8) return ravine + 3;
                if (sequence == 30) return 0;
                if (sequence == 31) return -5;
            }
            throw std::runtime_error("Unsupported original barricade substitution code");
        };
        for (const auto &record : catalog.substitutions()) {
            if (record.type != 12) continue;
            const auto pattern = decodeDs1(archives.read(record.file));
            if (record.gridSize != 2 || pattern.walls.empty())
                throw std::runtime_error("Unsupported original barricade substitution grid");
            for (const auto &group : pattern.substitutionGroups) {
                const int choicesWidth = columns - group.width + 1;
                const int choicesHeight = rows - group.height + 1;
                if (choicesWidth <= 0 || choicesHeight <= 0) continue;
                std::vector<int> choices(choicesWidth * choicesHeight);
                std::iota(choices.begin(), choices.end(), 0);
                for (int index = 0; index < int(choices.size()); ++index)
                    std::swap(choices[random.below(int(choices.size()))], choices[random.below(int(choices.size()))]);
                for (int choice : choices) {
                    const int originX = choice % choicesWidth, originY = choice / choicesWidth;
                    bool matches = true;
                    for (int row = 0; row < group.height && matches; ++row)
                        for (int column = 0; column < group.width; ++column) {
                            const auto value = pattern.walls.front().at(size_t(group.y + row) * pattern.width +
                                group.x + column).value;
                            const int preset = rulePreset(value);
                            if (preset != -5 && occupied[(originY + row) * columns + originX + column] != preset)
                                matches = false;
                        }
                    if (!matches) continue;
                    const int variant = random.below(group.variants) + 1;
                    for (int row = 0; row < group.height; ++row)
                        for (int column = 0; column < group.width; ++column) {
                            const auto value = pattern.walls.front().at(size_t(group.y + row) * pattern.width +
                                group.x + variant * (group.width + 1) + column).value;
                            const int preset = rulePreset(value);
                            if (preset > 0 && !place(preset,originX+column,originY+row,-1,true))
                                throw std::runtime_error("Original barricade substitution does not fit");
                        }
                }
            }
        }
        auto randomPreset = [&](int presetId, int attempts, bool required, int variant = -1) {
            std::vector<int> cells((columns-2)*(rows-2));
            std::iota(cells.begin(),cells.end(),0);
            for (int index = 0; index < int(cells.size()); ++index)
                std::swap(cells[random.below(int(cells.size()))],cells[random.below(int(cells.size()))]);
            int added = 0;
            for (int cell : cells) {
                const auto &preset = catalog.presets().at(presetId);
                const int column = 1+cell%(columns-2), row = 1+cell/(columns-2);
                if (preset.width < 16 || preset.height < 16) {
                    if (occupied[row*columns+column]) continue;
                    const auto source = catalog.preset(presetId,31,variant >= 0 ? variant : random.below(std::max(1, preset.files)));
                    recipe.pieces.push_back({column*16,row*16,preset.width,preset.height,presetId,
                        variant >= 0 ? variant : source.variant,source.ds1,source.tileLibraries,
                        source.fillBlanks,preset.populate});
                    occupied[row*columns+column] = presetId;
                } else if (!place(presetId,column,row,variant)) continue;
                if (++added >= attempts) break;
            }
            if (!added && required) throw std::runtime_error("Missing original barricade special preset");
        };
        randomPreset(recipe.width < recipe.height ? 955 : 956,1,true,snow ? 1 : 0);
        if (id != 111) randomPreset(snow ? 954 : 953,1,true);
        if (id == 111) {
            for (int prison = 0; prison < 3; ++prison) randomPreset(931+random.below(8),1,true);
            for (int preset : {944,942,943,945,946}) randomPreset(preset,1,false);
        } else if (snow) {
            for (int preset : {948,949,950,951}) randomPreset(preset,1,false);
        } else {
            for (int preset : {939,940,941}) randomPreset(preset,1,false);
        }
    }
    return result;
}
}