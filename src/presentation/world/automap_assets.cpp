#include "gameplay/session/session.hpp"
#include "presentation/scene_assets.hpp"
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
                std::set<int> cellCels;
                auto collect = [&](const auto &layers) {
                    for (const auto &layer : layers) {
                        const auto &cell = layer[cellIndex];
                        if (!cell.present()) continue;
                        auto add = [&](const MapCell &tile) {
                            int cel = automapCatalog_.tileCel(region.recipe.levelType, tile, x, y);
                            if (cel < 0 || !cellCels.insert(cel).second) return;
                            stamps.push_back({x, y, cel});
                            used.insert(cel);
                        };
                        add(cell);
                        if (cell.orientation == 3) {
                            auto companion = cell;
                            companion.orientation = 4;
                            add(companion);
                        }
                    }
                };
                collect(map.data.floors);
                collect(map.data.walls);
            }
        for (const auto &object : region.objects) {
            int cel = object.npcClass.empty() ? automapCatalog_.objectCel(object.objectClass)
                                             : automapCatalog_.npcCel(object.npcClass);
            if (cel >= 0)
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
            if (cel < int(art->frames.size())) {
                auto frame = art->frames[cel];
                frame.x -= frame.width / 2;
                frame.y -= frame.height - frame.width / 4;
                automapCels[size].emplace(cel, graphics_.upload(frame));
            }
    }
}
} // namespace d2x
