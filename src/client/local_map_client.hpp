#pragma once
#include "client/map_client.hpp"
#include "client/map_asset_source.hpp"

namespace d2x {
class GameSession;
class LocalMapClient final : public IMapClient, public IMapAssetSource {
    GameSession &session_;
    mutable MapSceneView scene_;
    mutable TravelMenuView travel_;
    mutable std::vector<MapAssetView> assets_;
  public:
    explicit LocalMapClient(GameSession &session) : session_(session) {}
    const MapSceneView &read() const override;
    const TravelMenuView &travel(EntityId source, int act) const override;
    bool waypointSource(EntityId object) const override;
    void submit(MapIntent intent) override;
    size_t size() const override;
    const MapAssetView &readAsset(size_t slot) const override;
};
} // namespace d2x
