#include "remote_town.hpp"
#include "world/map_assembly.hpp"
#include "world/preset_identity.hpp"
#include <algorithm>
#include <set>

namespace d2x {
bool RemoteTown::collisionInvariant(const MapTerrain &terrain) const {
    // The retail room RNG stream is not reproduced. Different image variants may
    // be shown only when every eligible variant has identical collision flags.
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
        map_.reset();
        candidates_.clear();
        view_ = {};
        if (v.load.act == 0 && v.load.townArea == 1) {
            try {
                WorldCatalog catalog(archives_, v.load.difficulty.value_or(0));
                const auto &town = catalog.level(*v.load.townArea);
                for (const auto &[id, preset] : catalog.presets()) {
                    if (preset.level != town.id)
                        continue;
                    for (int variant = 0; variant < int(preset.variants.size()); ++variant) {
                        if (preset.variants[size_t(variant)].empty())
                            continue;
                        auto recipe = catalog.preset(id, town.levelType, variant);
                        recipe.act = town.act;
                        auto data = assembleMap(archives_, recipe);
                        candidates_.push_back({std::move(recipe), std::move(data)});
                    }
                }
                if (candidates_.empty())
                    view_.reason = "Original town presets are unavailable";
            } catch (const std::exception &e) {
                view_.reason = e.what();
            }
        } else
            view_.reason = "Online terrain currently supports the Act I town only";
    }
    if (revision_ == v.revision)
        return;
    revision_ = v.revision;
    view_.available = view_.movementAvailable = false;
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
                if (o.type != 2 || originalObjectClass(o, candidate.data.version, 0) != *u.classId)
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
                if (std::get<0>(room) != *v.load.townArea)
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
                const bool authoredClass = std::any_of(
                    candidate.data.objects.begin(), candidate.data.objects.end(), [&](const auto &o) {
                        return o.type == 2 && originalObjectClass(o, candidate.data.version, 0) == *u.classId;
                    });
                if (!authoredClass)
                    continue; // Dynamic portals etc. cannot bind a preset.
                const bool matched = std::any_of(
                    candidate.data.objects.begin(), candidate.data.objects.end(), [&](const auto &o) {
                        return o.type == 2 &&
                               originalObjectClass(o, candidate.data.version, 0) == *u.classId &&
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
                                       : "Town preset is ambiguous; waiting for more server landmarks";
        return;
    }
    const auto &match = matches.front();
    const auto &candidate = candidates_[match.candidate];
    const OnlinePoint origin{uint16_t(match.x), uint16_t(match.y)};
    if (!map_ || view_.origin != origin || view_.map != candidate.recipe.ds1) {
        try {
            auto map = std::make_unique<Map>();
            map->load(archives_, libraries_, candidate.recipe, v.load.mapSeed.value_or(0));
            view_.collisionVerified = collisionInvariant(map->terrain);
            map_ = std::move(map);
            view_.map = candidate.recipe.ds1;
            view_.origin = origin;
            view_.width = (candidate.data.width - 1) * 5;
            view_.height = (candidate.data.height - 1) * 5;
        } catch (const std::exception &e) {
            view_.reason = e.what();
            return;
        }
    }
    view_.landmarks = match.landmarks;
    view_.available = view_.collisionVerified;
    view_.movementAvailable = view_.collisionVerified && !(v.world.life && !*v.world.life);
    view_.reason = view_.collisionVerified
                       ? "Server landmarks and town rooms matched"
                       : "Town matched; differing or unresolved DT1 collision prevents world entry";
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
