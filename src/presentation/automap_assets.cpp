#include "scene_assets.hpp"
#include <set>
#include <stdexcept>

namespace d2x {
void SceneAssets::loadAutomap(const GameSession &session) {
    std::set<int> used;
    for (const auto &region : session.regions()) {
        const auto &map = region.map;
        auto &stamps = regionAutomap.emplace_back();
        for (int y = 0; y < map.data.height; ++y)
            for (int x = 0; x < map.data.width; ++x) {
                const size_t cellIndex = size_t(y) * map.data.width + x;
                auto collect = [&](const auto &layers) {
                    for (const auto &layer : layers) {
                        const auto &cell = layer[cellIndex];
                        if (!cell.present()) continue;
                        int index = map.tileIndex(cell, x, y);
                        if (index < 0) continue;
                        int cel = automapCatalog_.tileCel(region.recipe.levelType,
                                                          *map.tiles[index], x, y);
                        if (cel < 0) continue;
                        stamps.push_back({x, y, cel});
                        used.insert(cel);
                    }
                };
                collect(map.data.floors);
                collect(map.data.walls);
            }
        for (const auto &object : region.objects) {
            if (!object.npcClass.empty()) continue;
            if (int cel = automapCatalog_.objectCel(object.objectClass); cel >= 0)
                used.insert(cel);
        }
    }
    for (int objectClass : {59, 60})
        if (int cel = automapCatalog_.objectCel(objectClass); cel >= 0)
            used.insert(cel);
    constexpr const char *paths[] = {
        "data/global/ui/automap/maximaps.dc6",
        "data/global/ui/automap/maximap.dc6"};
    for (int size = 0; size < 2; ++size) {
        const auto *art = graphics_.animation(paths[size]);
        if (!art || art->frames.empty())
            throw std::runtime_error("Original MPQ automap art is missing");
        for (int cel : used)
            if (cel < int(art->frames.size()))
                automapCels[size].emplace(cel, graphics_.upload(art->frames[cel]));
    }
}
} // namespace d2x
