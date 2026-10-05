#include "remote_town.hpp"
#include "world/map_assembly.hpp"
#include "world/preset_identity.hpp"
#include <algorithm>
#include <iterator>
#include <set>
#include <stdexcept>

namespace d2x {
bool RemoteTown::collisionInvariant(const MapTerrain &terrain) const {
    // The preset fallback used outside Act I has no retail room RNG stream.
    // Admit it only when all eligible variants have identical collision flags.
    for (const auto &[choice, selected] : terrain.tileChoices) {
        const auto &[x, y, scope, key] = choice;
        (void)x;
        (void)y;
        const auto found = terrain.scopedLookup.at(scope).find(key);
        if (found == terrain.scopedLookup.at(scope).end())
            continue;
        const auto &indices = found->second;
        const bool weighted = std::any_of(indices.begin(), indices.end(),
                                          [&](int i) { return terrain.tiles.at(i)->rarity > 0; });
        for (int i : indices) {
            const auto *tile = terrain.tiles.at(i);
            if (!tile->animated() && (weighted ? tile->rarity <= 0 : i != indices.front()))
                continue;
            if (tile->flags != terrain.tiles.at(selected)->flags)
                return false;
        }
    }
    return terrain.unresolved == 0;
}
void RemoteTown::update(const OnlineView &v) {
    if (gameGeneration_ != v.gameGeneration || areaGeneration_ != v.world.areaGeneration) {
        gameGeneration_ = v.gameGeneration;
        areaGeneration_ = v.world.areaGeneration;
        revision_ = ~uint64_t{};
        map_ = nullptr;
        maps_.clear();
        nativeMap_.reset();
        nativeTerrain_.reset();
        nativePlayerRoom_.reset();
        nativePlayerPosition_.reset();
        nativeSequence_ = 0;
        nativeErrors_.clear();
        nativeReason_.clear();
        catalog_.reset();
        requestedLevels_.clear();
        rejectedLevels_.clear();
        layout_.reset();
        candidates_.clear();
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
                        if (*v.load.act == 0)
                            nativeMap_ = std::make_unique<NativeMapGenerator>(archives_, catalog,
                                libraries_, 0, *v.load.mapSeed, v.load.difficulty.value_or(0));
                    } catch (const std::exception &e) {
                        view_.layoutReason = e.what();
                        nativeReason_ = e.what();
                    }
                }
                if (*v.load.act != 0) requestPresetArea(town.id);
                if (*v.load.act != 0 && candidates_.empty())
                    view_.reason = "Original town presets are unavailable";
            } catch (const std::exception &e) {
                view_.reason = e.what();
            }
        } else
            view_.reason = "Waiting for the server act and town assignment";
    }
    if (revision_ == v.revision)
        return;
    revision_ = v.revision;
    if (catalog_ && v.load.act && *v.load.act != 0) {
        for (const auto &[room, anchor] : v.world.rooms) {
            (void)anchor;
            const int level = std::get<0>(room);
            const auto found = catalog_->levels().find(level);
            if (found == catalog_->levels().end() || found->second.act != *v.load.act ||
                found->second.generation != GenerationKind::Preset) continue;
            if (rejectedLevels_.contains(level)) continue;
            try { requestPresetArea(level); }
            catch (const std::exception &e) { rejectedLevels_.emplace(level, e.what()); }
        }
    }
    view_.available = view_.movementAvailable = false;
    view_.collisionVerified = false;
    view_.landmarks = view_.candidates = 0;
    view_.area.reset();
    view_.layoutOrigin.reset();
    view_.layoutMatched = false;
    view_.mapErrors = rejectedLevels_;
    if (updateNative(v)) return;
    if (v.load.act && *v.load.act == 0) {
        map_ = nullptr;
        view_.reason = nativeReason_.empty() ? "Waiting for native room assignments and player position" : nativeReason_;
        view_.nativeMapReason = view_.reason;
        for (const auto &[level, reason] : nativeErrors_) view_.mapErrors[level] = reason;
        return;
    }
    for (const auto &[level, reason] : nativeErrors_) view_.mapErrors[level] = reason;
    if (candidates_.empty())
        return;
    if (v.stage != OnlineStage::ProtocolReady || !v.load.serverLoadComplete || !v.world.playerPosition ||
        !v.load.playerUnitId) {
        view_.reason = "Waiting for the server player and completed loading";
        return;
    }
    struct Match {
        size_t candidate;
        int x, y, landmarks;
    };
    std::vector<Match> matches;
    for (size_t index = 0; index < candidates_.size(); ++index) {
        const auto &candidate = candidates_[index];
        std::set<std::pair<int, int>> origins;
        for (const auto &[key, u] : v.world.units) {
            if (key.type != 2 || !u.classId || !u.position)
                continue;
            for (const auto &o : candidate.data.objects) {
                if (o.type != 2 || originalObjectClass(o, candidate.data.version, candidate.recipe.act) != *u.classId)
                    continue;
                origins.emplace(int(u.position->x) - o.x, int(u.position->y) - o.y);
            }
        }
        for (const auto &[ox, oy] : origins) {
            if (ox < 0 || oy < 0 || ox % 5 || oy % 5)
                continue;
            const int width = (candidate.data.width - 1) * 5, height = (candidate.data.height - 1) * 5;
            if (ox + width > 65535 || oy + height > 65535)
                continue;
            const auto player = *v.world.playerPosition;
            if (player.x < ox || player.y < oy || player.x >= ox + width || player.y >= oy + height)
                continue;
            bool rooms = false, valid = true;
            for (const auto &[room, anchor] : v.world.rooms) {
                if (std::get<0>(room) != candidate.level)
                    continue;
                rooms = true;
                const int x = int(anchor.x) - ox / 5, y = int(anchor.y) - oy / 5;
                // DRLGPRESET_BuildArea(false) partitions the authored preset on an
                // eight-tile lattice. Room extents are deliberately not guessed.
                if (x < 0 || y < 0 || x >= width / 5 || y >= height / 5 || x % 8 || y % 8)
                    valid = false;
            }
            if (!rooms || !valid)
                continue;
            int landmarks = 0;
            std::set<int> classes;
            for (const auto &[key, u] : v.world.units) {
                if (key.type != 2 || !u.classId || !u.position)
                    continue;
                if (u.position->x < ox || u.position->y < oy ||
                    u.position->x >= ox + width || u.position->y >= oy + height)
                    continue; // Adjacent active areas can contain the same object classes.
                const bool authoredClass = std::any_of(
                    candidate.data.objects.begin(), candidate.data.objects.end(), [&](const auto &o) {
                        return o.type == 2 && originalObjectClass(o, candidate.data.version, candidate.recipe.act) == *u.classId;
                    });
                if (!authoredClass)
                    continue; // Dynamic portals etc. cannot bind a preset.
                const bool matched = std::any_of(
                    candidate.data.objects.begin(), candidate.data.objects.end(), [&](const auto &o) {
                        return o.type == 2 &&
                               originalObjectClass(o, candidate.data.version, candidate.recipe.act) == *u.classId &&
                               o.x + ox == u.position->x && o.y + oy == u.position->y;
                    });
                if (!matched) {
                    valid = false;
                    break;
                }
                ++landmarks;
                classes.insert(*u.classId);
            }
            if (valid && landmarks >= 2 && classes.size() >= 2)
                matches.push_back({index, ox, oy, landmarks});
        }
    }
    view_.candidates = int(matches.size());
    if (matches.size() != 1) {
        view_.reason = matches.empty() ? "Waiting for matching server rooms and stationary landmarks"
                                       : "Area preset is ambiguous; waiting for more server landmarks";
        return;
    }
    const auto &match = matches.front();
    const auto &candidate = candidates_[match.candidate];
    const OnlinePoint origin{uint16_t(match.x), uint16_t(match.y)};
    try {
        const auto key = std::tuple{match.candidate, match.x, match.y};
        auto found = maps_.find(key);
        if (found == maps_.end()) {
            auto map = std::make_unique<Map>();
            map->load(archives_, libraries_, candidate.recipe, v.load.mapSeed.value_or(0));
            const bool verified = collisionInvariant(map->terrain);
            found = maps_.emplace(key, CachedMap{std::move(map), verified}).first;
        }
        map_ = found->second.map.get();
        view_.collisionVerified = found->second.collisionVerified;
        view_.map = candidate.recipe.ds1;
        view_.origin = origin;
        view_.width = (candidate.data.width - 1) * 5;
        view_.height = (candidate.data.height - 1) * 5;
        const auto level = uint16_t(candidate.level);
        if (std::find(view_.cachedAreas.begin(), view_.cachedAreas.end(), level) == view_.cachedAreas.end())
            view_.cachedAreas.push_back(level);
    } catch (const std::exception &e) {
        view_.reason = e.what();
        view_.mapErrors[candidate.level] = e.what();
        return;
    }
    view_.landmarks = match.landmarks;
    view_.area = uint16_t(candidate.level);
    view_.layoutOrigin.reset();
    if (layout_ && layout_->levels.contains(candidate.level)) {
        const auto &placement = layout_->levels.at(candidate.level);
        if (placement.x >= 0 && placement.y >= 0 && placement.x <= 13107 && placement.y <= 13107)
            view_.layoutOrigin = OnlinePoint{uint16_t(placement.x * 5), uint16_t(placement.y * 5)};
    }
    view_.layoutMatched = view_.layoutOrigin == view_.origin;
    if (view_.layoutOrigin)
        view_.layoutReason = view_.layoutMatched ? "Pure C++ origin matches server area landmarks"
                                                : "Pure C++ origin differs from server area landmarks";
    view_.available = view_.collisionVerified;
    view_.movementAvailable = view_.collisionVerified && !(v.world.life && !*v.world.life);
    view_.reason = view_.collisionVerified
                       ? "Server landmarks and authored area rooms matched"
                       : "Area matched; differing or unresolved DT1 collision prevents world entry";
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
void RemoteTown::requestPresetArea(int levelId) {
    if (!catalog_ || requestedLevels_.contains(levelId)) return;
    const auto &level = catalog_->level(levelId);
    std::vector<Candidate> loaded;
    for (const auto &[id, preset] : catalog_->presets()) {
        if (preset.level != levelId) continue;
        for (int variant = 0; variant < int(preset.variants.size()); ++variant) {
            if (preset.variants[size_t(variant)].empty()) continue;
            auto recipe = catalog_->preset(id, level.levelType, variant);
            recipe.act = level.act;
            auto data = assembleMap(archives_, recipe);
            loaded.push_back({levelId, std::move(recipe), std::move(data)});
        }
    }
    if (loaded.empty())
        throw std::runtime_error("Area has no authored MPQ preset candidates: " + std::to_string(levelId));
    // Commit a whole area's candidates together; a failed resource read must
    // not leave a partial candidate set that accidentally appears unique.
    candidates_.insert(candidates_.end(), std::make_move_iterator(loaded.begin()),
                       std::make_move_iterator(loaded.end()));
    requestedLevels_.insert(levelId);
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
