#include "outdoor.hpp"
#include "world/generation_seed.hpp"
#include <algorithm>
#include <numeric>

namespace d2x {
std::map<int, MapRecipe> generateAct4Outdoors(const WorldCatalog &catalog, uint32_t seed) {
    const auto positions = layoutAct4(catalog, seed);
    std::map<int, MapRecipe> result;
    for (const auto &[id, position] : positions) {
        if (id == 103) {
            auto recipe = catalog.preset(797, catalog.level(id).levelType);
            recipe.act = 3;
            recipe.width = position.width; recipe.height = position.height;
            recipe.worldX = position.x; recipe.worldY = position.y;
            recipe.boundaries = position.boundaries;
            result.emplace(id, std::move(recipe));
            continue;
        }
        Seed world(seed);
        Seed random(world.next() + uint32_t(id));
        MapRecipe recipe;
        recipe.act = 3; recipe.levelType = 27; recipe.preset = 799;
        recipe.width = position.width; recipe.height = position.height;
        recipe.worldX = position.x; recipe.worldY = position.y;
        recipe.boundaries = position.boundaries;
        recipe.baseFloor = 0xa40002;
        recipe.tileLibraries = catalog.terrainLibraries(27, catalog.presets().at(799).dt1Mask);
        recipe.ds1 = "mesa-v1/" + std::to_string(id) + "/" + std::to_string(seed);
        const int columns = position.width / 8, rows = position.height / 8;
        std::vector<int> occupied(columns * rows);
        auto place = [&](int presetId, int column, int row, int variant = -1) {
            const auto &preset = catalog.presets().at(presetId);
            const int width = preset.width / 8, height = preset.height / 8;
            if (column < 0 || row < 0 || column + width > columns || row + height > rows) return false;
            for (int vertical = row; vertical < row + height; ++vertical)
                for (int horizontal = column; horizontal < column + width; ++horizontal)
                    if (occupied[vertical * columns + horizontal]) return false;
            if (variant < 0) variant = random.below(preset.files);
            const auto source = catalog.preset(presetId, 27, variant);
            recipe.pieces.push_back({column * 8, row * 8, preset.width, preset.height, presetId, variant,
                source.ds1, source.tileLibraries, source.fillBlanks, preset.populate, -1, source.killEdge, source.animationSpeed, 0, source.pops, source.popPad});
            for (int vertical = row; vertical < row + height; ++vertical)
                for (int horizontal = column; horizontal < column + width; ++horizontal)
                    occupied[vertical * columns + horizontal] = presetId;
            return true;
        };
        if (id == 104 && !place(798, 0, position.flags & 0x400000 ? 1 : 4, 0))
            throw std::runtime_error("Original fortress transition does not fit");
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < columns; ++column) {
                if (occupied[row * columns + column]) continue;
                const int side = row == 0 ? 2 : row == rows - 1 ? 0
                               : column == 0 ? 1 : column == columns - 1 ? 3 : -1;
                if (side < 0) continue;
                const bool passage = std::any_of(position.boundaries.begin(), position.boundaries.end(),
                    [&](const auto &boundary) {
                        const int lateral = (side % 2 ? row : column) * 8;
                        return side == boundary.side && lateral < boundary.end && lateral + 8 > boundary.start;
                    });
                if (passage) { occupied[row * columns + column] = -1; continue; }
                constexpr int borders[]{799,800,801,802};
                int preset = borders[side];
                if (!column && !row) preset = 804;
                else if (column == columns - 1 && !row) preset = 805;
                else if (column == columns - 1 && row == rows - 1) preset = 806;
                else if (!column && row == rows - 1) preset = 803;
                if (!place(preset, column, row)) throw std::runtime_error("Original mesa border overlaps");
            }
        auto randomPreset = [&](int presetId, bool required = true) {
            std::vector<int> cells((columns - 2) * (rows - 2));
            std::iota(cells.begin(), cells.end(), 0);
            for (int index = 0; index < int(cells.size()); ++index)
                std::swap(cells[random.below(int(cells.size()))], cells[random.below(int(cells.size()))]);
            for (int cell : cells)
                if (place(presetId, cell % (columns - 2) + 1, cell / (columns - 2) + 1)) return;
            if (required) throw std::runtime_error("Missing space for original mesa preset " + std::to_string(presetId));
        };
        if (id == 106) randomPreset(811);
        const int mesa = id == 104 ? 812 : id == 105 ? 817 : 823;
        const int pit = id == 104 ? 828 : 832;
        if (id == 105) randomPreset(822);
        for (int offset : {0,1,1,2,2,3,3,4,4,4,4}) randomPreset(mesa + offset, false);
        for (int offset : {0,1,1,2,2,3,3,3,3}) randomPreset(pit + offset, false);
        result.emplace(id, std::move(recipe));
    }
    return result;
}
}