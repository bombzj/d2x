#include "resources/formats.hpp"
#include "presentation/scene_assets.hpp"
#include <set>
#include <stdexcept>
#include <limits>

namespace d2x {
Color SceneAssets::automapColor(Color rgb) const {
    if (!automapColors) automapColors = decodePalette(archives_.read("data/global/palette/act1/pal.dat"));
    const Pixel *best = nullptr;
    int distance = std::numeric_limits<int>::max();
    for (const auto &p : *automapColors) {
        const int r = int(p.r) - rgb.r, g = int(p.g) - rgb.g, b = int(p.b) - rgb.b;
        const int candidate = r * r + g * g + b * b;
        if (candidate < distance) { distance = candidate; best = &p; }
    }
    return {best->r, best->g, best->b, 255};
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
    // CelDrawClipped uses the DC6 x offset and an inclusive bottom baseline.
    // It never centers a frame by its width or special-cases cel 317.
    frame.y -= frame.height - 1;
    return &cache.emplace(cel, graphics_.upload(frame, true)).first->second;
}
const std::vector<TownAutomapCell> &SceneAssets::townAutomapSprites(int level, int variant, bool large) const {
    const auto key = std::tuple{level, variant, large};
    if (const auto found = townAutomapArt.find(key); found != townAutomapArt.end()) return found->second;
    const auto path=townAutomapPath(level,large);
    if(path.empty()) return townAutomapArt[key];
    const auto *art=graphics_.animation(path);
    if(!art) throw std::runtime_error("Original town automap image unavailable: "+path);
    std::vector<TownAutomapCell> images;
    for(const auto &cell:townAutomapLayout(*art,level,variant,large)) {
        auto frame=art->frames[cell.frame];frame.y-=frame.height-1;
        images.push_back({graphics_.upload(frame,true),cell.offset});
    }
    return townAutomapArt.emplace(key, std::move(images)).first->second;
}
} // namespace d2x
