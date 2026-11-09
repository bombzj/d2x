#include "content/monsters/monster_identity.hpp"
#include "remote_town.hpp"
#include "world/navigation_snapshot.hpp"
#include "world/retail/desert.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include "world/interaction_geometry.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/fingerprint.hpp"
#include "gameplay/quest/catalog.hpp"
#include "content/world/object_mode.hpp"

namespace d2x {
std::optional<Vec> RemoteTown::inputOrigin(const OnlineView &v) const {
    if(gameGeneration_!=v.gameGeneration || areaGeneration_!=v.world.areaGeneration || !displayPlayerPosition_) return {};
    return Vec{float(displayPlayerPosition_->x),float(displayPlayerPosition_->y)};
}
int RemoteTown::monsterSizeX(uint16_t identity) const {
    const auto row=monsterRows_.find(identity);
    if(row==monsterRows_.end()) return 0;
    const auto shape=monsterSizeRows_.find(monsters_.value(row->second,"MonStatsEx"));
    return shape==monsterSizeRows_.end()?0:monsterSizes_.number(shape->second,"SizeX").value_or(0);
}
Grid RemoteTown::navigationGrid(const OnlineView &online,std::optional<OnlineUnitKey> ignored) const {
    Grid result;
    if(!map_ || !view_.origin) return result;
    const auto *terrain=&map_->grid;const auto origin=*view_.origin;
    result.width=terrain->width;result.height=terrain->height;
    auto presence=std::make_shared<std::map<std::pair<int,int>,uint16_t>>();
    if(online.protocolProfile=="lod113c") for(const auto &[key,unit]:online.world.units) {
        if(key==ignored || !unit.position || (key.type==0 && key.id==online.load.playerUnitId)) continue;
        int size=0;uint16_t mask=0;bool nonblockingPresence=false;
        if(key.type==0 && !online.world.corpseOwners.contains(key.id) && unit.mode!=0 && unit.mode!=17) {
            size=playerMovement.size;mask=0x80;
        } else if(key.type==1 && unit.classId && !onlineMonsterCorpse(unit)) {
            const auto row=monsterRows_.find(*unit.classId);
            if(row==monsterRows_.end()) continue;
            const auto ai=monsters_.value(row->second,"AI");
            // The original idle S1 of these AIs is non-solid. Initial monster
            // assignments also carry that mode, without a 0x0E nativeMode tag.
            if(unit.mode==8 && !unit.actionSkill && (ai=="Vulture" || ai=="SandMaggot" || ai=="FrogDemon")) continue;
            if(ai=="FrogDemon") {
                const auto used=unit.actionSkill?unit.actionSkill:unit.stoppedSkill;
                bool submerged=false;
                if(used) for(size_t skill=0;skill<skills_.rows().size();++skill)
                    if(skills_.number(skill,"Id")==*used) {submerged=skills_.number(skill,"srvstfunc")==51;break;}
                if(submerged) continue;
            }
            size=monsterSizeX(*unit.classId);mask=0x100;
            // D2Common_11281: town monsters without INTERACT use pet
            // presence. Match the normal client's navigationCollision;
            // their monster contact mask must not become PLAYER_PATH presence.
            nonblockingPresence=(monsters_.number(row->second,"npc").value_or(0) ||
                monsters_.number(row->second,"inTown").value_or(0)) &&
                !monsters_.number(row->second,"interact").value_or(0);
        }
        const bool hireling=key.type==1 && online.world.hireling && online.world.hireling->id==key.id;
        const auto pattern=nativeUnitCollisionPattern(size,hireling || nonblockingPresence);
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            const auto bits=nativeCollisionFootprint(pattern,dx,dy,mask);
            if(bits) (*presence)[{int(unit.position->x)-origin.x+dx,int(unit.position->y)-origin.y+dy}]|=bits;
        }
    }
    // A moving NPC can overlap the player's current footprint after a
    // conversation. Do not let this derived avoidance layer reject every
    // route at its starting cell: keep the rest of that unit solid, and keep
    // all terrain/object masks, while allowing a normal input out of overlap.
    const auto observer=displayPlayerPosition_?displayPlayerPosition_:online.world.playerPosition;
    if(observer) {
        const auto pattern=nativeUnitCollisionPattern(playerMovement.size,false);
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
            if(nativeCollisionFootprint(pattern,dx,dy,1))
                presence->erase({int(observer->x)-origin.x+dx,int(observer->y)-origin.y+dy});
    }
    // Ephemeral client query only: retain terrain/object authority, add the
    // original cross/square sizes of already assigned living units/other players.
    // No footprint is written into NativeMap or the replicated world.
    // Identity of this ephemeral collision view for debug-query continuation.
    // Hash the derived masks, not unit packets/timestamps: movement unrelated
    // to collision must not invalidate an otherwise identical query domain.
    const uint64_t terrainRevision=navigationRevision()*0x100000001b3ULL+terrain->nativeCollisionRevision;
    uint64_t collisionRevision=terrainRevision;
    for(const auto &[cell,mask]:*presence) {
        collisionRevision=(collisionRevision^uint32_t(cell.first))*0x100000001b3ULL;
        collisionRevision=(collisionRevision^uint32_t(cell.second))*0x100000001b3ULL;
        collisionRevision=(collisionRevision^mask)*0x100000001b3ULL;
    }
    result.obstacleRevision=collisionRevision;
    result.collisionSample=[terrain,presence](int x,int y,EntityId target) {
        const auto found=presence->find({x,y});
        return uint16_t(terrain->nativeMovementMask(x,y,target)|(found==presence->end()?0:found->second));
    };
    // Freeze on demand on the client thread. A moving unit only changes the
    // sparse overlay, so avoid a map lookup per terrain cell on every change.
    result.freezeMovement=[this,presence,collisionRevision] {
        auto snapshot=std::make_shared<NavigationSnapshot>();
        snapshot->base=navigationSnapshot(true);
        snapshot->world=snapshot->base->world;
        snapshot->width=snapshot->base->width;snapshot->height=snapshot->base->height;
        snapshot->originX=snapshot->base->originX;snapshot->originY=snapshot->base->originY;
        snapshot->publication=collisionRevision;snapshot->overlay=*presence;
        return snapshot;
    };
    return result;
}
std::shared_ptr<const NavigationSnapshot> RemoteTown::navigationSnapshot(bool native) const {
    if(!navigationPublicationError_.empty()) throw std::runtime_error(navigationPublicationError_);
    if(!map_ || !view_.origin) return {};
    if(native && map_->grid.nativeCollision.empty() && map_->grid.neighbours.empty() && map_->grid.navigationSources.empty())
        return navigationSnapshot(false);
    auto &revision=native?navigationNativeRevision_:navigationStaticRevision_;
    auto &snapshot=native?navigationNativeSnapshot_:navigationStaticSnapshot_;
    auto &dirty=native?navigationDirtyNative_:navigationDirtyStatic_;
    auto &full=native?navigationFullNative_:navigationFullStatic_;
    const NavigationRevision next{gameGeneration_,areaGeneration_,nativeMap_?nativeMap_->terrainRevision():0,
        map_->grid.obstacleRevision,map_->grid.missileCollisionRevision,native?map_->grid.nativeCollisionRevision:0,
        uint64_t(view_.origin->x),uint64_t(view_.origin->y),(uint64_t(uint32_t(map_->grid.width))<<32)|uint32_t(map_->grid.height)};
    if(revision!=next || !snapshot) {
        if(revision && ((*revision)[2]!=next[2] || (*revision)[4]!=next[4] ||
            (native && (*revision)[5]!=next[5] && dirty.empty()))) full=true;
        const auto &source=map_->grid;
        if(!native && source.neighbours.empty() && source.navigationSources.empty() && !source.terrainCollision.empty()) {
            if(full || !navigationTerrainSnapshot_) {
                Grid terrain;terrain.width=source.width;terrain.height=source.height;
                terrain.collisionSample=[&source](int column,int row,EntityId)->uint16_t {
                    if(column<0 || row<0 || column>=source.width || row>=source.height) return 0xffff;
                    const size_t index=size_t(row)*source.width+column;
                    const auto flags=source.fullTerrainCollision.empty()?source.terrainCollision[index]:source.fullTerrainCollision[index];
                    return uint16_t(flags|(source.missileCollision.empty()?0:source.missileCollision[index])|
                        (source.blocked[index] && !(source.terrainCollision[index]&0x09)?1:0));
                };
                navigationTerrainSnapshot_=NavigationSnapshot::publish(terrain,{float(view_.origin->x),float(view_.origin->y)},
                    navigationTerrainSnapshot_,false,nullptr,navigationWorld_);
            }
            auto layered=std::make_shared<NavigationSnapshot>();
            layered->base=navigationTerrainSnapshot_;layered->world=navigationWorld_;
            layered->width=source.width;layered->height=source.height;
            layered->originX=view_.origin->x;layered->originY=view_.origin->y;
            layered->publication=source.obstacleRevision;
            if(full) {
                layered->scannedCells=navigationTerrainSnapshot_->scannedCells;
                layered->copiedBytes=navigationTerrainSnapshot_->copiedBytes;
                layered->reusedBlocks=navigationTerrainSnapshot_->reusedBlocks;
            }
            for(const auto &obstacle:source.obstacles)
                for(int row=std::max(0,obstacle.y);row<std::min(source.height,obstacle.y+obstacle.height);++row)
                    for(int column=std::max(0,obstacle.x);column<std::min(source.width,obstacle.x+obstacle.width);++column)
                        layered->overlay[{column,row}]|=obstacle.mask;
            layered->copiedBytes+=layered->overlay.size()*sizeof(NavigationSnapshot::Overlay::value_type);
            if(!snapshot || snapshot->base!=layered->base || snapshot->overlay!=layered->overlay) snapshot=std::move(layered);
        } else snapshot=NavigationSnapshot::publish(source,{float(view_.origin->x),float(view_.origin->y)},snapshot,native,
            full?nullptr:&dirty,navigationWorld_);
        dirty.clear();full=false;
        revision=next;
    }
    return snapshot;
}
RemoteTown::RemoteTown(Archives &a, bool reportAutomapRoomsPd2)
    : archives_(a), libraries_(a), objects_(a.read("data/global/excel/objects.txt")),
      monsters_(a.read("data/global/excel/monstats.txt")),
      monsterSizes_(a.read("data/global/excel/monstats2.txt")),
      skills_(a.read("data/global/excel/skills.txt")), shrines_(a.read("data/global/excel/shrines.txt")),
      petTypes_(a.read("data/global/excel/pettype.txt")), automap_(a), strings_(a),
      nativeAutomapCelCount_(decodeDc6(a.read("data/global/ui/automap/maximap.dc6")).frames.size()),
      revealed_(a, automap_, nativeAutomapCelCount_),
      reportAutomapRoomsPd2_(reportAutomapRoomsPd2) {
    for (size_t row = 0; row < shrines_.rows().size(); ++row)
        if (auto code = shrines_.number(row, "Code")) shrineRows_.emplace(*code, row);
    for(size_t row=0;row<petTypes_.rows().size();++row)
        if(petTypes_.number(row,"automap").value_or(0))
            if(const auto type=petTypes_.number(row,"idx");type && *type>=0 && *type<=255) automapPetTypes_.insert(uint8_t(*type));
    for (size_t row = 0; row < objects_.rows().size(); ++row)
        if (auto id = objects_.number(row, "Id")) objectRows_.emplace(*id, row);
    for (size_t row = 0; row < monsters_.rows().size(); ++row)
        if (!monsters_.value(row,"hcIdx").empty()) monsterRows_.emplace(compiledMonsterIndex(monsters_,row), row);
    for (size_t row = 0; row < monsterSizes_.rows().size(); ++row)
        monsterSizeRows_.emplace(std::string(monsterSizes_.value(row, "Id")), row);
}
void RemoteTown::update(const OnlineView &v, std::optional<Vec> displayPosition) {
    const NavigationWorldKey world{v.gameGeneration,v.world.areaGeneration,v.load.mapSeed.value_or(0),
        v.load.act.value_or(0),v.load.difficulty.value_or(0),v.protocolProfile};
    if(navigationWorld_!=world) {
        navigationPublicationError_.clear();
        navigationWorld_=world;navigationStaticSnapshot_.reset();navigationNativeSnapshot_.reset();navigationTerrainSnapshot_.reset();
        navigationStaticRevision_.reset();navigationNativeRevision_.reset();
        navigationFullStatic_=navigationFullNative_=true;
    }
    automapPlayerPresentPd2_ = v.remoteLobbyOnly && v.load.playerUnitId && v.world.units.contains({0, *v.load.playerUnitId});
    std::optional<OnlinePoint> point;
    if (displayPosition && std::isfinite(displayPosition->x) && std::isfinite(displayPosition->y) &&
        displayPosition->x >= 0 && displayPosition->y >= 0 && displayPosition->x <= UINT16_MAX && displayPosition->y <= UINT16_MAX)
        point = OnlinePoint{uint16_t(std::lround(displayPosition->x)), uint16_t(std::lround(displayPosition->y))};
    const bool observerChanged = point != displayPlayerPosition_;
    displayPlayerPosition_ = point;
    automapClientPosition_.reset();
    if(point) {
        const int pixelX=int(std::floor((double(displayPosition->x)+.5)*32));
        const int pixelY=int(std::floor((double(displayPosition->y)+.5)*32));
        automapClientPosition_=std::pair{(pixelX-pixelY)/2,(pixelX+pixelY)/4};
    }
    if (gameGeneration_ != v.gameGeneration || areaGeneration_ != v.world.areaGeneration) {
        if (gameGeneration_ != v.gameGeneration) {
            revealed_.reset();
            automapDrawnTiles_.clear();automapRevealPosition_.reset();
            automapPositionPd2_ = {};
            explored_.clear(); discoveredCels_.clear(); discoveredTowns_.clear(); preparedTownAutomaps_.clear(); actSeeds_.clear();
            nativeAutomapCells_.clear();
            knownTargetPositions_.clear();observedTerrain_.clear();
            knownConnections_.clear();knownWaypoints_.clear();
        }
        gameGeneration_ = v.gameGeneration;
        areaGeneration_ = v.world.areaGeneration;
        automapRoomCallbacksPd2_.clear();
        revision_ = ~uint64_t{};
        map_ = nullptr;
        nativeMap_.reset();
        nativeTerrain_.reset();
        navigationStaticSnapshot_.reset();navigationNativeSnapshot_.reset();navigationTerrainSnapshot_.reset();
        navigationStaticRevision_.reset();navigationNativeRevision_.reset();
        navigationDirtyStatic_.clear();navigationDirtyNative_.clear();navigationFullStatic_=navigationFullNative_=true;
        automapVisitedTiles_.clear();automapViewportPrepared_=false;automapProjection_.reset();automapComponent_.clear();
        nativeTerrainDirty_ = true;
        nativePlayerRoom_.reset();
        unitRooms_.clear();
        nativePlayerPosition_.reset();
        walkingObservation_.reset();
        nativeSequence_ = 0;
        nativePositionDiscontinuity_.reset();
        nativePendingSince_=0;
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
                        std::erase_if(observedTerrain_,[&](const auto &entry){return catalog_->level(std::get<0>(entry)).act==act;});
                        std::erase_if(knownTargetPositions_,[&](const auto &entry){return catalog_->level(std::get<0>(entry.first)).act==act;});
                        std::erase_if(knownConnections_,[&](const auto &entry){return catalog_->level(entry.first.first).act==act;});
                        std::erase_if(knownWaypoints_,[&](const auto &entry){return catalog_->level(entry.first).act==act;});
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
    if (updateNative(v)) {
        rememberWalkingConnection(v);
        updateUnitRooms(v);
        updateMapTargets(v);
        navigationPublicationError_.clear();
        try {navigationSnapshot();}
        catch(const std::exception &failure) {
            navigationPublicationError_="Navigation publication unavailable: "+std::string(failure.what());
            view_.mapErrors[*view_.area]=navigationPublicationError_;
        }
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
    automapProjection_.reset();
    view_.automapStamps.clear();view_.automapTowns.clear();view_.automapRevealedCells.clear();view_.automap={};
    view_.reason = nativeReason_.empty() ? "Waiting for native room assignments and player position" : nativeReason_;
    view_.nativeMapReason = view_.reason;
    for (const auto &[level, reason] : nativeErrors_) view_.mapErrors[level] = reason;
}
void RemoteTown::updateUnitRooms(const OnlineView &v) {
    std::erase_if(unitRooms_,[&](const auto &entry) {return !v.world.units.contains(entry.first);});
    for(const auto &[key,unit]:v.world.units) {
        const auto room=unit.position?nativeMap_->clientRoom(unit.position->x,unit.position->y):std::nullopt;
        if(room && (key.type==1 || key.type==2) && unit.classId)
            knownTargetPositions_[{uint16_t(nativeMap_->tiles().rooms().at(*room).level),key.type,*unit.classId}]=*unit.position;
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
                {}, width, height, v.protocolProfile == "lod113c" && monsters_.number(record->second, "npc").value_or(0) != 0});
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
        if(operation==43 && *unit.classId==100 && (mode==1 || mode==2) && nativeTerrain_)
            nativeTerrain_->openTombWall({float(x),float(y)});
        // ObjMode::OperateFunction23 accepts both operating/opened waypoints.
        // Selectable1 is clear in MPQ, but the native handler still opens the
        // menu in mode 1 or 2. Targetability does not override that exception.
        const bool selectable=unit.mode && *unit.mode<8 &&
            (number("Selectable"+std::to_string(mode))!=0 ||
             (operation==23 && (mode==1 || mode==2)));
        // Retail D2Client 249F0 applies Selectable[mode] to ordinary objects.
        // Only its IsDoor branch (24040) consumes the 0E boolean argument.
        // Applying a server flag mask to every object hides an otherwise
        // selectable Inifuss tree after its initial native 0E refresh.
        if (!selectable || (number("IsDoor")!=0 && unit.objectTargetable==false)) continue;
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
            auto destination=unit.portalDestination?std::optional<uint16_t>{*unit.portalDestination}:std::nullopt;
            // A4Q2 Operate73 changes act to Harrogath directly; Init78 does
            // not require a fabricated destination/linked-portal packet.
            if(v.protocolProfile=="lod113c" && operation==73) destination=109;
            // D2MOO Operate34: the authored palace portal does not need a
            // dynamic portal-destination packet. Its original operator fixes
            // the two levels; coordinates still come from this real assignment.
            if(v.protocolProfile=="lod113c" && (operation==34 || operation==70 || operation==72)) {
                const auto room=nativeMap_->clientRoom(unit.position->x,unit.position->y);
                if(room) {
                    const int level=nativeMap_->tiles().rooms().at(*room).level;
                    if(operation==34) {
                        if(level==54) destination=74;
                        else if(level==74) destination=54;
                    } else {
                        const auto slot=questDefinition(QuestId::EveOfDestruction).nativeSlot;
                        const auto status=v.world.quests.statuses[slot];
                        const auto flags=v.world.quests.playerFlags?(*v.world.quests.playerFlags)[slot]:0;
                        destination.reset();
                        // Operate70 returns freely from the chamber. In the throne room
                        // status 3 announces departure; a rewarded rejoin can instead
                        // observe status 12 and the reopened portal. Arrival is server-owned.
                        if(operation==70 && level==132) destination=131;
                        if(operation==70 && level==131 && (status==3 || (unit.mode==2 && ((flags&0x2000) || (status==12 && (flags&1))))))
                            destination=132;
                        // Operate72 requires this game's PRIMARYGOALDONE on an open exit.
                        if(operation==72 && level==132 && mode==2 && (flags&0x2000)) destination=109;
                    }
                }
            }
            std::string displayName(label.empty() ? keyName : label);
            if ((number("SubClass") & 4) != 0) {
                const int titleDestination = unit.portalDestination.value_or(unit.objectInteractType.value_or(0));
                const auto level = catalog_->levels().find(titleDestination);
                if (level == catalog_->levels().end()) displayName = strings_.find(5389);
                else if (v.protocolProfile == "lod113c" && titleDestination == 8) {
                    const auto &quests = v.world.quests.availability;
                    displayName = quests ? strings_.find((*quests)[1] ? 5048 : 5047) : std::string_view{};
                } else displayName = strings_.find(level->second.name);
            }
            view_.mapTargets.push_back({key, *unit.position, *interaction,
                std::move(displayName), destination,
                number("SizeX"), number("SizeY"),false,
                number("IsDoor")?std::optional<int>{number("Orientation")}:std::nullopt});
        }
    }
    std::set<OnlineUnitKey> coveredWarps;
    if(v.protocolProfile=="lod113c") for(auto &entry:view_.mapTargets) if(entry.unit.type==2 &&
        (entry.interaction==OnlineMapInteraction::Exit || entry.interaction==OnlineMapInteraction::Door)) {
        const auto unit=v.world.units.find(entry.unit);if(unit==v.world.units.end() || !unit->second.classId) continue;
        const auto row=objectRows_.find(*unit->second.classId);
        if(row==objectRows_.end()) continue;
        const auto operation=objects_.number(row->second,"OperateFn");
        if(operation!=44 && operation!=16) continue;
        const auto room=nativeMap_->clientRoom(entry.position.x,entry.position.y);if(!room) continue;
        const OnlineMapTarget *tile=nullptr;bool ambiguous=false;
        // ObjMode16/44 -> WarpPlayerToTile iterates the object's real Room1.
        // Bind only a unique currently assigned tile in that same room.
        for(const auto &candidate:view_.mapTargets) if(candidate.unit.type==5 && candidate.destination &&
            nativeMap_->clientRoom(candidate.position.x,candidate.position.y)==room) {
            if(tile) {ambiguous=true;break;}tile=&candidate;
        }
        if(tile && !ambiguous) {
            entry.destination=tile->destination;
            if(operation==16) {
                // The visible trapdoor owns this tile: first click opens it,
                // a later click on the open object enters. Do not expose the
                // underlying tile as an independent shortcut past the lid.
                entry.interaction=OnlineMapInteraction::Exit;coveredWarps.insert(tile->unit);
            }
        }
    }
    std::erase_if(view_.mapTargets,[&](const auto &entry){return coveredWarps.contains(entry.unit);});
    if(view_.origin) {
        const auto mark=[&](const Grid::Obstacle &obstacle) {
            for(int row=NavigationSnapshot::blockCoordinate(obstacle.y+view_.origin->y);
                row<=NavigationSnapshot::blockCoordinate(obstacle.y+obstacle.height-1+view_.origin->y);++row)
                for(int column=NavigationSnapshot::blockCoordinate(obstacle.x+view_.origin->x);
                    column<=NavigationSnapshot::blockCoordinate(obstacle.x+obstacle.width-1+view_.origin->x);++column) {
                    navigationDirtyStatic_.emplace(column,row);navigationDirtyNative_.emplace(column,row);
                }
        };
        for(const auto &old:nativeTerrain_->grid.obstacles)
            if(std::find(obstacles.begin(),obstacles.end(),old)==obstacles.end()) mark(old);
        for(const auto &next:obstacles)
            if(std::find(nativeTerrain_->grid.obstacles.begin(),nativeTerrain_->grid.obstacles.end(),next)==nativeTerrain_->grid.obstacles.end()) mark(next);
    }
    nativeTerrain_->grid.setObstacles(std::move(obstacles));
    for(const auto &entry:view_.mapTargets) {
        const auto room=nativeMap_->clientRoom(entry.position.x,entry.position.y);if(!room) continue;
        const auto level=uint16_t(nativeMap_->tiles().rooms().at(*room).level);
        const auto unit=v.world.units.find(entry.unit);
        const auto object=unit!=v.world.units.end() && unit->second.classId?objectRows_.find(*unit->second.classId):objectRows_.end();
        const bool fixedPortal=v.protocolProfile=="lod113c" && entry.interaction==OnlineMapInteraction::Portal &&
            object!=objectRows_.end() && (unit->second.classId==60 || objects_.number(object->second,"OperateFn")==34 || objects_.number(object->second,"OperateFn")==70);
        if((entry.interaction==OnlineMapInteraction::Exit || fixedPortal) && entry.destination)
            knownConnections_[{level,*entry.destination}]=entry.position;
        if(entry.interaction==OnlineMapInteraction::Waypoint) knownWaypoints_[level]=entry.position;
    }
    if (v.world.npcConversation) {
        const auto &conversation = *v.world.npcConversation;
        const auto target = std::find_if(view_.mapTargets.begin(), view_.mapTargets.end(),
            [&](const auto &entry) { return entry.unit == OnlineUnitKey{conversation.type, conversation.source}; });
        if (target != view_.mapTargets.end()) {
            OnlineNpcDialogView dialog{};
            dialog.source = conversation.source;
            dialog.revision = conversation.revision;
            dialog.position = target->position;
            dialog.speaker = target->name; // NPC and object targets already carry their display names.
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
            if (id == "warriv1" && rewarded(6)) {label = "WarrivMenu1b";dialog.travelDestination=40;}
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
    view_.mapLoading={};
    view_.mapLoading.receivedSequence=v.world.mapEventSequence;
    view_.mapLoading.consumedSequence=nativeSequence_;
    view_.mapLoading.assignedRooms=v.world.rooms.size();
    if (!nativeMap_ || !nativeReason_.empty()) return false;
    const auto started = std::chrono::steady_clock::now();
    const auto sampleLoading=[&] {
        auto &loading=view_.mapLoading;
        loading.consumedSequence=nativeSequence_;
        loading.pendingEvents=v.world.mapEventSequence>=nativeSequence_?v.world.mapEventSequence-nativeSequence_:0;
        const auto now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        if(loading.pendingEvents) {if(!nativePendingSince_) nativePendingSince_=now;}
        else nativePendingSince_=0;
        loading.pendingMilliseconds=nativePendingSince_?now-nativePendingSince_:0;
        loading.terrainDirty=nativeTerrainDirty_;
        loading.terrainRevision=nativeMap_->terrainRevision();
        const auto &rooms=nativeMap_->tiles().rooms();
        loading.loadedRooms=std::count_if(rooms.begin(),rooms.end(),[](const auto &room){return room.loaded;});
        const auto sampleRoom=[&](const std::optional<OnlinePoint> &position,std::optional<bool> &loaded,std::optional<bool> &assigned) {
            if(!position) return;
            loaded=false;assigned=false;
            if(const auto index=nativeMap_->clientRoom(position->x,position->y)) {
                const auto &room=rooms.at(*index);loaded=room.loaded;
                assigned=v.world.rooms.contains({uint8_t(room.level),uint16_t(room.room.x),uint16_t(room.room.y)});
            }
        };
        sampleRoom(displayPlayerPosition_,loading.clientRoomLoaded,loading.clientRoomAssigned);
        sampleRoom(v.world.playerPosition,loading.serverRoomLoaded,loading.serverRoomAssigned);
        loading.updateMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    };
    int eventLevel = 0;
    try {
        const auto &world = v.world;
        std::optional<std::pair<uint32_t,uint64_t>> discontinuity;
        if(v.load.playerUnitId) if(const auto player=world.units.find({0,*v.load.playerUnitId});player!=world.units.end())
            discontinuity=std::pair{*v.load.playerUnitId,player->second.positionDiscontinuity};
        const bool repositioned=discontinuity && discontinuity!=nativePositionDiscontinuity_;
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
        auto locatePlayerRoom = [&]() -> std::optional<size_t> {
            if (!nativePlayerPosition_) return {};
            const auto room = nativeMap_->clientRoom(nativePlayerPosition_->x, nativePlayerPosition_->y);
            // D2Client 07/08 own the in-sight references. Clients::sub_6FC33020
            // changes CLIENT_IN_ROOM references on the server; repeating that
            // operation here lets a later 08 consume a propagated reference.
            // Coordinates select the snapshot, without acquiring another root.
            if (room && nativeMap_->tiles().rooms().at(*room).loaded) selectRoom(*room);
            return room;
        };
        for (const auto &event : world.mapEvents) {
            if (event.sequence <= nativeSequence_) continue;
            if (event.sequence != nativeSequence_ + 1)
                throw std::runtime_error("Native map event history was lost; rejoin the game");
            eventLevel = event.level;
            switch (event.kind) {
            case OnlineMapEvent::Kind::RevealRoom:
                nativeMap_->reveal(event.level, event.point.x, event.point.y);
                break;
            case OnlineMapEvent::Kind::HideRoom:
                nativeMap_->hide(event.level, event.point.x, event.point.y);
                break;
            case OnlineMapEvent::Kind::PlayerPosition:
                nativePlayerPosition_ = event.point;
                locatePlayerRoom();
                break;
            case OnlineMapEvent::Kind::PlayerReposition: {
                const bool roomed=nativePlayerRoom_ &&
                    nativeMap_->tiles().rooms().at(*nativePlayerRoom_).loaded;
                bool warp=false;
                if(event.usedSkill) for(size_t row=0;row<skills_.rows().size();++row)
                    if(skills_.number(row,"Id")==*event.usedSkill) {
                        warp=skills_.number(row,"warp").value_or(0)!=0;break;
                    }
                const bool retail = v.protocolProfile == "lod113c" && !event.dead;
                const bool setup = event.repositionFlags && (!roomed || !warp);
                if (retail && roomed && setup)
                    view_.repositionFadeEndMilliseconds=event.receivedMilliseconds+500;
                nativePlayerPosition_=event.point;
                const auto destinationRoom = locatePlayerRoom();
                if (retail) {
                    // 81720 clears both water effects, then conditionally runs 814F0
                    // with the destination room already attached.
                    auto &weather = view_.weatherRepositions;
                    const auto firstRetained = std::ranges::lower_bound(
                        weather, world.mapEvents.front().sequence, {}, &OnlineWeatherReposition::sequence);
                    weather.erase(weather.begin(), firstRetained);
                    OnlineWeatherReposition reposition{event.sequence, {}};
                    if (setup && destinationRoom)
                        reposition.setupLevel = uint16_t(nativeMap_->tiles().rooms().at(*destinationRoom).level);
                    weather.push_back(reposition);
                }
                break;
            }
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
            if (!repositioned && nativeSequence_ != world.mapEventSequence && std::chrono::steady_clock::now() >= deadline) {
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
        auto position = displayPlayerPosition_;
        if (!position && !deferred && nativeLoadComplete_) position = world.playerPosition;
        if (position) {
            if (const auto room = nativeMap_->clientRoom(position->x, position->y)) {
                const auto &prepared = nativeMap_->tiles().rooms().at(*room);
                // Loading uses the ordered map-event anchor above. Afterward,
                // normal movement follows the current client unit coordinates;
                // callers without a display sample must not return to the last
                // loading/warp position. Only an assigned 07 room is selected.
                if (prepared.loaded && world.rooms.contains({uint8_t(prepared.level), uint16_t(prepared.room.x), uint16_t(prepared.room.y)})) {
                    selectRoom(*room);
                }
            }
        } else locatePlayerRoom();
        nativeTerrainDirty_ |= changed || nativeMap_->terrainRevision() != terrainRevision;
        view_.mapLoading.deferred=deferred;
        sampleLoading();
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
            navigationFullStatic_=navigationFullNative_=true;
            navigationStaticRevision_.reset();navigationNativeRevision_.reset();
            nativeTerrain_->grid.freezeMovement=[this]{return navigationSnapshot();};
            ++automapTerrainRevision_;
            automapVisitedTiles_.assign(nativeTerrain_->terrain.instances.size(),0);
            automapViewportPrepared_=false;
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
        nativePositionDiscontinuity_=discontinuity;
        view_.mapLoading.terrainDirty=nativeTerrainDirty_;
        view_.mapLoading.updateMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        // Report work exceeding one 25 Hz simulation tick, not every movement.
        if (elapsed >= 40) std::clog << "[area-load] client level=" << level << " map_ms=" << elapsed << '\n';
        return true;
    } catch (const std::exception &e) {
        nativeReason_ = e.what();
        nativeErrors_[eventLevel] = nativeReason_;
        view_.nativeMapReason = nativeReason_;
        view_.mapLoading.consumedSequence=nativeSequence_;
        view_.mapLoading.pendingEvents=v.world.mapEventSequence>=nativeSequence_?v.world.mapEventSequence-nativeSequence_:0;
        view_.mapLoading.terrainDirty=nativeTerrainDirty_;
        view_.mapLoading.updateMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        return false;
    }
}
void RemoteTown::rememberWalkingConnection(const OnlineView &v) {
    if(!nativeMap_ || !view_.area || !v.world.playerPosition) return;
    // This is a navigation hint, not transfer credit. The normal local path
    // observes the seam even when a later sparse resource sample skips it.
    const auto point=v.protocolProfile=="lod113c"?displayPlayerPosition_.value_or(*v.world.playerPosition):*v.world.playerPosition;
    const auto room=nativeMap_->clientRoom(point.x,point.y);
    if(!room || !nativeMap_->tiles().rooms().at(*room).loaded) return;
    const auto level=uint16_t(nativeMap_->tiles().rooms().at(*room).level);
    if(level!=view_.area) return; // Wait for a confirmed point in the displayed level.
    if(walkingObservation_ && walkingObservation_->first!=level) {
        const auto [previous,from]=*walkingObservation_;
        for(const auto &edge:walkingBoundaries()) if(edge.destination==previous) {
            const auto normal=[&](OnlinePoint p){return edge.side%2?int(p.x):int(p.y);};
            const auto along=[&](OnlinePoint p){return edge.side%2?int(p.y):int(p.x);};
            // Only an actually crossed, reconstructed outdoor seam is a
            // walking hint. A waypoint/portal to an unrelated room is not.
            if((normal(from)<edge.plane)==(normal(point)<edge.plane) ||
                std::abs(normal(from)-edge.plane)>64 || std::abs(normal(point)-edge.plane)>64 ||
                along(from)<edge.start || along(from)>=edge.end || along(point)<edge.start || along(point)>=edge.end) continue;
            knownConnections_[{previous,level}]=from;knownConnections_[{level,previous}]=point;break;
        }
    }
    walkingObservation_=std::pair{level,point};
}
std::vector<RemoteTown::WalkingBoundary> RemoteTown::walkingBoundaries() const {
    std::vector<WalkingBoundary> result;
    if(!nativeMap_ || !catalog_ || !view_.nativeMapReady || !view_.area) return result;
    const int level=*view_.area;const auto &layout=nativeMap_->layout();
    const auto found=layout.connections.find(level);const auto &record=catalog_->level(level);
    const auto slots=found==layout.connections.end()?NativeActLayout::ConnectionSlots{record.visible,record.warps}:found->second;
    const auto &rooms=nativeMap_->tiles().rooms();
    const auto append=[&](int destination,const auto &a,const auto &b) {
        int side=-1,plane=0,start=0,end=0;
        if(a.x+a.width==b.x || b.x+b.width==a.x) {
            side=a.x+a.width==b.x?3:1;plane=(side==3?a.x+a.width:a.x)*5;
            start=std::max(a.y,b.y)*5;end=std::min(a.y+a.height,b.y+b.height)*5;
        } else if(a.y+a.height==b.y || b.y+b.height==a.y) {
            side=a.y+a.height==b.y?0:2;plane=(side==0?a.y+a.height:a.y)*5;
            start=std::max(a.x,b.x)*5;end=std::min(a.x+a.width,b.x+b.width)*5;
        }
        if(side>=0 && start<end) result.push_back({uint16_t(destination),side,plane,start,end});
    };
    for(const auto &source:rooms) if(source.level==level) for(const auto near:source.near) {
        const auto &other=rooms.at(near);if(other.level==level) continue;
        bool walking=false;
        for(size_t slot=0;slot<slots.visible.size();++slot)
            walking|=slots.visible[slot]==other.level && slots.warps[slot]==-1;
        if(walking) append(other.level,source.room,other.room);
    }
    // The client already reconstructed level placement and resolved Vis/-1
    // walking links. A neighboring Room1 need not be assigned until ordinary
    // movement approaches the border. Expose its candidate seam now; neither
    // collision, a passable opening nor an unseen room is fabricated here.
    if(const auto source=layout.levels.find(level);source!=layout.levels.end())
        for(size_t slot=0;slot<slots.visible.size();++slot) {
            const int destination=slots.visible[slot];if(destination<=0 || slots.warps[slot]!=-1) continue;
            const auto other=layout.levels.find(destination);
            if(other!=layout.levels.end() && catalog_->level(destination).act==record.act)
                append(destination,source->second,other->second);
        }
    std::sort(result.begin(),result.end(),[](const auto &a,const auto &b) {
        return std::tie(a.destination,a.side,a.plane,a.start,a.end)<std::tie(b.destination,b.side,b.plane,b.start,b.end);
    });
    std::vector<WalkingBoundary> merged;
    for(const auto &edge:result) {
        if(!merged.empty()) {
            auto &last=merged.back();
            if(last.destination==edge.destination && last.side==edge.side && last.plane==edge.plane && edge.start<=last.end) {
                last.end=std::max(last.end,edge.end);continue;
            }
        }
        merged.push_back(edge);
    }
    return merged;
}
void RemoteTown::refreshNavigationCache() const {
    if(!nativeMap_ || !catalog_ || !view_.nativeMapReady || !view_.area) {
        navigationRooms_.clear();navigationConnections_.clear();navigationCached_=false;return;
    }
    const auto key=std::tuple{gameGeneration_,areaGeneration_,nativeMap_->terrainRevision()};
    if(navigationCached_ && key==navigationKey_) return;
    navigationKey_=key;navigationCached_=true;++navigationCacheBuilds_;
    navigationRooms_.clear();navigationConnections_.clear();
    for(const auto &room:nativeMap_->tiles().rooms())
        navigationRooms_.push_back({room.level,room.room.x*5,room.room.y*5,room.room.width*5,room.room.height*5,room.loaded,room.near});
    const int act=catalog_->level(*view_.area).act;
    for(const auto &[id,level]:catalog_->levels()) if(level.act==act) {
        const auto found=nativeMap_->layout().connections.find(id);
        const auto visible=found==nativeMap_->layout().connections.end()?level.visible:found->second.visible;
        for(int destination:visible) if(destination>0 && std::find(navigationConnections_[id].begin(),navigationConnections_[id].end(),destination)==navigationConnections_[id].end())
            navigationConnections_[id].push_back(destination);
    }
}
const std::vector<RemoteTown::NavigationRoom> &RemoteTown::navigationRooms() const {
    refreshNavigationCache();return navigationRooms_;
}
std::optional<RemoteTown::NavigationObservation> RemoteTown::navigationObservation() const {
    if(!nativeMap_ || !catalog_ || !view_.nativeMapReady || !view_.area ||
        observationExtent_.x<=0 || observationExtent_.y<=0) return {};
    const int level=*view_.area,act=catalog_->level(level).act;
    const auto layer=explored_.layers().find(RegionId(level));
    const auto placement=nativeMap_->layout().levels.find(level);const auto seed=actSeeds_.find(act);
    if(layer==explored_.layers().end() || placement==nativeMap_->layout().levels.end() || seed==actSeeds_.end()) return {};
    const auto &known=layer->second;const auto &bounds=placement->second;
    Fingerprint fingerprint;
    for(const auto value:{int64_t(seed->second),int64_t(act),int64_t(level),int64_t(bounds.x),int64_t(bounds.y)})
        fingerprint.add(std::to_string(value));
    if(known.width!=bounds.width+1 || known.height!=bounds.height+1 || known.layoutFingerprint!=fingerprint.value()) return {};
    return NavigationObservation{bounds.x,bounds.y,known.width,known.height,known.seen,observationExtent_,
        catalog_->level(level).generation==GenerationKind::Outdoor};
}
std::vector<RemoteTown::KnownTerrainTile> RemoteTown::knownNavigationTerrain() const {
    std::vector<KnownTerrainTile> result;
    if(!view_.nativeMapReady || !map_ || !view_.origin) return result;
    // NativeMapSnapshot contains only loaded rooms with complete collision.
    // Room rectangles are storage ownership here, never exploration targets.
    for(const auto &room:map_->terrain.rooms)
        for(int y=room.y;y<room.y+room.height;++y) for(int x=room.x;x<room.x+room.width;++x)
            result.push_back({uint16_t(room.level),x+view_.origin->x/5,y+view_.origin->y/5});
    return result;
}
std::vector<std::pair<int,int>> RemoteTown::knownNavigationTerrain(uint16_t level) const {
    std::vector<std::pair<int,int>> result;
    for(const auto &entry:knownNavigationTerrain()) if(entry.level==level) result.emplace_back(entry.x,entry.y);
    return result;
}
std::vector<RemoteTown::NavigationWall> RemoteTown::observedNavigationWalls(uint16_t level) const {
    std::vector<NavigationWall> result;
    for(const auto &entry:observedTerrain_) {
        const auto [area,x,y,type,main,sub,wall]=entry;
        if(area==level && wall && type>=1 && type<=12) result.push_back({x,y,uint32_t((main<<16)|(sub<<8)|type)});
    }
    return result;
}
RemoteTown::NativeCells RemoteTown::naturalAutomapCells(uint16_t area,std::optional<uint32_t> objectSeed) const {
    if(!objectSeed && automapVisitedSeeds_) objectSeed=automapVisitedSeeds_->second;
    NativeCells result;
    if(!catalog_ || !catalog_->levels().contains(area)) return result;
    const auto &level=catalog_->level(area);const auto seed=actSeeds_.find(level.act);
    if(seed==actSeeds_.end()) return result;
    for(const auto &[key,groups]:nativeAutomapCells_) {
        if(std::get<0>(key)!=seed->second || std::get<1>(key)!=uint32_t(level.automapLayer)) continue;
        for(size_t group=0;group<groups.size();++group) {
            if(group==2 && (!objectSeed || std::get<2>(key)!=*objectSeed)) continue;
            for(const auto &cell:groups[group]) insertNativeCell(result[group],cell);
        }
    }
    return result;
}
const RemoteTown::SketchArt &RemoteTown::sketchArt(const std::string &path) const {
    if(const auto found=sketchArt_.find(path);found!=sketchArt_.end()) return found->second;
    SketchArt result;result.geometry=decodeDc6(archives_.read(path));
    for(auto &frame:result.geometry.frames) {
        result.ink.push_back(automapInk(frame));Bytes{}.swap(frame.pixels);
    }
    return sketchArt_.emplace(path,std::move(result)).first->second;
}
std::shared_ptr<const AutomapSketch> RemoteTown::navigationSketch(uint16_t area) const {
    if(!catalog_ || !catalog_->levels().contains(area)) return {};
    const auto key=std::tuple{gameGeneration_,automapDataRevision_,area,automapVisitedSeeds_};
    if(sketch_ && sketchKey_==key) return sketch_;
    auto result=std::make_shared<AutomapSketch>();result->revision=automapDataRevision_;result->area=area;
    const auto cells=naturalAutomapCells(area);
    const auto &art=sketchArt("data/global/ui/automap/maximap.dc6");
    const auto append=[&](Vec anchor,const std::vector<Vec> &ink) {
        ++result->primitives;
        for(const auto pixel:ink) result->ink.push_back((anchor+pixel)*10);
    };
    // These are the same natural cells rendered before the optional reveal overlay.
    for(const auto &group:cells) for(const auto &cell:group)
        if(cell.cel<art.ink.size()) append({float(cell.x),float(cell.y)},art.ink[cell.cel]);
    const auto &level=catalog_->level(area);const auto seed=actSeeds_.find(level.act);
    if(seed!=actSeeds_.end()) for(const auto &[townKey,town]:discoveredTowns_) {
        if(std::get<0>(townKey)!=seed->second || std::get<1>(townKey)!=level.act || town.level!=area) continue;
        const auto path=townAutomapPath(area,true);if(path.empty()) continue;
        const auto &montage=sketchArt(path);
        const Vec center=project({float(town.center.x),float(town.center.y)})*.1f;
        for(const auto &cell:townAutomapLayout(montage.geometry,area,town.variant,true))
            append(center+cell.offset,montage.ink[cell.frame]);
    }
    if(level.town && nativeMap_ && seed!=actSeeds_.end() &&
        preparedTownAutomaps_.contains({seed->second,level.act,area}) && result->primitives) {
        // Full town artwork identifies the original service sites. Read the
        // matching MPQ town variant, never infer a live NPC or reveal collision.
        const auto appendService=[&](int identity,int x,int y) {
            const auto row=monsterRows_.find(identity);
            if(row==monsterRows_.end() || !monsters_.number(row->second,"npc").value_or(0) ||
                monsters_.number(row->second,"isAtt").value_or(0) || x<0 || y<0 ||
                x>UINT16_MAX || y>UINT16_MAX) return;
            result->townServices.push_back({uint16_t(identity),{float(x),float(y)}});
        };
        for(const auto &entry:nativeMap_->townPresetPositions(area)) appendService(entry[0],entry[1],entry[2]);
        std::set<int> directServices;
        for(const auto &entry:result->townServices) directServices.insert(entry.identity);
        // D2MOO Objects.InitFn town service markers. The callback identifies
        // the service; MPQ resolves its class and authored location. These
        // sites remain hints, even before a quest permits the actual NPC.
        const auto service=[&](int init)->std::string_view {
            if(area==1 && init==54) return "cain5";
            if(area==40 && (init==18 || init==19)) return "jerhyn";
            if(area==75 && (init==49 || init==50)) return "hratli";
            if(area==109) switch(init) {
                case 66:return "drehya";
                case 68:return "nihlathak";
                case 70:return "qual-kehk";
                case 71:return "larzuk";
            }
            return {};
        };
        for(const auto &entry:nativeMap_->townPresetPositions(area,2)) {
            const auto object=objectRows_.find(entry[0]);if(object==objectRows_.end()) continue;
            const auto code=service(objects_.number(object->second,"InitFn").value_or(0));if(code.empty()) continue;
            for(const auto &[identity,row]:monsterRows_) if(monsters_.value(row,"Id")==code) {
                // Do not turn a unique direct service into an ambiguous clue
                // by adding its alternate story marker as another home.
                if(!directServices.contains(identity)) appendService(identity,entry[1],entry[2]);
                break;
            }
        }
    }
    sketchKey_=key;sketch_=std::move(result);return sketch_;
}
std::optional<OnlinePoint> RemoteTown::knownTargetPosition(uint16_t area,uint8_t type,uint16_t identity) const {
    const auto found=knownTargetPositions_.find({area,type,identity});
    return found==knownTargetPositions_.end()?std::nullopt:std::optional{found->second};
}
std::optional<RemoteTown::KnownTownFrame> RemoteTown::knownTownFrame(uint16_t area) const {
    if(!catalog_ || !nativeMap_ || !catalog_->levels().contains(area)) return {};
    const auto &level=catalog_->level(area);const auto seed=actSeeds_.find(level.act);
    if(!level.town || seed==actSeeds_.end() || !preparedTownAutomaps_.contains({seed->second,level.act,area}) ||
        !nativeMap_->layout().levels.contains(area)) return {};
    // The whole town drawing is already disclosed. This frame aligns configured
    // service-site experience with that visible variant; it adds no terrain.
    const auto &placement=nativeMap_->layout().levels.at(area);
    return KnownTownFrame{placement.presetVariant.value_or(0),
        {uint16_t(placement.x*5),uint16_t(placement.y*5)},placement.width*5,placement.height*5};
}
std::optional<OnlinePoint> RemoteTown::automapNpcPosition(uint16_t area,uint16_t identity) const {
    const auto sketch=navigationSketch(area);if(!sketch) return {};
    std::optional<OnlinePoint> result;
    for(const auto &entry:sketch->townServices) if(entry.identity==identity) {
        const OnlinePoint point{uint16_t(entry.position.x),uint16_t(entry.position.y)};
        if(result && *result!=point) return {}; // Ambiguous service identity is not a unique clue.
        result=point;
    }
    return result;
}
std::optional<OnlinePoint> RemoteTown::automapTargetPosition(uint16_t area,uint16_t identity) const {
    return automapObjectPosition(area,identity);
}
std::optional<OnlinePoint> RemoteTown::automapObjectPosition(uint16_t area,std::optional<uint16_t> identity) const {
    if(!catalog_ || !nativeMap_ || !nativeMap_->layout().levels.contains(area)) return {};
    const auto &level=catalog_->level(area);
    if(!identity && (level.waypoint<0 || level.waypoint>=112)) return {};
    const auto seed=actSeeds_.find(level.act);
    if(seed==actSeeds_.end()) return {};
    std::set<int> cels;
    for(const auto &[object,row]:objectRows_) {
        if(identity?object!=*identity:objects_.number(row,"OperateFn")!=23) continue;
        const int cel=automap_.objectCel(object);
        if(cel>0) cels.insert(cel);
    }
    for(const auto &[object,row]:objectRows_) {
        if(identity?object==*identity:objects_.number(row,"OperateFn")==23) continue;
        cels.erase(automap_.objectCel(object));
    }
    const auto positions=automapPositions(area,cels,2,true);
    if(positions.size()!=1) return {};
    return positions.front();
}
std::vector<OnlinePoint> RemoteTown::automapPositions(uint16_t area,const std::set<int> &cels,size_t group,bool object) const {
    std::vector<OnlinePoint> result;
    if(cels.empty() || group>=4 || !catalog_ || !nativeMap_ || !nativeMap_->layout().levels.contains(area)) return result;
    const auto &level=catalog_->level(area);
    const auto seed=actSeeds_.find(level.act);
    if(seed==actSeeds_.end()) return result;
    const auto &bounds=nativeMap_->layout().levels.at(area);
    {
        const auto groups=naturalAutomapCells(area);
        for(const auto &cell:groups[group]) {
            if(!cels.contains(cell.cel)) continue;
            const int savedX=int(cell.x)-(object?1:0),savedY=int(cell.y)+(object?3:0);
            Vec point;
            if(object) {
                point=unproject({float(savedX*10)+(savedX<0?-4.5f:savedX>0?4.5f:0.f),
                    float(savedY*10)+(savedY<0?-4.5f:savedY>0?4.5f:0.f)});
            } else {
                if(savedX%8 || savedY%4) continue;
                const int difference=savedX/8,sum=savedY/4;
                if((sum+difference)%2 || (sum-difference)%2) continue;
                point={float((sum+difference)/2*5),float((sum-difference)/2*5)};
            }
            if(point.x<bounds.x*5 || point.y<bounds.y*5 || point.x>=(bounds.x+bounds.width)*5 ||
                point.y>=(bounds.y+bounds.height)*5 || point.x<0 || point.y<0 || point.x>UINT16_MAX || point.y>UINT16_MAX) continue;
            const bool ambiguous=std::any_of(nativeMap_->layout().levels.begin(),nativeMap_->layout().levels.end(),[&](const auto &entry) {
                const auto &[id,other]=entry;
                return id!=area && catalog_->level(id).automapLayer==level.automapLayer && point.x>=other.x*5 &&
                    point.y>=other.y*5 && point.x<(other.x+other.width)*5 && point.y<(other.y+other.height)*5;
            });
            if(ambiguous) continue;
            const OnlinePoint candidate{uint16_t(std::lround(point.x)),uint16_t(std::lround(point.y))};
            if(candidate.x<bounds.x*5 || candidate.y<bounds.y*5 || candidate.x>=(bounds.x+bounds.width)*5 ||
                candidate.y>=(bounds.y+bounds.height)*5) continue;
            if(std::none_of(result.begin(),result.end(),[&](OnlinePoint old) {
                return std::abs(int(old.x)-candidate.x)<=2 && std::abs(int(old.y)-candidate.y)<=2;
            })) result.push_back(candidate);
        }
    }
    return result;
}
std::vector<OnlinePoint> RemoteTown::automapEntrancePositions(uint16_t area) const {
    if(!catalog_ || !catalog_->levels().contains(area)) return {};
    const auto terrainCels=automap_.entranceCels(catalog_->level(area).levelType);
    auto result=automapPositions(area,terrainCels,0,false);
    const auto walls=automapPositions(area,terrainCels,1,false);
    result.insert(result.end(),walls.begin(),walls.end());
    const auto entrance=[](int operation) {
        return operation==15 || operation==16 || operation==34 || operation==43 || operation==44 ||
            operation==46 || operation==47 || operation==50 || operation==70 || operation==72 || operation==73;
    };
    std::set<int> objectCels;
    for(const auto &[object,row]:objectRows_)
        if(entrance(objects_.number(row,"OperateFn").value_or(0)) && automap_.objectCel(object)>0)
            objectCels.insert(automap_.objectCel(object));
    for(const auto &[object,row]:objectRows_)
        if(!entrance(objects_.number(row,"OperateFn").value_or(0))) objectCels.erase(automap_.objectCel(object));
    const auto objects=automapPositions(area,objectCels,2,true);
    result.insert(result.end(),objects.begin(),objects.end());
    std::sort(result.begin(),result.end(),[](OnlinePoint first,OnlinePoint second) {
        return std::tie(first.x,first.y)<std::tie(second.x,second.y);
    });
    result.erase(std::unique(result.begin(),result.end()),result.end());
    return result;
}
std::optional<OnlinePoint> RemoteTown::knownConnectionPosition(uint16_t from,uint16_t to) const {
    const auto found=knownConnections_.find({from,to});return found==knownConnections_.end()?std::nullopt:std::optional{found->second};
}
std::optional<uint16_t> RemoteTown::assignedUnitLevel(OnlineUnitKey key) const {
    const auto it=unitRooms_.find(key);
    if(!view_.nativeMapReady || !nativeMap_ || it==unitRooms_.end()) return {};
    return uint16_t(nativeMap_->tiles().rooms().at(it->second.room).level);
}
std::optional<OnlinePoint> RemoteTown::knownWaypointPosition(uint16_t area) const {
    const auto found=knownWaypoints_.find(area);
    if(found!=knownWaypoints_.end()) return found->second;
    return automapObjectPosition(area,{});
}
const std::map<int,std::vector<int>> &RemoteTown::navigationConnections() const {
    refreshNavigationCache();
    // Remember only directions actually observed in this game. A remembered
    // position guides reacquisition; it never authorizes an absent warp unit.
    for(const auto &[edge,position]:knownConnections_) {
        (void)position;
        auto &destinations=navigationConnections_[edge.first];
        if(std::find(destinations.begin(),destinations.end(),edge.second)==destinations.end()) destinations.push_back(edge.second);
    }
    return navigationConnections_;
}
uint64_t RemoteTown::navigationRevision() const {
    uint64_t revision=(gameGeneration_*0x100000001b3ULL+areaGeneration_)*0x100000001b3ULL;
    if(nativeMap_) revision+=nativeMap_->terrainRevision();
    if(map_) revision=revision*0x100000001b3ULL+map_->grid.navigationRevision();
    if(view_.origin) revision=(revision*0x100000001b3ULL+view_.origin->x)*0x100000001b3ULL+view_.origin->y;
    return revision;
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
std::optional<int> RemoteTown::projectedObjectMode(const OnlineView &v,OnlineUnitKey target) const {
    const auto unit=v.world.units.find(target);
    if(target.type!=2 || unit==v.world.units.end() || !unit->second.classId || !unit->second.mode) return {};
    const auto row=objectRows_.find(*unit->second.classId);if(row==objectRows_.end()) return {};
    const auto now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    const auto received=unit->second.actionReceivedMilliseconds;
    const float age=received && now>=received?float(now-received)/1000.f:0.f;
    // Shared silent ENDANIM projection, never a write to the server replica.
    return objectDisplayMode(objects_,row->second,*unit->second.mode,age);
}
bool RemoteTown::interactionReady(const OnlineView &v, OnlineUnitKey target, std::optional<Vec> displayOrigin, bool approaching, std::optional<OnlinePoint> targetPath, std::optional<int> followRange) const {
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
    const auto point = targetPath.value_or(found->position);
    if (followRange && found->interaction == OnlineMapInteraction::Npc) {
        if (!map_ || !view_.origin) return false;
        const Vec origin{float(view_.origin->x), float(view_.origin->y)};
        return playerMeleeReach(map_->grid, observer - origin,
            Vec{float(point.x), float(point.y)} - origin, found->collisionWidth, *followRange);
    }
    const int clickRange = corpse || tradePeer ? 8 : v.protocolProfile == "lod113c" ? 2 : 6;
    return nativeUnitDistance(observer, 2, {float(point.x), float(point.y)}, found->collisionWidth) <= (approaching ? 0 : clickRange);
}
void RemoteTown::prepareTownAutomap(const OnlineView &v) {
    if (!view_.nativeMapReady || !view_.area || !v.load.mapSeed || !v.load.act) return;
    const auto &level = catalog_->level(*view_.area);
    if (!level.town) return;
    const auto seed = *v.load.mapSeed;
    const int act = *v.load.act;
    const auto key = std::tuple{seed, act, level.id};
    if (preparedTownAutomaps_.contains(key)) return;
    // Preparation may publish a partial result before a resource failure.
    ++automapDataRevision_;
    const auto &placement = nativeMap_->layout().levels.at(level.id);
    if (level.id == 40 || level.id == 103 || level.id == 109) {
        const auto recipe = nativeMap_->recipe(level.id);
        discoveredTowns_[key] = {uint16_t(level.id), recipe.variant,
            OnlinePoint{uint16_t(placement.x * 5 + placement.width * 5 / 2),
                uint16_t(placement.y * 5 + placement.height * 5 / 2)}};
    } else {
        // DRLGPRESET_GenerateLevel initializes every AutoMap preset room before its
        // callback. Read those original tiles without changing room sight refs
        // or revealing the adjoining wilderness through the camera viewport.
        const auto snapshot = nativeMap_->snapshot(level.id, false);
        const auto &terrain = snapshot.map.terrain;
        for (const auto &instance : terrain.instances) {
            if (instance.type == 13 || instance.type == 15 || (instance.flags & 8)) continue;
            const auto *tile = terrain.tiles.at(size_t(instance.tile));
            const MapCell cell{uint32_t((tile->main << 20) | (tile->sub << 8)),instance.type};
            const auto cels=automap_.cellCels(level.levelType,cell,
                snapshot.tileX+instance.x-placement.x,snapshot.tileY+instance.y-placement.y);
            rememberTile(v, level.id, snapshot.tileX + instance.x, snapshot.tileY + instance.y,
                instance.type, instance.wallArray, cels);
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
void RemoteTown::rememberTile(const OnlineView &v, int levelId, int x, int y, int type, bool wallArray, std::span<const int> cels) {
    const auto &level = catalog_->level(levelId);
    if (level.automapLayer < 0 || level.automapLayer >= 100 || !v.load.mapSeed || !v.load.secondarySeed) return;
    // D2Client 0x61880: GameTileToClientCoords / 10; lower walls (16+)
    // receive the native 24-pixel vertical adjustment before serialization.
    const int px = (x-y)*8, py = (x+y)*4 + (type >= 16 ? 24 : 0);
    if (px < INT16_MIN || px > INT16_MAX || py < INT16_MIN || py > INT16_MAX)
        throw std::runtime_error("Automap cell outside native coordinate range");
    auto &cells = nativeAutomapCells_[{*v.load.mapSeed, uint32_t(level.automapLayer), *v.load.secondarySeed}];
    for (const int cel : cels)
        if (cel >= 0 && size_t(cel) < nativeAutomapCelCount_ && cel <= INT16_MAX)
            if(insertNativeCell(cells[wallArray ? 1 : 0], {uint16_t(cel), int16_t(px), int16_t(py)}))
                ++automapDataRevision_;
}
void RemoteTown::record_automap_room_pd2() {
    automapRoomCallbacksPd2_.push_back(uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count()));
}
void RemoteTown::refresh_automap_rooms_pd2(const OnlineView &v) {
    if (!reportAutomapRoomsPd2_ || !v.remoteLobbyOnly || !nativeMap_ || !nativePlayerRoom_ || !v.load.playerUnitId) return;
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
void RemoteTown::revealVisibleTiles(const OnlineView &v, std::span<const size_t> indices,Vec observationExtent) {
    if (!view_.nativeMapReady || !map_ || !v.load.mapSeed || !v.load.act ||
        v.gameGeneration != gameGeneration_ || v.world.areaGeneration != areaGeneration_) return;
    observationExtent_=observationExtent;
    const auto seed = *v.load.mapSeed;
    const int act = *v.load.act;
    const auto &terrain = map_->terrain;
    const auto seeds=std::pair{seed,v.load.secondarySeed};
    if(automapVisitedSeeds_!=seeds) {
        std::fill(automapVisitedTiles_.begin(),automapVisitedTiles_.end(),uint8_t{0});
        automapVisitedSeeds_=seeds;
        automapViewportPrepared_=false;
        automapDrawnTiles_.clear();automapRevealPosition_.reset();
    }
    const auto tileKey=[&](const MapTerrain::Instance &instance) {
        const auto *tile=terrain.tiles.at(size_t(instance.tile));
        return AutomapTileKey{terrain.rooms.at(instance.room).level,instance.x+view_.origin->x/5,
            instance.y+view_.origin->y/5,instance.type,tile->main,tile->sub,instance.wallArray};
    };
    for(const auto index:indices) {
        if(index>=terrain.instances.size()) continue;
        const auto &instance=terrain.instances[index];
        if(instance.type==13 || instance.type==15 || (instance.flags&8)) continue;
        const auto key=tileKey(instance);
        observedTerrain_.insert(key);
        if(!automapVisitedTiles_.at(index)) automapDrawnTiles_.insert(key);
    }
    const auto position=displayPlayerPosition_?displayPlayerPosition_:v.world.playerPosition;
    if(!position || !nativePlayerRoom_) return;
    const auto projected=project({float(position->x),float(position->y)});
    const auto clientPosition=automapClientPosition_.value_or(std::pair{int(projected.x),int(projected.y)+8});
    const int dx=std::abs(clientPosition.first-(automapRevealPosition_?automapRevealPosition_->first:0));
    const int dy=std::abs(clientPosition.second-(automapRevealPosition_?automapRevealPosition_->second:0));
    const bool retail=v.protocolProfile=="lod113c";
    if(retail && std::max(dx,dy)+std::min(dx,dy)/2<80) return;
    automapRevealPosition_=clientPosition;
    const auto &nativeRooms=nativeMap_->tiles().rooms();
    const auto &observerRoom=nativeRooms.at(*nativePlayerRoom_);
    const auto observerLayer=catalog_->level(observerRoom.level).automapLayer;
    const auto nearRoom=[&](const MapTerrain::PreparedRoom &room) {
        if(catalog_->level(room.level).automapLayer!=observerLayer) return false;
        const auto matches=[&](size_t index) {
            const auto &native=nativeRooms.at(index);
            return native.loaded && native.level==room.level && native.room.x==room.x+view_.origin->x/5 &&
                native.room.y==room.y+view_.origin->y/5;
        };
        return matches(*nativePlayerRoom_) || std::any_of(observerRoom.activeNear.begin(),observerRoom.activeNear.end(),matches);
    };
    std::optional<MapSceneView> scene;
    // Even an empty viewport initializes the loaded regions once, as before.
    if(!automapViewportPrepared_) scene=automapScene(v);
    std::vector<AutomapVisibleCell> visible;
    std::vector<size_t> processed;
    for (size_t index=0;index<terrain.instances.size();++index) {
        const auto &instance = terrain.instances[index];
        if (instance.type == 13 || instance.type == 15 || (instance.flags & 8)) continue;
        if(automapVisitedTiles_.at(index)) continue;
        const auto &room = terrain.rooms.at(instance.room);
        if((retail && !nearRoom(room)) || !automapDrawnTiles_.contains(tileKey(instance))) continue;
        if(!scene) scene=automapScene(v);
        const auto &level = catalog_->level(room.level);
        const auto &placement = nativeMap_->layout().levels.at(room.level);
        const int x = instance.x + view_.origin->x / 5, y = instance.y + view_.origin->y / 5;
        const Discovery key{seed, act, room.level, x, y};
        const auto region = std::find_if(scene->regions.begin(), scene->regions.end(),
            [&](const auto &entry) { return entry.id == RegionId(room.level); });
        if (region != scene->regions.end())
            visible.push_back({int(region - scene->regions.begin()), x - placement.x, y - placement.y});
        const auto *tile = terrain.tiles.at(size_t(instance.tile));
        MapCell cell{uint32_t((tile->main << 20) | (tile->sub << 8)), instance.type};
        const auto cels=automap_.cellCels(level.levelType,cell,x-placement.x,y-placement.y);
        rememberTile(v, room.level, x, y, instance.type, instance.wallArray, cels);
        for (int cel : cels)
            if(discoveredCels_[key].insert(cel).second) ++automapDataRevision_;
        processed.push_back(index);
    }
    if(scene) {
        explored_.reveal(*scene, visible);
        // Commit only after both consumers accepted this viewport. Restoring
        // navigation memory or replacing terrain clears these derived marks.
        for(const auto index:processed) {
            automapVisitedTiles_[index]=1;automapDrawnTiles_.erase(tileKey(terrain.instances[index]));
        }
        automapViewportPrepared_=true;
    }
    refresh_automap_rooms_pd2(v);
    updateAutomapView(v);
}
void RemoteTown::updateAutomapReveal(const OnlineView &v, bool enabled, AutomapPresetCellSelector selector) {
    if (enabled && v.stage == OnlineStage::ProtocolReady && view_.nativeMapReady && view_.area &&
        v.load.mapSeed && v.load.secondarySeed && v.load.act && v.load.difficulty &&
        v.gameGeneration == gameGeneration_ && v.world.areaGeneration == areaGeneration_) {
        revealed_.update({v.gameGeneration, v.world.areaGeneration, *v.load.mapSeed, *v.load.secondarySeed,
            *v.load.act, *v.load.difficulty}, *view_.area, selector);
    } else revealed_.disable();
    updateAutomapView(v);
}
void RemoteTown::updateAutomapBase(const OnlineView &v) {
    const AutomapProjectionKey sources{automapTerrainRevision_,explored_.revision(),automapDataRevision_,
        revealed_.revision(),*v.load.mapSeed,v.load.secondarySeed,*v.load.act,*view_.area};
    if(automapProjection_ && *automapProjection_==sources) return;
    // Remember visited walking neighbours; stairs and portals keep separate layers.
    if(!automapProjection_ || automapProjection_->terrain!=sources.terrain ||
        automapProjection_->seed!=sources.seed || automapProjection_->area!=sources.area ||
        automapProjection_->act!=sources.act) {
        automapComponent_={*view_.area};
        std::vector<int> pending{*view_.area};
        for(size_t next=0;next<pending.size();++next) {
            const auto found=nativeMap_->layout().connections.find(pending[next]);
            if(found==nativeMap_->layout().connections.end()) continue;
            const auto &slots=found->second;
            for(size_t i=0;i<slots.visible.size();++i)
                if(slots.visible[i] && slots.warps[i]==-1 && catalog_->level(slots.visible[i]).act==*v.load.act &&
                    automapComponent_.insert(slots.visible[i]).second) pending.push_back(slots.visible[i]);
        }
    }
    const auto &component=automapComponent_;
    view_.automapStamps.clear();view_.automapTowns.clear();view_.automapRevealedCells.clear();
    view_.automap.stamps.clear();view_.automap.towns.clear();view_.automap.lkChestLines.clear();
    for (const auto &[id, layer] : explored_.layers()) {
        const int level = int(id);
        if (!component.contains(level) || !nativeMap_->layout().levels.contains(level)) continue;
        const auto &placement = nativeMap_->layout().levels.at(level);
        Fingerprint fingerprint;
        fingerprint.add(std::to_string(*v.load.mapSeed)); fingerprint.add(std::to_string(*v.load.act));
        fingerprint.add(std::to_string(level)); fingerprint.add(std::to_string(placement.x)); fingerprint.add(std::to_string(placement.y));
        if(layer.width!=placement.width+1 || layer.height!=placement.height+1 || layer.layoutFingerprint!=fingerprint.value()) continue;
        if(const auto count=explored_.revealedCount(id)) view_.automapRevealedCells[uint16_t(level)]=count;
        const auto first=view_.automapStamps.size();
        const auto minimum=std::numeric_limits<int>::min();
        for(auto cell=discoveredCels_.lower_bound({*v.load.mapSeed,*v.load.act,level,minimum,minimum});cell!=discoveredCels_.end();++cell) {
            const auto &[seed,act,cellLevel,gx,gy]=cell->first;
            if(seed!=*v.load.mapSeed || act!=*v.load.act || cellLevel!=level) break;
            const int x=gx-placement.x,y=gy-placement.y;
            if(x<0 || y<0 || x>=layer.width || y>=layer.height || !layer.seen[size_t(y)*layer.width+x]) continue;
            for(const auto cel:cell->second) view_.automapStamps.push_back({uint16_t(level),uint16_t(gx),uint16_t(gy),cel});
        }
        // Discovery keys sort by x/y; retain the previous row-major output.
        std::sort(view_.automapStamps.begin()+first,view_.automapStamps.end(),[](const auto &a,const auto &b) {
            return std::tie(a.tileY,a.tileX,a.cel)<std::tie(b.tileY,b.tileX,b.cel);
        });
    }
    for (const auto &[key, town] : discoveredTowns_) {
        const auto &[seed, act, level] = key;
        if (seed == *v.load.mapSeed && act == *v.load.act && component.contains(level))
            view_.automapTowns.push_back(town);
    }
    // BH LK Chest Lines: endpoints for every Lower Kurast (level 79) superchest
    // preset (Objects.txt 160), independent of exploration.
    if (view_.area && *view_.area == 79 && nativeMap_)
        for (const auto &preset : nativeMap_->levelPresetObjects(79))
            if (preset[0] == 160)
                view_.automap.lkChestLines.push_back({float(preset[1]), float(preset[2])});
    NativeCells displayCells=naturalAutomapCells(*view_.area,v.load.secondarySeed);
    const int displayLayer=catalog_->level(*view_.area).automapLayer;
    // Merge at display time only. Natural discoveries win variant collisions;
    // disabling reveal removes the overlay without rewriting native caches.
    revealed_.appendCells(displayLayer, displayCells);
    for (const auto &group : displayCells) for (const auto &cell : group)
        view_.automap.stamps.push_back({unproject({cell.x * 10.f, cell.y * 10.f}), cell.cel});
    for (const auto &town : view_.automapTowns)
        view_.automap.towns.push_back({town.level, town.variant, {float(town.center.x), float(town.center.y)}});
    revealed_.appendTowns(displayLayer, view_.automap.towns);
    automapProjection_=sources;
}
void RemoteTown::updateAutomapView(const OnlineView &v) {
    view_.automapReveal = revealed_.progress();
    if (!nativeMap_ || !view_.area || !v.load.mapSeed || !v.load.act) {
        automapProjection_.reset();
        view_.automapStamps.clear();view_.automapTowns.clear();view_.automapRevealedCells.clear();view_.automap={};
        return;
    }
    updateAutomapBase(v);
    auto &[areaName, version, difficulty, expansion] = view_.automap.infoLines;
    const bool retail = v.protocolProfile == "lod113c";
    if (retail && *view_.area == 8) {
        // D2Client BE240 selects the Den's name using byte 1 of the 0x5E quest data.
        const auto &quests = v.world.quests.availability;
        areaName = quests ? strings_.find((*quests)[1] ? 5048 : 5047) : std::string_view{};
    } else {
        areaName = strings_.find(catalog_->level(*view_.area).name);
    }
    version = retail ? "v 1.13" : "";
    difficulty.clear();
    if (v.load.difficulty == 1 || v.load.difficulty == 2) {
        const auto label = strings_.find(*v.load.difficulty == 1 ? 5155 : 5154);
        const auto format = strings_.find(4183);
        const auto slot = format.find("%s");
        if (!label.empty() && slot != std::string_view::npos) {
            difficulty = format;
            difficulty.replace(slot, 2, label);
        }
    }
    expansion = v.load.expansion ? strings_.find(22730) : std::string_view{};
    const auto &component=automapComponent_;
    view_.automap.markers.clear();view_.automap.pathLines.clear();
    view_.automap.observer=v.world.playerPosition?Vec{float(v.world.playerPosition->x),float(v.world.playerPosition->y)}:Vec{};
    view_.automap.observerVisible=!onlinePlayerDead(v.world);
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
                    view_.automap.markers.push_back({{float(unit.position->x),float(unit.position->y)},-1,unit.name,false,true,Mark::Corpse,std::pair{key.type,key.id},0,{},{}});
            } else if(key.id!=v.load.playerUnitId && unit.mode!=(unit.nativeMode?0:8) && unit.mode!=(unit.nativeMode?17:9)) {
                const auto roster=v.world.social.players.find(key.id);
                const auto &name=roster==v.world.social.players.end()?unit.name:roster->second.name;
                view_.automap.markers.push_back({{float(unit.position->x),float(unit.position->y)},-1,name,false,true,inParty(key.id)?Mark::Party:Mark::OtherPlayer,std::pair{key.type,key.id},0,{},{}});
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
                if(insertNativeCell(nativeAutomapCells_[{*v.load.mapSeed, uint32_t(layer), *v.load.secondarySeed}][2],
                    {uint16_t(cel), int16_t(x), int16_t(y)})) ++automapDataRevision_;
            cel = -1;
        }
        const auto target = std::find_if(view_.mapTargets.begin(), view_.mapTargets.end(),
            [&](const auto &entry) { return entry.unit == key; });
        const bool showName = target != view_.mapTargets.end() &&
            (target->interaction == OnlineMapInteraction::Npc || target->interaction == OnlineMapInteraction::Stash);
        if (showName) name = target->name;
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
                mark == Mark::None ? cel : -1, name, mark==Mark::Npc, showName || mark==Mark::Npc, mark,std::pair{key.type,key.id},0,{},{}});
    }
    if (self == v.world.social.players.end() || !self->second.partyId || *self->second.partyId == UINT16_MAX) return;
    for (const auto &[id, p] : v.world.social.players) {
        if (id == *v.load.playerUnitId || drawnPlayers.contains(id) || !p.listed || p.partyId != self->second.partyId) continue;
        std::optional<Vec> position;
        if (const auto unit = v.world.units.find({0, id}); unit != v.world.units.end() && unit->second.position)
            position = Vec{float(unit->second.position->x), float(unit->second.position->y)};
        else if (p.area && component.contains(*p.area) && p.positionX && p.positionY)
            position = Vec{float(*p.positionX), float(*p.positionY)};
        if (position) view_.automap.markers.push_back({*position, -1, p.name, false, true, AutomapDrawView::UnitMark::Party,{},0,{},{}});
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
RemoteTown::NavigationMemory RemoteTown::navigationMemory() const {
    NavigationMemory result;result.targets=knownTargetPositions_.size();result.connections=knownConnections_.size();
    result.waypoints=knownWaypoints_.size();
    for(const auto &[id,layer]:explored_.layers()) {(void)layer;result.observedCells+=explored_.revealedCount(id);}
    return result;
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
        ++automapDataRevision_;
    }
    updateAutomapView(v);
}
} // namespace d2x
