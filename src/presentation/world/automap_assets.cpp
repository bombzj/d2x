#include "resources/formats.hpp"
#include "presentation/scene_assets.hpp"
#include <set>
#include <stdexcept>

namespace d2x {
void SceneAssets::loadAutomap(const IMapAssetSource &source) {
    std::set<int> used{317};
    regionTownAutomap.resize(source.size());
    regionAutomap.resize(source.size());
    regionAutomapLoaded.resize(source.size(), false);
    for (size_t regionIndex = 0; regionIndex < source.size(); ++regionIndex) {
        const auto &region = source.readAsset(regionIndex);
        if (!region.loaded || regionAutomapLoaded[regionIndex]) continue;
        if (!region.townAutomap) continue;
        const int level = int(region.region);
        const char *name = level == 40 ? "act2map" : level == 103 ? "act4map"
                         : level == 109 ? "extnmap" : nullptr;
        if (!name) continue;
        const int columns = level == 40 ? 5 : level == 103 ? 2 : 3;
        const int rows = level == 40 ? 4 : 2;
        const int count = columns * rows;
        const int group = level == 40 ? region.variant - 1 : 0;
        if (group < 0 || group > (level == 40 ? 1 : 0))
            throw std::runtime_error("Unsupported original town automap variant");
        for (int size = 0; size < 2; ++size) {
            const auto path = std::string("data/global/ui/automap/") + name + (size ? "" : "s") + ".dc6";
            const auto *art = graphics_.animation(path);
            if (!art || int(art->frames.size()) != count * (level == 40 ? 2 : 1))
                throw std::runtime_error("Original town automap frames disagree with layout: " + path);
            const int width = art->frames[size_t(group * count)].width;
            const int height = art->frames[size_t(group * count)].height;
            for (int index = 0; index < count; ++index) {
                auto frame = art->frames[size_t(group * count + index)];
                if (frame.width != width || frame.height != height)
                    throw std::runtime_error("Inconsistent original town automap frame dimensions");
                frame.x += (index % columns) * width - columns * width / 2;
                frame.y += (index / columns) * height - rows * height / 2;
                regionTownAutomap[regionIndex][size].push_back(graphics_.upload(frame));
            }
        }
    }
    for (size_t index = 0; index < source.size(); ++index) {
        const auto &region = source.readAsset(index);
        if (!region.loaded || regionAutomapLoaded[index]) continue;
        regionAutomapLoaded[index] = true;
        const auto &data = *region.data;
        auto &stamps = regionAutomap[index];
        for (int y = 0; y < data.height; ++y)
            for (int x = 0; x < data.width; ++x) {
                const size_t cellIndex = size_t(y) * data.width + x;
                std::set<int> cellCels;
                auto collect = [&](const auto &layers) {
                    for (const auto &layer : layers) {
                        const auto &cell = layer[cellIndex];
                        if (!cell.present()) continue;
                        auto add = [&](const MapCell &tile) {
                            int cel = automapCatalog_.tileCel(region.levelType, tile, x, y);
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
                collect(data.floors);
                collect(data.walls);
            }
        for (const auto &object : region.markers) {
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
            if (cel < int(art->frames.size()) && !automapCels[size].contains(cel)) {
                auto frame = art->frames[cel];
                frame.x -= frame.width / 2;
                frame.y -= cel == 317 ? frame.height / 2 : frame.height - frame.width / 4;
                automapCels[size].emplace(cel, graphics_.upload(frame));
            }
    }
}
} // namespace d2x
