#include "remote_town.hpp"
#include <algorithm>
#include <stdexcept>
#include "world/interaction_geometry.hpp"

namespace d2x {
RemoteTown::RemoteTown(Archives &a)
    : archives_(a), libraries_(a), objects_(a.read("data/global/excel/objects.txt")),
      monsters_(a.read("data/global/excel/monstats.txt")),
      monsterSizes_(a.read("data/global/excel/monstats2.txt")),
      skills_(a.read("data/global/excel/skills.txt")), automap_(a), strings_(a) {
    for (size_t row = 0; row < objects_.rows().size(); ++row)
        if (auto id = objects_.number(row, "Id")) objectRows_.emplace(*id, row);
    for (size_t row = 0; row < monsters_.rows().size(); ++row)
        if (auto id = monsters_.number(row, "hcIdx")) monsterRows_.emplace(*id, row);
    for (size_t row = 0; row < monsterSizes_.rows().size(); ++row)
        monsterSizeRows_.emplace(std::string(monsterSizes_.value(row, "Id")), row);
}
void RemoteTown::update(const OnlineView &v) {
    if (gameGeneration_ != v.gameGeneration || areaGeneration_ != v.world.areaGeneration) {
        if (gameGeneration_ != v.gameGeneration) {
            explored_.clear(); discoveredCels_.clear(); discoveredTowns_.clear();
        }
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
    view_.palette.reset();
    view_.layoutOrigin.reset();
    view_.layoutMatched = false;
    view_.mapErrors.clear();
    view_.mapTargets.clear();
    view_.waypoints.clear();
    view_.town = false; view_.townPortalSkills.clear();
    view_.npcConversation.reset();
    view_.automapStamps.clear(); view_.automapTowns.clear(); view_.automapRevealedCells.clear();
    if (updateNative(v)) {
        updateMapTargets(v);
        updateAutomapView(v);
        return;
    }
    map_ = nullptr;
    view_.reason = nativeReason_.empty() ? "Waiting for native room assignments and player position" : nativeReason_;
    view_.nativeMapReason = view_.reason;
    for (const auto &[level, reason] : nativeErrors_) view_.mapErrors[level] = reason;
}
void RemoteTown::updateMapTargets(const OnlineView &v) {
    std::vector<Grid::Obstacle> obstacles;
    for (const auto &[key, unit] : v.world.units) {
        if (!unit.position || !unit.classId || !view_.origin) continue;
        const int x = int(unit.position->x) - view_.origin->x;
        const int y = int(unit.position->y) - view_.origin->y;
        if (x < 0 || y < 0 || x >= view_.width || y >= view_.height) continue;
        if (key.type == 0) {
            const auto owner = v.world.corpseOwners.find(key.id);
            if (owner != v.world.corpseOwners.end() && owner->second == v.load.playerUnitId) {
                const auto label = strings_.find("corpse");
                view_.mapTargets.push_back({key, *unit.position, OnlineMapInteraction::Corpse,
                    unit.name + " " + std::string(label), {}, playerMovement.size, playerMovement.size});
            }
            continue;
        }
        if (key.type == 1) {
            const auto record = monsterRows_.find(*unit.classId);
            if (record == monsterRows_.end() || !monsters_.number(record->second, "interact").value_or(0) ||
                unit.mode == 0 || unit.mode == 12) continue;
            const auto size = monsterSizeRows_.find(monsters_.value(record->second, "MonStatsEx"));
            if (size == monsterSizeRows_.end()) continue;
            const int width = monsterSizes_.number(size->second, "SizeX").value_or(0);
            const int height = monsterSizes_.number(size->second, "SizeY").value_or(0);
            if (width <= 0 || height <= 0) continue;
            view_.mapTargets.push_back({key, *unit.position, OnlineMapInteraction::Npc,
                std::string(monsters_.value(record->second, "NameStr")), {}, width, height});
            continue;
        }
        if (key.type == 5) {
            for (const auto &exit : map_->terrain.exits)
                if (exit.selection.id == *unit.classId && exit.position.x == x && exit.position.y == y) {
                    const auto &level = catalog_->level(exit.destination);
                    view_.mapTargets.push_back({key, *unit.position, OnlineMapInteraction::Exit,
                        level.name, uint16_t(exit.destination)});
                    break;
                }
            continue;
        }
        if (key.type != 2) continue;
        const auto record = objectRows_.find(*unit.classId);
        if (record == objectRows_.end()) continue;
        const size_t row = record->second;
        auto number = [&](std::string_view field) { return objects_.number(row, field).value_or(0); };
        if (unit.mode && *unit.mode < 8) {
            const auto suffix = std::to_string(*unit.mode);
            const bool collision = number("HasCollision" + suffix) != 0;
            const bool light = number("BlocksLight" + suffix) != 0;
            const int width = number("SizeX"), height = number("SizeY");
            if ((collision || light) && width > 0 && height > 0) {
                const bool door = number("IsDoor") != 0, missile = number("BlockMissile") != 0;
                const uint16_t mask = door ? (number("BlocksVis") ? 0x0806 : missile ? 0x0804 : 0x0400)
                    : (number("SubClass") & 4) ? 0x8000 : missile ? 0x0404 : 0x0400;
                // Server mode selects the MPQ collision. Never run local object animations/rules.
                obstacles.push_back({EntityId{uint64_t(key.id) + 1}, x - width / 2, y - height / 2,
                    width, height, uint16_t(collision ? mask : 0), light});
            }
        }
        std::optional<OnlineMapInteraction> interaction;
        const int operation = number("OperateFn");
        switch (operation) {
        case 8: case 16: case 18: case 29: case 61: case 66: case 71:
            interaction = OnlineMapInteraction::Door; break;
        case 15: case 34: case 43: case 46: case 70: case 72: case 73:
            interaction = OnlineMapInteraction::Portal; break;
        case 27: interaction = OnlineMapInteraction::TeleportPad; break;
        case 23: interaction = OnlineMapInteraction::Waypoint; break;
        case 32: interaction = OnlineMapInteraction::Stash; break;
        case 44: case 47: case 50: interaction = OnlineMapInteraction::Exit; break;
        default: break;
        }
        if (interaction)
            view_.mapTargets.push_back({key, *unit.position, *interaction,
                std::string(objects_.value(row, "Name")), unit.portalDestination
                    ? std::optional<uint16_t>{*unit.portalDestination} : std::nullopt});
    }
    nativeTerrain_->grid.setObstacles(std::move(obstacles));
    if (v.world.npcConversation) {
        const auto &conversation = *v.world.npcConversation;
        const auto target = std::find_if(view_.mapTargets.begin(), view_.mapTargets.end(),
            [&](const auto &entry) { return entry.unit == OnlineUnitKey{1, conversation.source}; });
        if (target != view_.mapTargets.end()) {
            OnlineNpcDialogView dialog{conversation.source, conversation.revision, target->position,
                std::string(strings_.find(target->name)), {}, {}};
            for (const auto &message : conversation.messages)
                dialog.messages.push_back({message.stringId, message.menu,
                    std::string(strings_.find(message.stringId)), conversation.acknowledged.contains(message.stringId)});
            const auto &unit = v.world.units.at(target->unit);
            const auto id = monsters_.value(monsterRows_.at(*unit.classId), "Id");
            auto rewarded = [&](size_t slot) {
                // D2MOO QuestRecord: native slot*16 + QFLAG_REWARDGRANTED(0).
                return conversation.questFlags.size() > slot * 2 && (conversation.questFlags[slot * 2] & 1);
            };
            const char *label = nullptr;
            if (id == "warriv1" && rewarded(6)) label = "WarrivMenu1b";
            else if (id == "warriv2") label = "WarrivMenu1c";
            else if (id == "meshif1" && rewarded(14)) label = "MeshifMenuEast";
            else if (id == "meshif2") label = "MeshifMenuWest";
            if (label) dialog.travelLabel = strings_.find(label);
            view_.npcConversation = std::move(dialog);
        }
    }
    view_.town = view_.area && catalog_->level(*view_.area).town;
    for (size_t row = 0; row < skills_.rows().size(); ++row) {
        // MPQ identifies the native portal item skills; their IDs are not hardcoded.
        if (skills_.value(row, "skill") != "Scroll of Townportal" &&
            skills_.value(row, "skill") != "Book of Townportal") continue;
        const auto id = skills_.number(row, "Id");
        if (id && *id >= 0 && *id <= UINT16_MAX) view_.townPortalSkills.push_back(uint16_t(*id));
    }
    if (v.world.waypointHistory) {
        for (const auto &[id, level] : catalog_->levels()) {
            if (id < 1 || id > 136 || level.waypoint < 0 || level.waypoint >= 112 ||
                level.act < 0 || level.act > 4) continue;
            const int index = level.waypoint;
            const bool unlocked = ((*v.world.waypointHistory)[1 + index / 16] & (1u << (index & 15))) != 0;
            view_.waypoints.push_back({uint16_t(id), uint8_t(level.act), uint8_t(index),
                level.name, unlocked, view_.area == id});
        }
        std::sort(view_.waypoints.begin(), view_.waypoints.end(), [](const auto &a, const auto &b) {
            return a.number < b.number;
        });
    }
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
        view_.palette = uint8_t(catalog_->level(level).palette);
        if (layout_) {
            const auto &placement = nativeMap_->layout().levels.at(level);
            view_.layoutOrigin = OnlinePoint{uint16_t(placement.x * 5), uint16_t(placement.y * 5)};
        }
        view_.layoutMatched = true; // reveal() validated every server room anchor.
        view_.layoutReason = "Native room anchors match generated area coordinates";
        view_.map = map_->terrain.path;
        view_.collisionVerified = view_.available = view_.nativeMapReady = true;
        view_.movementAvailable = !onlinePlayerDead(world);
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
    return map_->grid.walkable(int(target.x) - origin.x, int(target.y) - origin.y, playerMovement);
}
bool RemoteTown::permitsInteraction(const OnlineView &v, OnlineUnitKey target) const {
    return view_.nativeMapReady && view_.movementAvailable &&
        !onlinePlayerDead(v.world) &&
        gameGeneration_ == v.gameGeneration && areaGeneration_ == v.world.areaGeneration &&
        v.stage == OnlineStage::ProtocolReady &&
        std::any_of(view_.mapTargets.begin(), view_.mapTargets.end(),
            [&](const auto &entry) { return entry.unit == target; });
}
bool RemoteTown::interactionReady(const OnlineView &v, OnlineUnitKey target) const {
    if (!permitsInteraction(v, target) || !v.world.playerPosition) return false;
    const auto found = std::find_if(view_.mapTargets.begin(), view_.mapTargets.end(),
        [&](const auto &entry) { return entry.unit == target; });
    const bool corpse = found->interaction == OnlineMapInteraction::Corpse;
    if (found->interaction != OnlineMapInteraction::Npc && !corpse) return true;
    const auto &player = *v.world.playerPosition;
    return nativeUnitDistance({float(player.x), float(player.y)}, 2,
        {float(found->position.x), float(found->position.y)}, found->collisionWidth) <= (corpse ? 8 : 6);
}
void RemoteTown::revealVisibleTiles(const OnlineView &v, std::span<const size_t> indices) {
    if (!view_.nativeMapReady || !map_ || !v.load.mapSeed || !v.load.act ||
        v.gameGeneration != gameGeneration_ || v.world.areaGeneration != areaGeneration_) return;
    const auto seed = *v.load.mapSeed;
    const int act = *v.load.act;
    const auto &terrain = map_->terrain;
    for (const auto index : indices) {
        if (index >= terrain.instances.size()) continue;
        const auto &instance = terrain.instances[index];
        if (instance.type == 13 || instance.type == 15 || (instance.flags & 8)) continue;
        const auto &room = terrain.rooms.at(instance.room);
        const auto &level = catalog_->level(room.level);
        const auto &placement = nativeMap_->layout().levels.at(room.level);
        const int x = instance.x + view_.origin->x / 5, y = instance.y + view_.origin->y / 5;
        const Discovery key{seed, act, room.level, x, y};
        explored_.insert(key);
        const auto *tile = terrain.tiles.at(size_t(instance.tile));
        MapCell cell{uint32_t((tile->main << 20) | (tile->sub << 8)), instance.type};
        const int cel = automap_.tileCel(level.levelType, cell, x - placement.x, y - placement.y);
        if (cel >= 0) discoveredCels_[key].insert(cel);
    }
    if (view_.area) {
        const auto &level = catalog_->level(*view_.area);
        if (level.town && (level.id == 40 || level.id == 103 || level.id == 109) &&
            !discoveredTowns_.contains({seed, act, level.id})) {
            const auto recipe = nativeMap_->recipe(level.id);
            const auto &placement = nativeMap_->layout().levels.at(level.id);
            discoveredTowns_[{seed, act, level.id}] = {uint16_t(level.id), recipe.variant,
                OnlinePoint{uint16_t(placement.x * 5 + placement.width * 5 / 2),
                    uint16_t(placement.y * 5 + placement.height * 5 / 2)}};
        }
    }
    updateAutomapView(v);
}
void RemoteTown::updateAutomapView(const OnlineView &v) {
    view_.automapStamps.clear(); view_.automapTowns.clear(); view_.automapRevealedCells.clear();
    if (!nativeMap_ || !view_.area || !v.load.mapSeed || !v.load.act) return;
    // Remember visited walking neighbours; stairs and portals keep separate layers.
    std::set<int> component{*view_.area};
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto &[level, slots] : nativeMap_->layout().connections)
            if (component.contains(level))
                for (size_t i = 0; i < slots.visible.size(); ++i)
                    if (slots.visible[i] && slots.warps[i] == -1 &&
                        catalog_->level(slots.visible[i]).act == *v.load.act)
                        changed |= component.insert(slots.visible[i]).second;
    }
    for (const auto &key : explored_) {
        const auto &[seed, act, level, x, y] = key;
        if (seed != *v.load.mapSeed || act != *v.load.act || !component.contains(level)) continue;
        ++view_.automapRevealedCells[uint16_t(level)];
        const auto cels = discoveredCels_.find(key);
        if (cels != discoveredCels_.end())
            for (int cel : cels->second)
                view_.automapStamps.push_back({uint16_t(level), uint16_t(x), uint16_t(y), cel});
    }
    for (const auto &[key, town] : discoveredTowns_) {
        const auto &[seed, act, level] = key;
        if (seed == *v.load.mapSeed && act == *v.load.act && component.contains(level))
            view_.automapTowns.push_back(town);
    }
}
} // namespace d2x
