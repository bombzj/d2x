#include "client/local_map_client.hpp"
#include "gameplay/session/session.hpp"
#include "gameplay/model/state.hpp"
#include "world/region.hpp"
#include "world/region_visibility.hpp"
#include "core/fingerprint.hpp"
#include <algorithm>
#include <utility>

namespace d2x {
const MapSceneView &LocalMapClient::read() const {
    if (scene_.revision == session_.viewRevision()) return scene_;
    MapSceneView view;
    view.revision = session_.viewRevision(); view.actor = session_.state().player.id;
    view.region = session_.state().area.region; view.current = session_.regionIndex();
    view.observer = session_.state().player.movement.pos;
    const auto &regions = session_.regions();
    const auto &origin = session_.region();
    const auto &levels = session_.worldContent().levels();
    if (const auto level = levels.find(int(view.region)); level != levels.end()) view.act = level->second.act;
    for (size_t act = 0; act < view.waypointActs.size(); ++act)
        view.waypointActs[act] = act == 0 || session_.waypointUnlocked(RegionId(actTownLevels[act]));
    view.automapRegions = connectedRegionSlots(regions, view.region, true);
    view.regions.resize(regions.size());
    const auto *observer = origin.map.activation.room(view.observer);
    view.hasObserverRoom = observer != nullptr;
    for (const auto &portal : session_.portals(view.region))
        view.portals.push_back({portal.position, portal.revision, false});
    if (const auto portal = session_.cainPortalPosition()) view.portals.push_back({*portal, 0, true});
    for (const auto &[slot, offset] : view.automapRegions) {
        const auto &region = regions[size_t(slot)];
        auto &value = view.regions[size_t(slot)];
        value.id = region.definition.id; value.safe = region.definition.safe;
        value.width = region.map.terrain.data.width; value.height = region.map.terrain.data.height;
        if (!region.loaded) continue;
        auto [fingerprintEntry, inserted] = layoutFingerprints_.try_emplace(value.id);
        if (inserted) {
            Fingerprint fingerprint;
            auto number = [&](auto n) { fingerprint.add(std::to_string(n)); };
            number(value.width); number(value.height); number(region.recipe.levelType); number(region.recipe.variant);
            // Hash generation geometry: opening a quest wall must not invalidate discovery on reload.
            std::map<size_t, const Map::TombWall *> closedWalls;
            for (const auto &wall : region.map.tombWalls)
                closedWalls.emplace(size_t(wall.y) * value.width + wall.x, &wall);
            for (const auto &layers : {&region.map.terrain.data.floors, &region.map.terrain.data.walls}) {
                number(layers->size());
                for (size_t layerIndex = 0; layerIndex < layers->size(); ++layerIndex) {
                    const auto &layer = (*layers)[layerIndex];
                    number(layer.size());
                    for (size_t cellIndex = 0; cellIndex < layer.size(); ++cellIndex) {
                        const auto closed = closedWalls.find(cellIndex);
                        const auto &cell = layers == &region.map.terrain.data.walls && closed != closedWalls.end()
                            ? closed->second->walls.at(layerIndex) : layer[cellIndex];
                        number(cell.value); number(cell.orientation);
                    }
                }
            }
            for (const auto &room : region.map.rooms) {
                number(room.x); number(room.y); number(room.width); number(room.height);
            }
            fingerprintEntry->second = fingerprint.value();
        }
        value.layoutFingerprint = fingerprintEntry->second;
        if (observer) {
            auto local = *observer; local.x -= int(offset.x); local.y -= int(offset.y);
            for (const auto *room : region.map.activation.nearRooms(local))
                value.revealRooms.push_back({room->x, room->y, room->width, room->height});
        }
        for (const auto &object : region.objects) if (!object.questHidden)
            value.markers.push_back({object.pos, object.objectClass, object.npcClass, object.name,
                object.interaction != Interaction::None &&
                    (!object.npcClass.empty() || object.interaction == Interaction::Stash)});
        for (const auto &portal : session_.portals(value.id))
            value.markers.push_back({portal.position, 59, {}, {}, false});
        if (slot == view.current)
            if (const auto portal = session_.cainPortalPosition())
                value.markers.push_back({*portal, 60, {}, {}, false});
    }
    for (const auto &exit : origin.exits)
        view.exits.push_back({exit.slot, exit.position,
            {exit.selection.selectX, exit.selection.selectY, exit.selection.selectWidth, exit.selection.selectHeight},
            exit.name, exit.enabled});
    scene_ = std::move(view);
    return scene_;
}
const TravelMenuView &LocalMapClient::travel(EntityId source, int act) const {
    if (travel_.revision == session_.viewRevision() && travel_.source == source && travel_.act == act) return travel_;
    TravelMenuView view;
    view.revision = session_.viewRevision(); view.actor = session_.state().player.id; view.source = source; view.act = act;
    if (!source) {
        for (const auto &entry : session_.worldEntries())
            view.entries.push_back({entry.level, entry.name, entry.status, entry.missing, entry.destination});
    } else {
        std::vector<std::pair<int, TravelEntryView>> ordered;
        const auto &levels = session_.worldContent().levels();
        for (const auto &region : session_.regions()) {
            const auto record = levels.find(int(region.definition.id));
            if (record == levels.end() || record->second.act != act ||
                record->second.waypoint < 0 || record->second.waypoint == 255) continue;
            const bool unlocked = session_.waypointUnlocked(region.definition.id);
            ordered.push_back({record->second.waypoint, {int(region.definition.id), region.definition.name,
                unlocked ? "Activated" : "Not activated", {},
                unlocked ? std::optional<RegionId>{region.definition.id} : std::nullopt}});
        }
        std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
            return a.first == b.first ? a.second.level < b.second.level : a.first < b.first;
        });
        for (auto &[order, entry] : ordered) view.entries.push_back(std::move(entry));
    }
    travel_ = std::move(view);
    return travel_;
}
bool LocalMapClient::waypointSource(EntityId id) const {
    const auto *object = session_.object(id);
    return object && object->isWaypoint();
}
void LocalMapClient::submit(MapIntent intent) {
    std::visit([this](const auto &command) { session_.submit(command); }, intent);
}
size_t LocalMapClient::size() const { return session_.regions().size(); }
const MapAssetView &LocalMapClient::readAsset(size_t slot) const {
    assets_.resize(size());
    auto &view = assets_.at(slot);
    const auto &region = session_.regions().at(slot);
    if (view.loaded || !region.loaded) return view;
    view.region = region.definition.id; view.loaded = true;
    view.data = &region.map.terrain.data; view.tiles = region.map.terrain.tiles;
    view.levelType = region.recipe.levelType; view.variant = region.recipe.variant;
    const auto &catalog = session_.worldContent();
    const auto level = catalog.levels().find(int(view.region));
    view.palette = level == catalog.levels().end() ? region.map.terrain.data.act : level->second.palette;
    const auto preset = catalog.presets().find(region.recipe.preset);
    view.townAutomap = preset != catalog.presets().end() && preset->second.automap;
    for (const auto &object : region.objects) {
        view.markers.push_back({object.objectClass, object.npcClass});
        view.propKeys.push_back(object.key);
    }
    return view;
}
TerrainDrawBounds LocalMapClient::terrainBounds(size_t slot, int x, int y) const {
    const auto &region = session_.regions().at(slot);
    const auto &map = region.map;
    const auto &data = map.terrain.data;
    TerrainDrawBounds bounds;
    if (!region.loaded || x < 0 || y < 0 || x >= data.width || y >= data.height) return bounds;
    auto add = [&](const MapCell &cell) {
        if (!cell.present() || cell.orientation == 15) return;
        const int index = map.tileIndex(cell, x, y);
        if (index < 0) return;
        const auto &image = map.terrain.tiles.at(size_t(index))->image;
        if (image.width <= 0 || image.height <= 0) return;
        if (!bounds.width || !bounds.height) {
            bounds = {image.x, image.y, image.width, image.height};
        } else {
            const int right = std::max(bounds.x + bounds.width, image.x + image.width);
            const int bottom = std::max(bounds.y + bounds.height, image.y + image.height);
            bounds.x = std::min(bounds.x, image.x); bounds.y = std::min(bounds.y, image.y);
            bounds.width = right - bounds.x; bounds.height = bottom - bounds.y;
        }
    };
    const size_t cell = size_t(y) * data.width + x;
    for (const auto &layer : data.floors) add(layer.at(cell));
    for (const auto &layer : data.walls) add(layer.at(cell));
    return bounds;
}
} // namespace d2x
