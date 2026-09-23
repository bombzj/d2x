#include "map_assembly.hpp"
#include <algorithm>

namespace d2x {
MapData assembleMap(Archives &archives, const MapRecipe &recipe) {
    MapData result;
    if (recipe.pieces.empty())
        return decodeDs1(archives.read(recipe.ds1));
    result.width = recipe.width ? recipe.width + 1 : 0;
    result.height = recipe.height ? recipe.height + 1 : 0;
    for (const auto &piece : recipe.pieces) {
        if (piece.x < 0 || piece.y < 0 || piece.width < 1 || piece.height < 1)
            throw std::runtime_error("Invalid room placement");
        result.width = std::max(result.width, piece.x + piece.width + 1);
        result.height = std::max(result.height, piece.y + piece.height + 1);
    }
    if (result.width > 2048 || result.height > 2048)
        throw std::runtime_error("Generated map exceeds the supported terrain bounds");
    size_t cells = size_t(result.width) * result.height;
    result.shadows.resize(cells);
    result.substitutions.resize(cells);
    if (recipe.baseFloor)
        result.floors.emplace_back(cells, MapCell{recipe.baseFloor, 0});
    for (const auto &blank : recipe.blankAreas) {
        if (!recipe.baseFloor || blank.x < 0 || blank.y < 0 || blank.width < 1 || blank.height < 1 ||
            int64_t(blank.x) + blank.width > recipe.width || int64_t(blank.y) + blank.height > recipe.height)
            throw std::runtime_error("Invalid outdoor blank area");
        for (int row = blank.y; row < blank.y + blank.height; ++row)
            for (int column = blank.x; column < blank.x + blank.width; ++column)
                result.floors.front()[size_t(row) * result.width + column] = {};
    }
    for (const auto &floor : recipe.floorOverrides) {
        if (!recipe.baseFloor || floor.x < 0 || floor.y < 0 || floor.x >= recipe.width ||
            floor.y >= recipe.height)
            throw std::runtime_error("Invalid outdoor floor override");
        result.floors.front()[size_t(floor.y) * result.width + floor.x] = {floor.value, 0};
    }
    for (size_t pieceIndex = 0; pieceIndex < recipe.pieces.size(); ++pieceIndex) {
        const auto &piece = recipe.pieces[pieceIndex];
        auto source = decodeDs1(archives.read(piece.ds1));
        if (piece.substitutionGroup >= 0) {
            const auto group = source.substitutionGroups.at(size_t(piece.substitutionGroup));
            if (group.width != piece.width || group.height != piece.height || group.variants != 0)
                throw std::runtime_error("Invalid fixed substitution piece: " + piece.ds1);
            auto crop = [&](auto &layer) {
                using Cell = typename std::decay_t<decltype(layer)>::value_type;
                std::vector<Cell> output(size_t(piece.width + 1) * (piece.height + 1));
                for (int row = 0; row < group.height; ++row)
                    for (int column = 0; column < group.width; ++column)
                        output[size_t(row) * (piece.width + 1) + column] =
                            layer.at(size_t(group.y + row) * source.width + group.x + column);
                layer = std::move(output);
            };
            for (auto &layer : source.floors) crop(layer);
            for (auto &layer : source.walls) crop(layer);
            crop(source.shadows);
            if (!source.substitutions.empty()) crop(source.substitutions);
            std::erase_if(source.objects, [&](const auto &object) {
                return object.x <= group.x * 5 || object.y <= group.y * 5 ||
                       object.x >= (group.x + group.width) * 5 || object.y >= (group.y + group.height) * 5;
            });
            for (auto &object : source.objects) {
                object.x -= group.x * 5;
                object.y -= group.y * 5;
            }
            source.width = piece.width + 1;
            source.height = piece.height + 1;
        }
        if (source.width != piece.width + 1 || source.height != piece.height + 1)
            throw std::runtime_error("DS1 room dimensions disagree with LvlMaze: " + piece.ds1);
        if (!result.version) {
            result.version = source.version;
            result.act = source.act;
        }
        if (result.act != source.act || (result.version > 4) != (source.version > 4))
            throw std::runtime_error("Incompatible DS1 unit schema within a generated level");
        result.version = std::max(result.version, source.version);
        if (piece.fillBlanks)
            for (auto &cell : source.floors.front())
                if (!cell.occupied())
                    cell.value = (30u << 20) | 0x80000002u;
        auto merge = [&](const auto &from, auto &to) {
            for (int y = 0; y < source.height; ++y)
                for (int x = 0; x < source.width; ++x) {
                    const auto &cell = from[y * source.width + x];
                    auto &dest = to[(piece.y + y) * result.width + piece.x + x];
                    // DS1's extra row/column is a shared room border, not an extra room tile.
                    // Empty cells do not erase the neighbouring room's authored wall/floor.
                    if (cell.occupied() && (!cell.hidden() || !dest.occupied())) {
                        dest = cell;
                        dest.libraryScope = pieceIndex + 1;
                    }
                }
        };
        auto layers = [&](const auto &from, auto &to) {
            while (to.size() < from.size())
                to.emplace_back(cells);
            for (size_t i = 0; i < from.size(); ++i)
                merge(from[i], to[i]);
        };
        layers(source.floors, result.floors);
        layers(source.walls, result.walls);
        merge(source.shadows, result.shadows);
        for (int y = 0; y < source.height; ++y)
            for (int x = 0; x < source.width; ++x)
                if (!source.substitutions.empty())
                    result.substitutions[(piece.y + y) * result.width + piece.x + x] =
                        source.substitutions[y * source.width + x];
        for (auto object : source.objects) {
            object.x += piece.x * 5;
            object.y += piece.y * 5;
            result.objects.push_back(object);
        }
    }
    return result;
}
} // namespace d2x
