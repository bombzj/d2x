#include "resources/formats.hpp"
#include "presentation/scene_assets.hpp"
#include "core/fingerprint.hpp"
#include <set>
#include <stdexcept>

namespace d2x {
void SceneAssets::loadAutomap(const IMapAssetSource &source) {
    if (!automapContentFingerprint) {
        // An explicit stable input set, independent of start region, loading order and UI assets.
        Fingerprint fingerprint;
        fingerprint.add("d2x-automap-content-v1");
        for (const auto *table : {"automap", "levels", "lvltypes", "lvlprest", "lvlmaze", "lvlsub", "objects", "monstats2"}) {
            const auto path = std::string("data/global/excel/") + table + ".txt";
            fingerprint.add(path); fingerprint.add(archives_.read(path));
        }
        automapContentFingerprint = fingerprint.value();
    }
    std::set<int> used{317};
    regionTownAutomap.resize(source.size());
    regionAutomapVariants.resize(source.size());
    regionAutomap.resize(source.size());
    regionAutomapLoaded.resize(source.size(), false);
    for (size_t regionIndex = 0; regionIndex < source.size(); ++regionIndex) {
        const auto &region = source.readAsset(regionIndex);
        if (!region.loaded || regionAutomapLoaded[regionIndex]) continue;
        if (!region.townAutomap) continue;
        regionAutomapVariants[regionIndex] = region.variant;
        for (int size = 0; size < 2; ++size)
            regionTownAutomap[regionIndex][size] = townAutomapSprites(int(region.region), region.variant, size != 0);
    }

    for (size_t index = 0; index < source.size(); ++index) {
        const auto &region = source.readAsset(index);
        if (!region.loaded || regionAutomapLoaded[index]) continue;
        regionAutomapLoaded[index] = true;
        auto &stamps = regionAutomap[index];
        stamps = automapCatalog_.stamps(*region.data, region.levelType);
        for (const auto &stamp : stamps) used.insert(stamp.cel);
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
    for (int size = 0; size < 2; ++size)
        for (int cel : used) automapSprite(cel, size != 0);
}
const Sprite *SceneAssets::automapSprite(int cel, bool large) const {
    if (cel < 0) return nullptr;
    auto &cache = automapCels[size_t(large)];
    if (const auto found = cache.find(cel); found != cache.end()) return &found->second;
    const auto *art = graphics_.animation(large ? "data/global/ui/automap/maximap.dc6"
                                               : "data/global/ui/automap/maximaps.dc6");
    if (!art) throw std::runtime_error("Original automap image unavailable");
    if (size_t(cel) >= art->frames.size()) return nullptr;
    auto frame = art->frames[size_t(cel)];
    frame.x -= frame.width / 2;
    frame.y -= cel == 317 ? frame.height / 2 : frame.height - frame.width / 4;
    return &cache.emplace(cel, graphics_.upload(frame)).first->second;
}
const std::vector<Sprite> &SceneAssets::townAutomapSprites(int level, int variant, bool large) const {
    const auto key = std::tuple{level, variant, large};
    if (const auto found = townAutomapArt.find(key); found != townAutomapArt.end()) return found->second;
    const char *name = level == 40 ? "act2map" : level == 103 ? "act4map" : level == 109 ? "extnmap" : nullptr;
    if (!name) return townAutomapArt[key];
    const int columns = level == 40 ? 5 : level == 103 ? 2 : 3;
    const int rows = level == 40 ? 4 : 2, count = columns * rows;
    const int group = level == 40 ? variant - 1 : 0;
    if (group < 0 || group > (level == 40 ? 1 : 0))
        throw std::runtime_error("Unsupported original town automap variant");
    const auto path = std::string("data/global/ui/automap/") + name + (large ? "" : "s") + ".dc6";
    const auto *art = graphics_.animation(path);
    if (!art || int(art->frames.size()) != count * (level == 40 ? 2 : 1))
        throw std::runtime_error("Original town automap frames disagree with layout: " + path);
    const int width = art->frames[size_t(group * count)].width, height = art->frames[size_t(group * count)].height;
    std::vector<Sprite> images;
    for (int index = 0; index < count; ++index) {
        if (!townAutomapCellVisible(level, group * count + index)) continue;
        auto frame = art->frames[size_t(group * count + index)];
        if (frame.width != width || frame.height != height)
            throw std::runtime_error("Inconsistent original town automap frame dimensions");
        frame.x += (index % columns) * width - columns * width / 2;
        frame.y += (index / columns) * height - rows * height / 2;
        images.push_back(graphics_.upload(frame));
    }
    return townAutomapArt.emplace(key, std::move(images)).first->second;
}
} // namespace d2x
