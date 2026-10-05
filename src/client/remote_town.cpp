#include "remote_town.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
void RemoteTown::update(const OnlineView &v) {
    if (gameGeneration_ != v.gameGeneration || areaGeneration_ != v.world.areaGeneration) {
        gameGeneration_ = v.gameGeneration;
        areaGeneration_ = v.world.areaGeneration;
        revision_ = ~uint64_t{};
        map_ = nullptr;
        nativeMap_.reset();
        nativeTerrain_.reset();
        nativePlayerRoom_.reset();
        nativePlayerPosition_.reset();
        nativeSequence_ = 0;
        nativeErrors_.clear();
        nativeReason_.clear();
        catalog_.reset();
        layout_.reset();
        view_ = {};
        if (v.load.act && v.load.townArea && *v.load.act < actTownLevels.size() &&
            *v.load.townArea == actTownLevels[*v.load.act]) {
            try {
                catalog_ = std::make_unique<WorldCatalog>(archives_, v.load.difficulty.value_or(0));
                const auto &catalog = *catalog_;
                const auto &town = catalog.level(*v.load.townArea);
                if (v.load.mapSeed) {
                    try {
                        layout_ = placeNativeAct(catalog, *v.load.act, *v.load.mapSeed);
                        const auto &placement = layout_->levels.at(town.id);
                        if (placement.x >= 0 && placement.y >= 0 && placement.x <= 13107 &&
                            placement.y <= 13107)
                            view_.layoutOrigin = OnlinePoint{uint16_t(placement.x * 5),
                                                            uint16_t(placement.y * 5)};
                        view_.layoutReason = "Pure C++ placement awaiting server landmark comparison";
                        nativeMap_ = std::make_unique<NativeMapGenerator>(archives_, catalog,
                                libraries_, *v.load.act, *v.load.mapSeed, v.load.difficulty.value_or(0));
                    } catch (const std::exception &e) {
                        view_.layoutReason = e.what();
                        nativeReason_ = e.what();
                    }
                }
            } catch (const std::exception &e) {
                view_.reason = e.what();
            }
        } else
            view_.reason = "Waiting for the server act and town assignment";
    }
    if (revision_ == v.revision)
        return;
    revision_ = v.revision;
    view_.available = view_.movementAvailable = false;
    view_.collisionVerified = false;
    view_.landmarks = view_.candidates = 0;
    view_.area.reset();
    view_.layoutOrigin.reset();
    view_.layoutMatched = false;
    view_.mapErrors.clear();
    if (updateNative(v)) return;
    map_ = nullptr;
    view_.reason = nativeReason_.empty() ? "Waiting for native room assignments and player position" : nativeReason_;
    view_.nativeMapReason = view_.reason;
    for (const auto &[level, reason] : nativeErrors_) view_.mapErrors[level] = reason;
}
bool RemoteTown::updateNative(const OnlineView &v) {
    view_.nativeMapReady = false;
    view_.nativeMapReason = nativeReason_;
    if (!nativeMap_ || !nativeReason_.empty()) return false;
    int eventLevel = 0;
    try {
        const auto &world = v.world;
        if (!nativeSequence_ && !nativePlayerPosition_)
            nativePlayerPosition_ = world.mapInitialPlayerPosition;
        bool changed = false;
        auto moveRoot = [&] {
            if (!nativePlayerPosition_) return;
            const auto room = nativeMap_->clientRoom(nativePlayerPosition_->x, nativePlayerPosition_->y);
            if (!room) return; // Position can precede the matching room assignment.
            changed |= nativePlayerRoom_ != room;
            nativeMap_->changeClientRoom(nativePlayerRoom_, room);
            nativePlayerRoom_ = room;
        };
        for (const auto &event : world.mapEvents) {
            if (event.sequence <= nativeSequence_) continue;
            if (event.sequence != nativeSequence_ + 1)
                throw std::runtime_error("Native map event history was lost; rejoin the game");
            eventLevel = event.level;
            switch (event.kind) {
            case OnlineMapEvent::Kind::RevealRoom:
                changed = true;
                nativeMap_->reveal(event.level, event.point.x, event.point.y);
                moveRoot();
                break;
            case OnlineMapEvent::Kind::HideRoom:
                changed = true;
                nativeMap_->hide(event.level, event.point.x, event.point.y);
                break;
            case OnlineMapEvent::Kind::PlayerPosition:
                nativePlayerPosition_ = event.point;
                moveRoot();
                break;
            case OnlineMapEvent::Kind::RemovePlayer:
                changed = true;
                nativeMap_->changeClientRoom(nativePlayerRoom_, {});
                nativePlayerRoom_.reset();
                nativePlayerPosition_.reset();
                break;
            }
            nativeSequence_ = event.sequence;
        }
        if (nativeSequence_ != world.mapEventSequence)
            throw std::runtime_error("Native map event tail is unavailable; rejoin the game");
        if (!nativePlayerRoom_ || v.stage != OnlineStage::ProtocolReady ||
            !v.load.serverLoadComplete || !world.playerPosition) return false;
        const int level = nativeMap_->tiles().rooms().at(*nativePlayerRoom_).level;
        if (changed || !nativeTerrain_) {
            auto snapshot = nativeMap_->snapshot(level);
            view_.origin = OnlinePoint{uint16_t(snapshot.tileX * 5), uint16_t(snapshot.tileY * 5)};
            view_.width = snapshot.map.grid.width; view_.height = snapshot.map.grid.height;
            auto map = std::make_unique<Map>(std::move(snapshot.map));
            nativeTerrain_.swap(map);
            for (int cached : snapshot.levels)
                if (std::find(view_.cachedAreas.begin(), view_.cachedAreas.end(), cached) == view_.cachedAreas.end())
                    view_.cachedAreas.push_back(uint16_t(cached));
        }
        map_ = nativeTerrain_.get();
        view_.area = uint16_t(level);
        if (layout_) {
            const auto &placement = layout_->levels.at(level);
            view_.layoutOrigin = OnlinePoint{uint16_t(placement.x * 5), uint16_t(placement.y * 5)};
        }
        view_.layoutMatched = true; // reveal() validated every server room anchor.
        view_.layoutReason = "Native room anchors match generated area coordinates";
        view_.map = map_->terrain.path;
        view_.collisionVerified = view_.available = view_.nativeMapReady = true;
        view_.movementAvailable = !(world.life && !*world.life);
        view_.nativeMapReason = view_.reason = "Native room anchors and ordered lifecycle reconstructed";
        return true;
    } catch (const std::exception &e) {
        nativeReason_ = e.what();
        nativeErrors_[eventLevel] = nativeReason_;
        view_.nativeMapReason = nativeReason_;
        return false;
    }
}
bool RemoteTown::permits(const OnlineView &v, OnlinePoint target) const {
    if (!view_.available || !view_.movementAvailable || !map_ || !view_.origin ||
        gameGeneration_ != v.gameGeneration || areaGeneration_ != v.world.areaGeneration ||
        v.stage != OnlineStage::ProtocolReady || !v.world.playerPosition)
        return false;
    const auto origin = *view_.origin;
    auto inside = [&](OnlinePoint p) {
        return p.x >= origin.x && p.y >= origin.y && int(p.x) < origin.x + view_.width &&
               int(p.y) < origin.y + view_.height;
    };
    if (!inside(target) || !inside(*v.world.playerPosition))
        return false;
    return map_->grid.walkable(int(target.x) - origin.x, int(target.y) - origin.y);
}
} // namespace d2x
