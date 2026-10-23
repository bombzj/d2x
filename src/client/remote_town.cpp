#include "remote_town.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <utility>
#include "world/interaction_geometry.hpp"
#include "core/fingerprint.hpp"
#include "content/world/object_mode.hpp"

namespace d2x {
namespace {
void insertNativeCell(std::set<NativeAutomapCell> &cells, NativeAutomapCell cell) {
    const int group = nativeAutomapCellGroup(cell.cel);
    for (auto it = cells.lower_bound({0, cell.x, cell.y});
         it != cells.end() && it->x == cell.x && it->y == cell.y; ++it)
        if (group < 0 || nativeAutomapCellGroup(it->cel) == group) return;
    cells.insert(cell);
}
}
RemoteTown::RemoteTown(Archives &a, bool reportAutomapRoomsPd2)
    : archives_(a), libraries_(a), objects_(a.read("data/global/excel/objects.txt")),
      monsters_(a.read("data/global/excel/monstats.txt")),
      monsterSizes_(a.read("data/global/excel/monstats2.txt")),
      skills_(a.read("data/global/excel/skills.txt")), shrines_(a.read("data/global/excel/shrines.txt")),
      petTypes_(a.read("data/global/excel/pettype.txt")), automap_(a), strings_(a),
      reportAutomapRoomsPd2_(reportAutomapRoomsPd2) {
    nativeAutomapCelCount_ = decodeDc6(a.read("data/global/ui/automap/maximap.dc6")).frames.size();
    for (size_t row = 0; row < shrines_.rows().size(); ++row)
        if (auto code = shrines_.number(row, "Code")) shrineRows_.emplace(*code, row);
    for(size_t row=0;row<petTypes_.rows().size();++row)
        if(petTypes_.number(row,"automap").value_or(0))
            if(const auto type=petTypes_.number(row,"idx");type && *type>=0 && *type<=255) automapPetTypes_.insert(uint8_t(*type));
    for (size_t row = 0; row < objects_.rows().size(); ++row)
        if (auto id = objects_.number(row, "Id")) objectRows_.emplace(*id, row);
    for (size_t row = 0; row < monsters_.rows().size(); ++row)
        if (auto id = monsters_.number(row, "hcIdx")) monsterRows_.emplace(*id, row);
    for (size_t row = 0; row < monsterSizes_.rows().size(); ++row)
        monsterSizeRows_.emplace(std::string(monsterSizes_.value(row, "Id")), row);
}
void RemoteTown::update(const OnlineView &v, std::optional<Vec> displayPosition) {
    automapPlayerPresentPd2_ = v.load.playerUnitId && v.world.units.contains({0, *v.load.playerUnitId});
    std::optional<OnlinePoint> point;
    if (displayPosition && std::isfinite(displayPosition->x) && std::isfinite(displayPosition->y) &&
        displayPosition->x >= 0 && displayPosition->y >= 0 && displayPosition->x <= UINT16_MAX && displayPosition->y <= UINT16_MAX)
        point = OnlinePoint{uint16_t(std::lround(displayPosition->x)), uint16_t(std::lround(displayPosition->y))};
    const bool observerChanged = point != displayPlayerPosition_;
    displayPlayerPosition_ = point;
    if (gameGeneration_ != v.gameGeneration || areaGeneration_ != v.world.areaGeneration) {
        if (gameGeneration_ != v.gameGeneration) {
            automapPositionPd2_ = {};
            explored_.clear(); discoveredCels_.clear(); discoveredTowns_.clear(); preparedTownAutomaps_.clear(); actSeeds_.clear();
            nativeAutomapCells_.clear();
        }
        gameGeneration_ = v.gameGeneration;
        areaGeneration_ = v.world.areaGeneration;
        automapRoomCallbacksPd2_.clear();
        revision_ = ~uint64_t{};
        map_ = nullptr;
        nativeMap_.reset();
        nativeTerrain_.reset();
        nativeTerrainDirty_ = true;
        nativePlayerRoom_.reset();
        unitRooms_.clear();
        nativePlayerPosition_.reset();
        nativeSequence_ = 0;
        nativeLoadComplete_ = false;
        nextNativeUpdateMilliseconds_ = 0;
        nextObjectTransition_.reset();
        nativeErrors_.clear();
        nativeReason_.clear();
        catalog_.reset();
        layout_.reset();
        view_ = {};
        if (v.load.act && v.load.townArea && *v.load.act < actTownLevels.size() &&
            *v.load.townArea == actTownLevels[*v.load.act]) {
            try {
                catalog_ = sharedWorldCatalog(archives_, v.load.difficulty.value_or(0));
                const auto &catalog = *catalog_;
                const auto &town = catalog.level(*v.load.townArea);
                if (v.load.mapSeed) {
                    const int act=*v.load.act;
                    if(actSeeds_.contains(act) && actSeeds_.at(act)!=*v.load.mapSeed) {
                        auto layers=explored_.layers();
                        std::erase_if(layers,[&](const auto &entry){return catalog_->level(int(entry.first)).act==act;});
                        explored_.restore(std::move(layers));
                        std::erase_if(discoveredCels_,[&](const auto &entry){return std::get<1>(entry.first)==act;});
                        std::erase_if(discoveredTowns_,[&](const auto &entry){return std::get<1>(entry.first)==act;});
                        std::erase_if(preparedTownAutomaps_,[&](const auto &entry){return std::get<1>(entry)==act;});
                    }
                    actSeeds_[act]=*v.load.mapSeed;
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
                        if (reportAutomapRoomsPd2_) nativeMap_->observeRoomInitialization([this] {
                            if (automapPlayerPresentPd2_) record_automap_room_pd2();
                        });
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
    const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    const bool pendingNative = nativeMap_ && nativeReason_.empty() &&
        (nativeTerrainDirty_ || nativeSequence_ != v.world.mapEventSequence ||
         (nativeLoadComplete_ && now >= nextNativeUpdateMilliseconds_));
    if (revision_ == v.revision && !observerChanged && !pendingNative &&
        (!nextObjectTransition_ || now < *nextObjectTransition_))
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
    view_.automapStamps.clear(); view_.automapTowns.clear(); view_.automapRevealedCells.clear(); view_.automap = {};
    if (updateNative(v)) {
        updateUnitRooms(v);
        updateMapTargets(v);
        try {
            prepareTownAutomap(v);
            explored_.reveal(automapScene(v), {});
        } catch (const std::exception &e) {
            view_.mapErrors[*view_.area] = "Town automap: " + std::string(e.what());
        }
        updateAutomapView(v);
        return;
    }
    map_ = nullptr;
    view_.reason = nativeReason_.empty() ? "Waiting for native room assignments and player position" : nativeReason_;
    view_.nativeMapReason = view_.reason;
    for (const auto &[level, reason] : nativeErrors_) view_.mapErrors[level] = reason;
}
void RemoteTown::updateUnitRooms(const OnlineView &v) {
    std::erase_if(unitRooms_,[&](const auto &entry) {return !v.world.units.contains(entry.first);});
    for(const auto &[key,unit]:v.world.units) {
        const auto room=unit.position?nativeMap_->clientRoom(unit.position->x,unit.position->y):std::nullopt;
        if(!room || !nativeMap_->tiles().rooms().at(*room).loaded) {unitRooms_.erase(key);continue;}
        const auto found=unitRooms_.find(key);
        if(found==unitRooms_.end() || found->second.room!=*room || found->second.assignment!=unit.assignmentRevision)
            unitRooms_[key]={*room,unit.assignmentRevision,std::max(unit.assignmentRevision,unit.positionRevision)};
    }
}
std::vector<OnlineUnitKey> RemoteTown::nearbyUnits(const OnlineView &v,OnlinePoint center,int radius) const {
    std::vector<OnlineUnitKey> result;
    if(!view_.nativeMapReady || !nativeMap_ || !v.world.playerPosition || radius<0 ||
       gameGeneration_!=v.gameGeneration || areaGeneration_!=v.world.areaGeneration) return result;
    const auto player=v.world.playerPosition;
    const auto root=nativeMap_->clientRoom(player->x,player->y);
    if(!root) return result;
    const auto &rooms=nativeMap_->tiles().rooms();
    const auto &bounds=rooms.at(*root).room;
    std::vector<size_t> search{*root};
    // UNITFINDS_FindAllMatchingUnitsInNeighboredRooms uses the player's
    // room unless the query reaches its boundary, then its activeNear list.
    if(int(center.x)-radius<=bounds.x*5 || int(center.y)-radius<=bounds.y*5 ||
       int(center.x)+radius>=(bounds.x+bounds.width)*5 || int(center.y)+radius>=(bounds.y+bounds.height)*5)
        search=rooms.at(*root).activeNear;
    for(const auto room:search) {
        if(!rooms.at(room).loaded || catalog_->level(rooms.at(room).level).town) continue;
        std::vector<OnlineUnitKey> members;
        for(const auto &[key,entry]:unitRooms_) {
            if(entry.room!=room) continue;
            const auto found=v.world.units.find(key);
            if(found==v.world.units.end() || !found->second.position) continue;
            const auto point=*found->second.position;
            const int64_t dx=int(point.x)-int(center.x),dy=int(point.y)-int(center.y);
            if(dx*dx+dy*dy<=int64_t(radius)*radius) members.push_back(key);
        }
        // UNITROOM_AddUnitToRoomEx prepends. Movement inside the same room
        // leaves its order intact; reassignment or a room crossing reinserts.
        std::stable_sort(members.begin(),members.end(),[&](auto a,auto b) {
            return unitRooms_.at(a).entered>unitRooms_.at(b).entered;
        });
        result.insert(result.end(),members.begin(),members.end());
    }
    return result;
}
void RemoteTown::updateMapTargets(const OnlineView &v) {
    nextObjectTransition_.reset();
    const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    view_.town = view_.area && catalog_->level(*view_.area).town;
    std::vector<Grid::Obstacle> obstacles;
    for (const auto &[key, unit] : v.world.units) {
        if (!unit.position || !unit.classId || !view_.origin) continue;
        const int x = int(unit.position->x) - view_.origin->x;
        const int y = int(unit.position->y) - view_.origin->y;
        if (x < 0 || y < 0 || x >= view_.width || y >= view_.height) continue;
        if (key.type == 0) {
            const auto owner = v.world.corpseOwners.find(key.id);
            if (v.load.playerUnitId && onlineCanLootCorpse(v.world, *v.load.playerUnitId, key.id)) {
                const auto label = strings_.find("corpse");
                view_.mapTargets.push_back({key, *unit.position, OnlineMapInteraction::Corpse,
                    unit.name + " " + std::string(label), {}, playerMovement.size, playerMovement.size});
            } else if (view_.town && key.id != v.load.playerUnitId && owner == v.world.corpseOwners.end() &&
                       unit.mode != 0 && unit.mode != 17) {
                view_.mapTargets.push_back({key, *unit.position, OnlineMapInteraction::PlayerTrade,
                    unit.name, {}, playerMovement.size, playerMovement.size});
            }
            continue;
        }
        if (key.type == 1) {
            const auto record = monsterRows_.find(*unit.classId);
            if (record == monsterRows_.end() || !monsters_.number(record->second, "interact").value_or(0) ||
                onlineMonsterCorpse(unit)) continue;
            const auto size = monsterSizeRows_.find(monsters_.value(record->second, "MonStatsEx"));
            if (size == monsterSizeRows_.end()) continue;
            const int width = monsterSizes_.number(size->second, "SizeX").value_or(0);
            const int height = monsterSizes_.number(size->second, "SizeY").value_or(0);
            if (width <= 0 || height <= 0) continue;
            const auto nameKey = monsters_.value(record->second, "NameStr");
            const auto name = strings_.find(nameKey);
            // Placeholder rows keep the target clickable but carry no display
            // name; only a genuinely missing key falls back to the raw key.
            view_.mapTargets.push_back({key, *unit.position, OnlineMapInteraction::Npc,
                strings_.isPlaceholder(name) ? std::string{} : std::string(name.empty() ? nameKey : name),
                {}, width, height});
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
        const float age = unit.actionReceivedMilliseconds && now >= unit.actionReceivedMilliseconds
            ? float(now - unit.actionReceivedMilliseconds) / 1000.f : 0.f;
        const int mode = objectDisplayMode(objects_, row, unit.mode.value_or(0), age);
        if (const auto end = objectEndAnimation(objects_, row, unit.mode.value_or(0));
            end && age < *end && unit.actionReceivedMilliseconds) {
            const uint64_t deadline = unit.actionReceivedMilliseconds + uint64_t(std::ceil(*end * 1000.f));
            if (!nextObjectTransition_ || deadline < *nextObjectTransition_) nextObjectTransition_ = deadline;
        }
        if (unit.mode && *unit.mode < 8) {
            const auto suffix = std::to_string(mode);
            const bool collision = number("HasCollision" + suffix) != 0;
            const int width = number("SizeX"), height = number("SizeY");
            if (collision && width > 0 && height > 0) {
                const bool door = number("IsDoor") != 0, missile = number("BlockMissile") != 0;
                const uint16_t mask = door ? (number("BlocksVis") ? 0x0806 : missile ? 0x0804 : 0x0400)
                    : (number("SubClass") & 4) ? 0x8000 : missile ? 0x0404 : 0x0400;
                // Include the silent native ENDANIM transition. MPQ supplies the
                // opened collision; server mode and all operation results stay unchanged.
                obstacles.push_back({EntityId{uint64_t(key.id) + 1}, x - width / 2, y - height / 2,
                    width, height, mask, (mask & 0x22) != 0});
            }
        }
        const int operation = number("OperateFn");
        if(operation==43 && *unit.classId==100 && mode==2 && nativeTerrain_) nativeTerrain_->openTombWall({float(x),float(y)});
        // ObjMode::OperateFunction23 accepts both operating/opened waypoints.
        // Selectable1 is clear in MPQ, but the native handler still opens the
        // menu in mode 1 or 2. Targetability does not override that exception.
        const bool selectable=unit.mode && *unit.mode<8 &&
            (number("Selectable"+std::to_string(mode))!=0 ||
             (operation==23 && (mode==1 || mode==2)));
        if (!selectable || (operation!=23 && unit.objectTargetable==false)) continue;
        std::optional<OnlineMapInteraction> interaction;
        switch (operation) {
        case 8: case 16: case 18: case 29: case 61: case 66: case 71:
            interaction = OnlineMapInteraction::Door; break;
        case 15: case 34: case 43: case 46: case 70: case 72: case 73:
            interaction = OnlineMapInteraction::Portal; break;
        case 27: interaction = OnlineMapInteraction::TeleportPad; break;
        case 23: interaction = OnlineMapInteraction::Waypoint; break;
        case 32: interaction = OnlineMapInteraction::Stash; break;
        case 44: case 47: case 50: interaction = OnlineMapInteraction::Exit; break;
        default: if (operation > 0) interaction = OnlineMapInteraction::Object; break;
        }
        if (interaction) {
            const auto keyName = objects_.value(row, "Name");
            auto label = strings_.find(keyName);
            if (operation == 4 && (unit.objectInteractType.value_or(0) & 0x80)) {
                const auto locked = strings_.find("lockedchest");
                if (!locked.empty()) label = locked;
            }
            if (operation == 2 && unit.objectInteractType && shrineRows_.contains(*unit.objectInteractType)) {
                const auto shrine = strings_.find("ShrId" + std::to_string(*unit.objectInteractType));
                if (!shrine.empty()) label = shrine;
            }
            view_.mapTargets.push_back({key, *unit.position, *interaction,
                std::string(label.empty() ? keyName : label), unit.portalDestination
                    ? std::optional<uint16_t>{*unit.portalDestination} : std::nullopt,
                number("SizeX"), number("SizeY")});
        }
    }
    nativeTerrain_->grid.setObstacles(std::move(obstacles));
    if (v.world.npcConversation) {
        const auto &conversation = *v.world.npcConversation;
        const auto target = std::find_if(view_.mapTargets.begin(), view_.mapTargets.end(),
            [&](const auto &entry) { return entry.unit == OnlineUnitKey{conversation.type, conversation.source}; });
        if (target != view_.mapTargets.end()) {
            OnlineNpcDialogView dialog{};
            dialog.source = conversation.source;
            dialog.revision = conversation.revision;
            dialog.position = target->position;
            dialog.speaker = strings_.find(target->name);
            dialog.type=conversation.type;
            for (const auto &message : conversation.messages)
                dialog.messages.push_back({message.stringId, message.menu,
                    std::string(strings_.speech(message.stringId)), conversation.acknowledged.contains(message.stringId)});
            const auto &unit = v.world.units.at(target->unit);
            const auto id = conversation.type==1?monsters_.value(monsterRows_.at(*unit.classId), "Id"):std::string_view{};
            auto rewarded = [&](size_t slot) {
                // D2MOO QuestRecord: native slot*16 + QFLAG_REWARDGRANTED(0).
                return v.world.quests.playerFlags && slot < v.world.quests.playerFlags->size() &&
                    ((*v.world.quests.playerFlags)[slot] & 1);
            };
            const char *label = nullptr;
            if (id == "warriv1" && v.world.quests.playerFlags && ((*v.world.quests.playerFlags)[6]&3u)) {label = "WarrivMenu1b";dialog.travelDestination=40;}
            else if (id == "warriv2") {label = "WarrivMenu1c";dialog.travelDestination=1;}
            else if (id == "meshif1" && rewarded(14)) {label = "MeshifMenuEast";dialog.travelDestination=75;}
            else if (id == "meshif2") {label = "MeshifMenuWest";dialog.travelDestination=40;}
            if (label) dialog.travelLabel = strings_.find(label);
            // Original 1.13c Akara menu: native quest slot 41, pending but unused.
            if(id=="akara" && v.world.quests.playerFlags && ((*v.world.quests.playerFlags)[41]&3u)==2u)
                dialog.respecLabel=strings_.speech(0x2ba0);
            view_.npcConversation = std::move(dialog);
        }
    }
    for (size_t row = 0; row < skills_.rows().size(); ++row) {
        // MPQ identifies the native portal item skills; their IDs are not hardcoded.
        if (skills_.value(row, "skill") != "Scroll of Townportal" &&
            skills_.value(row, "skill") != "Book of Townportal" &&
            !(v.protocolProfile == "pd2-cn-normal-20261020" &&
                skills_.value(row, "skill") == "Book of Unlimited Townportal" &&
                skills_.number(row, "InGame").value_or(0) == 1 &&
                skills_.number(row, "srvdofunc").value_or(0) == 113 &&
                skills_.number(row, "scroll").value_or(0) == 1)) continue;
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
    const auto started = std::chrono::steady_clock::now();
    int eventLevel = 0;
    try {
        const auto &world = v.world;
        if (!nativeSequence_ && !nativePlayerPosition_)
            nativePlayerPosition_ = world.mapInitialPlayerPosition;
        bool changed = false;
        const auto terrainRevision = nativeMap_->terrainRevision();
        bool deferred = false;
        const auto deadline = started + std::chrono::milliseconds(4);
        const auto selectRoom = [&](size_t room) {
            // The snapshot contains all active rooms in the level's walking
            // component. Crossing a room within that level does not change it.
            changed |= !nativePlayerRoom_ || nativeMap_->tiles().rooms().at(*nativePlayerRoom_).level !=
                nativeMap_->tiles().rooms().at(room).level;
            nativePlayerRoom_ = room;
        };
        auto locatePlayerRoom = [&] {
            if (!nativePlayerPosition_) return;
            const auto room = nativeMap_->clientRoom(nativePlayerPosition_->x, nativePlayerPosition_->y);
            if (!room || !nativeMap_->tiles().rooms().at(*room).loaded) return;
            // D2Client 07/08 own the in-sight references. Clients::sub_6FC33020
            // changes CLIENT_IN_ROOM references on the server; repeating that
            // operation here lets a later 08 consume a propagated reference.
            // Coordinates select the snapshot, without acquiring another root.
            selectRoom(*room);
        };
        for (const auto &event : world.mapEvents) {
            if (event.sequence <= nativeSequence_) continue;
            if (event.sequence != nativeSequence_ + 1)
                throw std::runtime_error("Native map event history was lost; rejoin the game");
            eventLevel = event.level;
            switch (event.kind) {
            case OnlineMapEvent::Kind::RevealRoom:
                nativeMap_->reveal(event.level, event.point.x, event.point.y);
                locatePlayerRoom();
                break;
            case OnlineMapEvent::Kind::HideRoom:
                nativeMap_->hide(event.level, event.point.x, event.point.y);
                break;
            case OnlineMapEvent::Kind::PlayerPosition:
                nativePlayerPosition_ = event.point;
                locatePlayerRoom();
                break;
            case OnlineMapEvent::Kind::RemovePlayer:
                changed = true;
                nativePlayerRoom_.reset();
                nativePlayerPosition_.reset();
                break;
            case OnlineMapEvent::Kind::LoadComplete: {
                // Retail 1.13c Rcv04 (D2Client RVA ADFE0) requires the local
                // player to be attached to a room. Consume the preceding map
                // events first, including when reconstruction spans frames.
                const auto room=nativePlayerPosition_?nativeMap_->clientRoom(nativePlayerPosition_->x,nativePlayerPosition_->y):std::nullopt;
                if(!room || !nativeMap_->tiles().rooms().at(*room).loaded)
                    throw std::runtime_error("Server load complete preceded the local player's active room");
                selectRoom(*room);nativeLoadComplete_=true;
                break;
            }
            }
            nativeSequence_ = event.sequence;
            if (nativeSequence_ != world.mapEventSequence && std::chrono::steady_clock::now() >= deadline) {
                deferred = true; break;
            }
        }
        if (!deferred && nativeSequence_ != world.mapEventSequence)
            throw std::runtime_error("Native map event tail is unavailable; rejoin the game");
        const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        if (!deferred && nativeLoadComplete_ && now >= nextNativeUpdateMilliseconds_) {
            nativeMap_->updateClient();
            nextNativeUpdateMilliseconds_ = now + 40;
        }
        if (displayPlayerPosition_ && world.playerPosition) {
            if (const auto room = nativeMap_->clientRoom(displayPlayerPosition_->x, displayPlayerPosition_->y)) {
                const auto &prepared = nativeMap_->tiles().rooms().at(*room);
                // Presentation selects only a room already assigned by 07.
                // No new room interest or terrain is acquired by prediction.
                if (prepared.loaded && world.rooms.contains({uint8_t(prepared.level), uint16_t(prepared.room.x), uint16_t(prepared.room.y)})) {
                    selectRoom(*room);
                }
            }
        } else locatePlayerRoom();
        nativeTerrainDirty_ |= changed || nativeMap_->terrainRevision() != terrainRevision;
        if (!nativePlayerRoom_ || v.stage != OnlineStage::ProtocolReady ||
            !v.load.serverLoadComplete || !nativeLoadComplete_ || !world.playerPosition) return false;
        if (!nativeMap_->tiles().rooms().at(*nativePlayerRoom_).loaded) return false;
        const int level = nativeMap_->tiles().rooms().at(*nativePlayerRoom_).level;
        if (nativeTerrainDirty_ || !nativeTerrain_) {
            auto snapshot = nativeMap_->snapshot(level);
            snapshot.map.grid.fullTerrainCollision = std::move(snapshot.collision);
            view_.origin = OnlinePoint{uint16_t(snapshot.tileX * 5), uint16_t(snapshot.tileY * 5)};
            view_.width = snapshot.map.grid.width; view_.height = snapshot.map.grid.height;
            auto map = std::make_unique<Map>(std::move(snapshot.map));
            nativeTerrain_.swap(map);
            nativeTerrainDirty_ = false;
            for (int cached : snapshot.levels)
                if (std::find(view_.cachedAreas.begin(), view_.cachedAreas.end(), cached) == view_.cachedAreas.end())
                    view_.cachedAreas.push_back(uint16_t(cached));
        }
        map_ = nativeTerrain_.get();
        view_.area = uint16_t(level);
        view_.automapLayer = uint32_t(catalog_->level(level).automapLayer);
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
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        // Report work exceeding one 25 Hz simulation tick, not every movement.
        if (elapsed >= 40) std::clog << "[area-load] client level=" << level << " map_ms=" << elapsed << '\n';
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
    if (!inside(target) || !inside(displayPlayerPosition_.value_or(*v.world.playerPosition)))
        return false;
    // Original coordinate movement submits the pointer even over a wall;
    // native Path prepares/clips the reachable target. Reject absent terrain,
    // not every blocked destination, or dragging across scenery drops input.
    return map_->grid.movementMask(int(target.x)-origin.x,int(target.y)-origin.y) != 0xffff;
}
bool RemoteTown::permitsInteraction(const OnlineView &v, OnlineUnitKey target) const {
    return view_.nativeMapReady && view_.movementAvailable &&
        !onlinePlayerDead(v.world) &&
        gameGeneration_ == v.gameGeneration && areaGeneration_ == v.world.areaGeneration &&
        v.stage == OnlineStage::ProtocolReady &&
        std::any_of(view_.mapTargets.begin(), view_.mapTargets.end(),
            [&](const auto &entry) { return entry.unit == target; });
}
bool RemoteTown::interactionReady(const OnlineView &v, OnlineUnitKey target, std::optional<Vec> displayOrigin, bool approaching) const {
    if (!permitsInteraction(v, target) || !v.world.playerPosition) return false;
    const auto found = std::find_if(view_.mapTargets.begin(), view_.mapTargets.end(),
        [&](const auto &entry) { return entry.unit == target; });
    const bool corpse = found->interaction == OnlineMapInteraction::Corpse;
    const bool tradePeer = found->interaction == OnlineMapInteraction::PlayerTrade;
    const auto &player = *v.world.playerPosition;
    const Vec observer = displayOrigin.value_or(Vec{float(player.x), float(player.y)});
    if (target.type == 2) {
        if (!map_ || !view_.origin) return false;
        const Vec origin{float(view_.origin->x), float(view_.origin->y)};
        const Vec position{float(found->position.x) - origin.x, float(found->position.y) - origin.y};
        return interactionClear(map_->grid, observer - origin,
            {EntityId{uint64_t(target.id) + 1}, position, position, found->collisionWidth, found->collisionHeight, 0, true});
    }
    if (target.type == 5) return nativeUnitDistance(observer, 2, {float(found->position.x), float(found->position.y)}, found->collisionWidth) <= 4;
    if (found->interaction != OnlineMapInteraction::Npc && !corpse && !tradePeer) return true;
    return nativeUnitDistance(observer, 2,
        {float(found->position.x), float(found->position.y)}, found->collisionWidth) <= (approaching ? 0 : corpse || tradePeer ? 8 : 6);
}
void RemoteTown::prepareTownAutomap(const OnlineView &v) {
    if (!view_.nativeMapReady || !view_.area || !v.load.mapSeed || !v.load.act) return;
    const auto &level = catalog_->level(*view_.area);
    if (!level.town) return;
    const auto seed = *v.load.mapSeed;
    const int act = *v.load.act;
    const auto key = std::tuple{seed, act, level.id};
    if (preparedTownAutomaps_.contains(key)) return;
    const auto &placement = nativeMap_->layout().levels.at(level.id);
    if (level.id == 40 || level.id == 103 || level.id == 109) {
        const auto recipe = nativeMap_->recipe(level.id);
        discoveredTowns_[key] = {uint16_t(level.id), recipe.variant,
            OnlinePoint{uint16_t(placement.x * 5 + placement.width * 5 / 2),
                uint16_t(placement.y * 5 + placement.height * 5 / 2)}};
    } else {
        // DRLGPRESET_InitLevel initializes every AutoMap preset room before its
        // callback. Read those original tiles without changing room sight refs
        // or revealing the adjoining wilderness through the camera viewport.
        const auto snapshot = nativeMap_->snapshot(level.id, false);
        const auto &terrain = snapshot.map.terrain;
        for (const auto &instance : terrain.instances) {
            if (instance.type == 13 || instance.type == 15 || (instance.flags & 8)) continue;
            const auto *tile = terrain.tiles.at(size_t(instance.tile));
            rememberTile(v, level.id, snapshot.tileX + instance.x, snapshot.tileY + instance.y,
                instance.type, instance.wallArray, {uint32_t((tile->main << 20) | (tile->sub << 8)), instance.type});
        }
        for (const auto &stamp : automap_.stamps(terrain.data, level.levelType))
            discoveredCels_[{seed, act, level.id, snapshot.tileX + stamp.x, snapshot.tileY + stamp.y}].insert(stamp.cel);
    }
    preparedTownAutomaps_.insert(key);
}
MapSceneView RemoteTown::automapScene(const OnlineView &v) const {
    MapSceneView scene;
    if (!map_ || !view_.area || !v.load.mapSeed || !v.load.act || !view_.origin) return scene;
    scene.act = *v.load.act; scene.region = RegionId(*view_.area);
    std::set<int> levels;
    for (const auto &room : map_->terrain.rooms) levels.insert(room.level);
    for (int id : levels) {
        const auto &placement = nativeMap_->layout().levels.at(id);
        MapRegionView region;
        region.id = RegionId(id); region.width = placement.width + 1; region.height = placement.height + 1;
        region.safe = catalog_->level(id).town;
        Fingerprint fingerprint;
        fingerprint.add(std::to_string(*v.load.mapSeed)); fingerprint.add(std::to_string(*v.load.act));
        fingerprint.add(std::to_string(id)); fingerprint.add(std::to_string(placement.x)); fingerprint.add(std::to_string(placement.y));
        region.layoutFingerprint = fingerprint.value();
        for (const auto &room : map_->terrain.rooms) if (room.level == id)
            region.revealRooms.push_back({(room.x + view_.origin->x / 5 - placement.x) * 5,
                (room.y + view_.origin->y / 5 - placement.y) * 5, room.width * 5, room.height * 5});
        const int slot = int(scene.regions.size());
        if (id == *view_.area) {
            scene.current = slot; scene.hasObserverRoom = true;
            if (v.world.playerPosition) scene.observer = {float(int(v.world.playerPosition->x) - placement.x * 5),
                float(int(v.world.playerPosition->y) - placement.y * 5)};
        }
        scene.automapRegions.emplace_back(slot, Vec{});
        scene.regions.push_back(std::move(region));
    }
    return scene;
}
void RemoteTown::rememberTile(const OnlineView &v, int levelId, int x, int y, int type, bool wallArray, const MapCell &cell) {
    const auto &level = catalog_->level(levelId);
    if (level.automapLayer < 0 || level.automapLayer >= 100 || !v.load.mapSeed || !v.load.secondarySeed) return;
    const auto &placement = nativeMap_->layout().levels.at(levelId);
    // D2Client 0x61880: GameTileToClientCoords / 10; lower walls (16+)
    // receive the native 24-pixel vertical adjustment before serialization.
    const int px = (x-y)*8, py = (x+y)*4 + (type >= 16 ? 24 : 0);
    if (px < INT16_MIN || px > INT16_MAX || py < INT16_MIN || py > INT16_MAX)
        throw std::runtime_error("Automap cell outside native coordinate range");
    auto &cells = nativeAutomapCells_[{*v.load.mapSeed, uint32_t(level.automapLayer), *v.load.secondarySeed}];
    for (const int cel : automap_.cellCels(level.levelType, cell, x-placement.x, y-placement.y))
        if (cel >= 0 && size_t(cel) < nativeAutomapCelCount_ && cel <= INT16_MAX)
            insertNativeCell(cells[wallArray ? 1 : 0], {uint16_t(cel), int16_t(px), int16_t(py)});
}
void RemoteTown::record_automap_room_pd2() {
    automapRoomCallbacksPd2_.push_back(uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count()));
}
void RemoteTown::refresh_automap_rooms_pd2(const OnlineView &v) {
    if (!reportAutomapRoomsPd2_ || !nativeMap_ || !nativePlayerRoom_ || !v.load.playerUnitId) return;
    const auto position = displayPlayerPosition_ ? displayPlayerPosition_ : v.world.playerPosition;
    if (!position) return;
    const int dx = std::abs(int(position->x) - automapPositionPd2_.x);
    const int dy = std::abs(int(position->y) - automapPositionPd2_.y);
    // D2Client RVA 0x62AF0: max(delta) + floor(min(delta)/2), threshold 80.
    // This is a room-reveal refresh, independent of frame rate or move packets.
    if (std::max(dx, dy) + std::min(dx, dy) / 2 < 80) return;
    automapPositionPd2_ = *position;
    const auto &rooms = nativeMap_->tiles().rooms();
    const auto &current = rooms.at(*nativePlayerRoom_);
    const auto layer = catalog_->level(current.level).automapLayer;
    for (const auto index : current.activeNear) {
        const auto &room = rooms.at(index);
        if (room.loaded && catalog_->level(room.level).automapLayer == layer) record_automap_room_pd2();
    }
}
std::vector<uint32_t> RemoteTown::take_automap_room_reveals_pd2() {
    return std::exchange(automapRoomCallbacksPd2_, {});
}
void RemoteTown::revealVisibleTiles(const OnlineView &v, std::span<const size_t> indices) {
    if (!view_.nativeMapReady || !map_ || !v.load.mapSeed || !v.load.act ||
        v.gameGeneration != gameGeneration_ || v.world.areaGeneration != areaGeneration_) return;
    const auto seed = *v.load.mapSeed;
    const int act = *v.load.act;
    const auto &terrain = map_->terrain;
    const auto scene = automapScene(v);
    std::vector<AutomapVisibleCell> visible;
    for (const auto index : indices) {
        if (index >= terrain.instances.size()) continue;
        const auto &instance = terrain.instances[index];
        if (instance.type == 13 || instance.type == 15 || (instance.flags & 8)) continue;
        const auto &room = terrain.rooms.at(instance.room);
        const auto &level = catalog_->level(room.level);
        const auto &placement = nativeMap_->layout().levels.at(room.level);
        const int x = instance.x + view_.origin->x / 5, y = instance.y + view_.origin->y / 5;
        const Discovery key{seed, act, room.level, x, y};
        const auto region = std::find_if(scene.regions.begin(), scene.regions.end(),
            [&](const auto &entry) { return entry.id == RegionId(room.level); });
        if (region != scene.regions.end())
            visible.push_back({int(region - scene.regions.begin()), x - placement.x, y - placement.y});
        const auto *tile = terrain.tiles.at(size_t(instance.tile));
        MapCell cell{uint32_t((tile->main << 20) | (tile->sub << 8)), instance.type};
        rememberTile(v, room.level, x, y, instance.type, instance.wallArray, cell);
        for (int cel : automap_.cellCels(level.levelType, cell, x - placement.x, y - placement.y))
            discoveredCels_[key].insert(cel);
    }
    explored_.reveal(scene, visible);
    refresh_automap_rooms_pd2(v);
    updateAutomapView(v);
}
void RemoteTown::revealAllTiles(const OnlineView &v) {
    if (!view_.nativeMapReady || !map_ || !v.load.mapSeed || !v.load.act ||
        v.gameGeneration != gameGeneration_ || v.world.areaGeneration != areaGeneration_) return;
    std::vector<size_t> all(map_->terrain.instances.size());
    for (size_t i = 0; i < all.size(); ++i) all[i] = i;
    revealVisibleTiles(v, all);
}
void RemoteTown::updateAutomapView(const OnlineView &v) {
    view_.automapStamps.clear(); view_.automapTowns.clear(); view_.automapRevealedCells.clear(); view_.automap = {};
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
    for (const auto &[id, layer] : explored_.layers()) {
        const int level = int(id);
        if (!component.contains(level) || !nativeMap_->layout().levels.contains(level)) continue;
        const auto &placement = nativeMap_->layout().levels.at(level);
        Fingerprint fingerprint;
        fingerprint.add(std::to_string(*v.load.mapSeed)); fingerprint.add(std::to_string(*v.load.act));
        fingerprint.add(std::to_string(level)); fingerprint.add(std::to_string(placement.x)); fingerprint.add(std::to_string(placement.y));
        if(layer.width!=placement.width+1 || layer.height!=placement.height+1 || layer.layoutFingerprint!=fingerprint.value()) continue;
        for (int y = 0; y < layer.height; ++y)
            for (int x = 0; x < layer.width; ++x) {
                if (!layer.seen[size_t(y) * layer.width + x]) continue;
                ++view_.automapRevealedCells[uint16_t(level)];
                const auto cels = discoveredCels_.find({*v.load.mapSeed, *v.load.act, level, x + placement.x, y + placement.y});
                if (cels != discoveredCels_.end()) for (int cel : cels->second)
                    view_.automapStamps.push_back({uint16_t(level), uint16_t(x + placement.x), uint16_t(y + placement.y), cel});
            }
    }
    for (const auto &[key, town] : discoveredTowns_) {
        const auto &[seed, act, level] = key;
        if (seed == *v.load.mapSeed && act == *v.load.act && component.contains(level))
            view_.automapTowns.push_back(town);
    }
    if (v.world.playerPosition) view_.automap.observer = {float(v.world.playerPosition->x), float(v.world.playerPosition->y)};
    view_.automap.observerVisible=!onlinePlayerDead(v.world);
    for (const auto &[key, groups] : nativeAutomapCells_) {
        const auto &[seed, layer, objectSeed] = key;
        if (seed != *v.load.mapSeed || layer != uint32_t(catalog_->level(*view_.area).automapLayer)) continue;
        for (size_t group = 0; group < groups.size(); ++group) {
            if (group == 2 && objectSeed != v.load.secondarySeed) continue;
            for (const auto &cell : groups[group])
                view_.automap.stamps.push_back({unproject({cell.x * 10.f, cell.y * 10.f}), cell.cel});
        }
    }
    for (const auto &town : view_.automapTowns)
        view_.automap.towns.push_back({town.level, town.variant, {float(town.center.x), float(town.center.y)}});
    using Mark = AutomapDrawView::UnitMark;
    const auto self = v.load.playerUnitId ? v.world.social.players.find(*v.load.playerUnitId) : v.world.social.players.end();
    const auto inParty=[&](uint32_t id) {
        const auto p=v.world.social.players.find(id);
        return self!=v.world.social.players.end() && self->second.partyId && *self->second.partyId!=UINT16_MAX &&
            p!=v.world.social.players.end() && p->second.partyId==self->second.partyId;
    };
    std::set<uint32_t> drawnPlayers;
    for (const auto &[key, unit] : v.world.units) {
        if (!unit.position || !unit.classId || key.type > 2) continue;
        if(key.type==0) {
            const auto corpse=v.world.corpseOwners.find(key.id);
            if(corpse!=v.world.corpseOwners.end()) {
                // Original corpse inventory/owner admission identifies one's body;
                // another player's corpse is not a public purple marker.
                if(corpse->second==v.load.playerUnitId)
                    view_.automap.markers.push_back({{float(unit.position->x),float(unit.position->y)},-1,unit.name,false,true,Mark::Corpse,std::pair{key.type,key.id}});
            } else if(key.id!=v.load.playerUnitId && unit.mode!=(unit.nativeMode?0:8) && unit.mode!=(unit.nativeMode?17:9)) {
                const auto roster=v.world.social.players.find(key.id);
                const auto &name=roster==v.world.social.players.end()?unit.name:roster->second.name;
                view_.automap.markers.push_back({{float(unit.position->x),float(unit.position->y)},-1,name,false,true,inParty(key.id)?Mark::Party:Mark::OtherPlayer,std::pair{key.type,key.id}});
                drawnPlayers.insert(key.id);
            }
            continue;
        }
        if(key.type==1 && (unit.mode==0 || unit.mode==12)) continue;
        bool seen = false;
        for (const auto &[id, layer] : explored_.layers()) {
            if (!component.contains(int(id)) || !nativeMap_->layout().levels.contains(int(id))) continue;
            const auto &placement = nativeMap_->layout().levels.at(int(id));
            const int x = int(unit.position->x) / 5 - placement.x, y = int(unit.position->y) / 5 - placement.y;
            if (x >= 0 && y >= 0 && x < layer.width && y < layer.height && layer.seen[size_t(y) * layer.width + x]) seen = true;
        }
        int cel = -1; std::string name;
        if (key.type == 2) cel = automap_.objectCel(*unit.classId);
        else if (const auto row = monsterRows_.find(*unit.classId); row != monsterRows_.end()) {
            cel = automap_.npcCel(monsters_.value(row->second, "Id"));
            name = strings_.findDisplayName(monsters_.value(row->second, "NameStr"));
        }
        if (key.type == 2 && seen && cel >= 0 && size_t(cel) < nativeAutomapCelCount_ &&
            cel <= INT16_MAX && v.load.secondarySeed) {
            const auto projected = project({float(unit.position->x), float(unit.position->y)});
            const int x = int(projected.x) / 10 + 1, y = int(projected.y) / 10 - 3;
            const auto layer = catalog_->level(*view_.area).automapLayer;
            if (layer >= 0 && layer < 100 && x >= INT16_MIN && x <= INT16_MAX && y >= INT16_MIN && y <= INT16_MAX)
                insertNativeCell(nativeAutomapCells_[{*v.load.mapSeed, uint32_t(layer), *v.load.secondarySeed}][2],
                    {uint16_t(cel), int16_t(x), int16_t(y)});
            cel = -1;
        }
        const auto target = std::find_if(view_.mapTargets.begin(), view_.mapTargets.end(),
            [&](const auto &entry) { return entry.unit == key; });
        const bool showName = target != view_.mapTargets.end() &&
            (target->interaction == OnlineMapInteraction::Npc || target->interaction == OnlineMapInteraction::Stash);
        if (showName) {
            const auto translated = strings_.find(target->name);
            name = translated.empty() ? target->name : std::string(translated);
        }
        auto mark = key.type == 1 && showName ? Mark::Npc : Mark::None;
        if(key.type==1) {
            // Native automap excludes the three non-public NPC identities.
            if(*unit.classId==537 || *unit.classId==538 || *unit.classId==539) continue;
            const auto row=monsterRows_.find(*unit.classId);
            if(row!=monsterRows_.end() && monsters_.number(row->second,"npc").value_or(0) &&
               *unit.classId!=537 && *unit.classId!=538 && *unit.classId!=539) mark=Mark::Npc;
            const auto pet=v.world.pets.find(key.id);
            if(pet!=v.world.pets.end()) {
                const bool eligible=automapPetTypes_.contains(pet->second.type);
                if(eligible && pet->second.owner==v.load.playerUnitId) mark=Mark::OwnPet;
                else if(eligible && inParty(pet->second.owner)) mark=Mark::PartyPet;
                else {mark=Mark::None;cel=-1;}
            }
        }
        if(key.type==2 && *unit.classId==267) mark=Mark::Stash;
        if (key.type == 2 && *unit.classId == 59) mark = Mark::BluePortal;
        if (key.type == 2 && *unit.classId == 60 && view_.area != 125 && view_.area != 126 &&
            view_.area != 127 && view_.area != 111 && view_.area != 112 && view_.area != 117)
            mark = Mark::RedPortal;
        // Live-unit symbols do not reveal map cells. Exploration gates only
        // the static MPQ cell artwork, rather than nearby NPC/pet/portal marks.
        if ((seen && cel >= 0) || mark != Mark::None)
            view_.automap.markers.push_back({{float(unit.position->x), float(unit.position->y)},
                mark == Mark::None ? cel : -1, name, mark==Mark::Npc, showName || mark==Mark::Npc, mark,std::pair{key.type,key.id}});
    }
    if (self == v.world.social.players.end() || !self->second.partyId || *self->second.partyId == UINT16_MAX) return;
    for (const auto &[id, p] : v.world.social.players) {
        if (id == *v.load.playerUnitId || drawnPlayers.contains(id) || !p.listed || p.partyId != self->second.partyId) continue;
        std::optional<Vec> position;
        if (const auto unit = v.world.units.find({0, id}); unit != v.world.units.end() && unit->second.position)
            position = Vec{float(unit->second.position->x), float(unit->second.position->y)};
        else if (p.area && component.contains(*p.area) && p.positionX && p.positionY)
            position = Vec{float(*p.positionX), float(*p.positionY)};
        if (position) view_.automap.markers.push_back({*position, -1, p.name, false, true, AutomapDrawView::UnitMark::Party});
    }
}
AutomapDiscovery RemoteTown::discovery() const {
    AutomapDiscovery d;
    for (const auto &[key, groups] : nativeAutomapCells_) {
        const auto &[seed, layer, objectSeed] = key;
        NativeAutomapLayer entry{seed, layer, 0, objectSeed, {}};
        for (size_t group = 0; group < groups.size(); ++group)
            entry.cells[group].assign(groups[group].begin(), groups[group].end());
        d.nativeLayers.push_back(std::move(entry));
    }
    return d;
}
void RemoteTown::restoreDiscovery(const AutomapDiscovery &d,const OnlineView &v) {
    if(!catalog_ || !v.load.act || !v.load.mapSeed || !v.load.secondarySeed)
        throw std::runtime_error("Automap discovery does not match entered world");
    for (const auto &layer : d.nativeLayers) {
        if (layer.seed != *v.load.mapSeed || layer.layer >= 100)
            throw std::runtime_error("Invalid native automap layer");
        for (const auto &group : layer.cells) for (const auto &cell : group)
            if (cell.cel >= nativeAutomapCelCount_) throw std::runtime_error("Automap cell does not exist in current MPQ");
    }
    decltype(nativeAutomapCells_) restored;
    for (const auto &layer : d.nativeLayers) {
        auto &groups = restored[{layer.seed, layer.layer, *v.load.secondarySeed}];
        for (size_t group = 0; group < groups.size(); ++group)
            if (group != 2 || layer.objectSeed == *v.load.secondarySeed)
                for (const auto &cell : layer.cells[group]) insertNativeCell(groups[group], cell);
    }
    for (auto &[key, groups] : restored) {
        const auto existing = nativeAutomapCells_.find(key);
        if (existing != nativeAutomapCells_.end())
            for (size_t group = 0; group < groups.size(); ++group)
                for (const auto &cell : existing->second[group]) insertNativeCell(groups[group], cell);
        nativeAutomapCells_[key] = std::move(groups);
    }
    updateAutomapView(v);
}
} // namespace d2x
