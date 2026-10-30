#include "client/automap_exploration.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x {
void AutomapExploration::restore(AutomapLayers layers) {
    decltype(revealedCounts_) counts;
    for (const auto &[id, layer] : layers) {
        if (layer.width <= 0 || layer.height <= 0 ||
            uint64_t(layer.width) * layer.height != layer.seen.size() ||
            std::any_of(layer.seen.begin(), layer.seen.end(), [](uint8_t value) { return value > 1; }))
            throw std::runtime_error("Invalid automap exploration layer");
        counts.emplace(id,size_t(std::count(layer.seen.begin(),layer.seen.end(),uint8_t{1})));
    }
    layers_ = std::move(layers);revealedCounts_=std::move(counts);++revision_;
}
size_t AutomapExploration::revealedCount(RegionId id) const {
    const auto found=revealedCounts_.find(id);
    return found==revealedCounts_.end()?0:found->second;
}
bool AutomapExploration::reveal(const MapSceneView &scene, std::span<const AutomapVisibleCell> visible,bool revealSafe) {
    bool invalidated = false,changed=false;
    for (const auto &[slot, offset] : scene.automapRegions) {
        const auto &region = scene.regions.at(size_t(slot));
        if (region.width <= 0 || region.height <= 0) continue;
        auto &layer = layers_[region.id];
        auto &count = revealedCounts_[region.id];
        if (layer.width != region.width || layer.height != region.height ||
            layer.layoutFingerprint != region.layoutFingerprint) {
            invalidated |= !layer.seen.empty();changed=true;
            layer = {region.width, region.height, region.layoutFingerprint,
                     std::vector<uint8_t>(size_t(region.width) * region.height)};
            count=0;
        }
        if (revealSafe && region.safe && slot == scene.current) {
            if(count!=layer.seen.size()) {
                std::fill(layer.seen.begin(), layer.seen.end(), 1);
                count=layer.seen.size();changed=true;
            }
            continue;
        }
        for (const auto &cell : visible) {
            if (cell.slot != slot || cell.x < 0 || cell.y < 0 ||
                cell.x >= region.width || cell.y >= region.height) continue;
            // ppRoomsNear restricts candidates; loading a room does not reveal it wholesale.
            const bool candidate = std::any_of(region.revealRooms.begin(), region.revealRooms.end(),
                [&](const MapRect &room) {
                    return cell.x >= std::max(0, room.x / 5) && cell.x <= (room.x + room.width) / 5 &&
                           cell.y >= std::max(0, room.y / 5) && cell.y <= (room.y + room.height) / 5;
                });
            if (candidate) { auto &seen=layer.seen[size_t(cell.y)*region.width+cell.x];if(!seen) {seen=1;++count;changed=true;} }
        }
        if (!scene.hasObserverRoom && slot == scene.current) {
            const int x = int(std::floor(scene.observer.x / 5.f)), y = int(std::floor(scene.observer.y / 5.f));
            if (x >= 0 && y >= 0 && x < region.width && y < region.height) {
                auto &seen=layer.seen[size_t(y)*region.width+x];if(!seen) {seen=1;++count;changed=true;}
            }
        }
    }
    if(changed) ++revision_;
    return invalidated;
}
} // namespace d2x
