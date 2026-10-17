#include "native_map.hpp"
#include <algorithm>
#include <stdexcept>
#include <variant>

namespace d2x {
namespace {
struct RoomInput {
    RoomTerrainLink link;
    RetailRoom room;
    size_t allocationOrder{};
    std::shared_ptr<RetailTileSelector> libraries;
    std::array<int,8> warpSlots{};
    std::vector<std::pair<int,RoomTerrainId>> warps;
    std::variant<std::monostate,RetailOutdoorRoomData,RetailPresetRoomData> data;
    std::shared_ptr<const MapData> unitSource;
    bool preloaded{};
    std::vector<RetailPresetUnit> preloadedUnits;
    explicit RoomInput(RetailRoom value) : room(std::move(value)) {}
};
class NativePreparedRoomTerrain final : public PreparedRoomTerrain {
  public:
    int actValue{}, difficulty{};
    uint32_t seed{};
    uint64_t mountIdentity{};
    std::shared_ptr<const WorldCatalog> catalog;
    RetailPresetScan identities;
    std::vector<RoomTerrainLink> links;
    std::vector<std::shared_ptr<const RoomInput>> inputs;
    explicit NativePreparedRoomTerrain(const RetailPresetScan &scan) : identities(scan) {}
    int act() const override { return actValue; }
    std::span<const RoomTerrainLink> rooms() const override { return links; }
    std::unique_ptr<RoomTerrainSession> instantiate() const override;
};
class NativeRoomTerrainSession final : public RoomTerrainSession {
    int act_, difficulty_;
    uint32_t seed_;
    uint64_t mountIdentity_;
    std::shared_ptr<const WorldCatalog> catalog_;
    RetailPresetScan identities_;
    RetailTileMaterializer tiles_;
    std::map<RoomTerrainId,size_t> indices_;
    std::map<RoomTerrainId,std::shared_ptr<const RoomInput>> inputs_;
  public:
    explicit NativeRoomTerrainSession(const NativePreparedRoomTerrain &source)
        : act_(source.actValue),difficulty_(source.difficulty),seed_(source.seed),mountIdentity_(source.mountIdentity),
          catalog_(source.catalog),identities_(source.identities),tiles_(*catalog_) { include(source); }
    void include(const PreparedRoomTerrain &value) override {
        const auto *source=dynamic_cast<const NativePreparedRoomTerrain *>(&value);
        if (!source || source->actValue!=act_ || source->difficulty!=difficulty_ || source->seed!=seed_ ||
            source->mountIdentity!=mountIdentity_)
            throw std::runtime_error("Prepared room terrain belongs to another DRLG");
        for (const auto &input : source->inputs) {
            const auto key=input->link.id;
            const auto found=inputs_.find(key);
            if (found==inputs_.end()) {
                const auto id=tiles_.registerRoom(key.level,input->room,input->libraries,input->warpSlots);
                inputs_.emplace(key,input);indices_.emplace(key,id);
            } else {
                const auto &old=*found->second;
                if (old.room.seed.initial!=input->room.seed.initial || old.room.width!=input->room.width ||
                    old.room.height!=input->room.height || old.warpSlots!=input->warpSlots)
                    throw std::runtime_error("Prepared room identity changed within a game");
                if (!old.link.near.empty() && !input->link.near.empty() && old.link.near!=input->link.near)
                    throw std::runtime_error("Prepared native room graph changed");
                // Existing rooms retain their decoded selector and unit state.
                // Only the separate logical links are completed below.
            }
        }
        for (const auto &input : source->inputs) {
            if (input->link.near.empty()) continue;
            std::vector<size_t> near;
            std::vector<std::pair<int,size_t>> warps;
            for (const auto key : input->link.near) near.push_back(indices_.at(key));
            for (const auto &[id,key] : input->warps) warps.emplace_back(id,indices_.at(key));
            tiles_.preparedLinks(indices_.at(input->link.id),near,warps);
        }
    }
    void prepare(RoomTerrainId key) override {
        const auto &input=*inputs_.at(key);
        if (!input.room.preset) return;
        tiles_.preparePresetUnits(indices_.at(key),input.preloaded,
            [&](Seed &random) {
                return identities_.buildUnits(*input.unitSource,catalog_->level(key.level).act,
                    input.room.mapX,input.room.mapY,random);
            },input.preloadedUnits);
    }
    void create(RoomTerrainId key) override {
        const auto &input=*inputs_.at(key);
        const auto index=indices_.at(key);
        const auto &room=tiles_.rooms().at(index);
        if (room.loaded) throw std::runtime_error("Duplicate authoritative room materialization");
        if (room.near.empty()) throw std::runtime_error("Missing prepared native room neighbourhood");
        prepare(key);
        const auto near=room.near;
        if (const auto *outdoor=std::get_if<RetailOutdoorRoomData>(&input.data))
            tiles_.loadOutdoor(index,*outdoor,near);
        else tiles_.loadPreset(index,std::get<RetailPresetRoomData>(input.data),near);
        auto active=near;
        std::erase_if(active,[&](size_t other) {return other!=index && !tiles_.collision(other);});
        tiles_.activateCollision(index,active);
    }
    void release(RoomTerrainId key) override { tiles_.releaseRoom(indices_.at(key)); }
    std::optional<RoomTerrainSample> sample(RoomTerrainId key, bool details,
        std::span<const std::pair<int,int>> openedTombTiles) const override {
        const auto found=indices_.find(key);
        if (found==indices_.end()) return {};
        const auto index=found->second;
        const auto *collision=tiles_.collision(index);
        if (!collision) return {};
        const auto &room=tiles_.rooms().at(index);
        RoomTerrainSample result;
        result.width=collision->width;result.height=collision->height;result.collision=collision->flags;
        if (!openedTombTiles.empty()) {
            auto opened=projectRetailRoomCollision(tiles_,index,room.activeNear,[&](const RetailMaterializedTile &tile) {
                const auto *record=tile.tile;
                const bool wall=record && ((record->main==8 && record->sub<=1 && tile.type>=1 && tile.type<=7) ||
                    (record->main==6 && (record->sub==8 || record->sub==9) && tile.type==1));
                return !wall || std::find(openedTombTiles.begin(),openedTombTiles.end(),std::pair{tile.x,tile.y})==openedTombTiles.end();
            });
            result.collision=std::move(opened.flags);
        }
        result.tileRandom=room.room.seed.random.state();
        result.initialActiveRandom=room.initialActiveRandom;
        for (const auto id : room.activeNear) {
            const auto &other=tiles_.rooms().at(id);
            result.activeNear.push_back({other.level,other.room.x,other.room.y});
        }
        auto &layout=result.population;
        layout.x=key.x;layout.y=key.y;layout.width=room.room.width;layout.height=room.room.height;
        layout.allocationOrder=inputs_.at(key)->allocationOrder;
        layout.waypoint=(room.room.flags&0x30000u)!=0;
        for (size_t slot=0;slot<room.warpSlots.size();++slot)
            layout.warp|=(room.room.flags&(0x10u<<slot))!=0 && room.warpSlots[slot]!=-1;
        if (layout.waypoint) for (const auto &unit : room.authoredUnits)
            if (unit.unit.type==2 && identities_.waypointObject(unit.unit.id)) {
                layout.waypointTile=Vec{float(unit.unit.x/5),float(unit.unit.y/5)};break;
            }
        layout.single=room.singleLogicalRegion;layout.cells=room.logicalCells;
        for (const auto &region : room.logicalRegions)
            layout.regions.push_back({region.index,region.left,region.top,region.right,region.bottom,region.node});
        for (const auto id : room.floors) {
            const auto &tile=tiles_.tiles().at(id);
            layout.floors.push_back({tile.x,tile.y,tile.tile?tile.tile->materialFlags:uint16_t{}});
        }
        if (details) {
            const auto append=[&](const auto &array,int kind) {
                for (const auto id : array) {
                    const auto &tile=tiles_.tiles().at(id);
                    if (!tile.tile) continue;
                    std::optional<std::pair<std::string,size_t>> identity;
                    for (const auto &[other,input] : inputs_) {
                        (void)other;
                        try {const auto [path,record]=input->libraries->identity(*tile.tile);identity={{std::string(path),record}};break;}
                        catch (const std::runtime_error &) {}
                    }
                    if (!identity) throw std::runtime_error("Materialized collision tile lost its prepared record");
                    result.tiles.push_back({kind,tile.x,tile.y,tile.type,tile.flags,
                        identity->first,identity->second,tile.tile->flags});
                }
            };
            append(room.floors,0);append(room.walls,1);append(room.shadows,2);
        }
        return result;
    }
};
std::unique_ptr<RoomTerrainSession> NativePreparedRoomTerrain::instantiate() const {
    return std::make_unique<NativeRoomTerrainSession>(*this);
}
} // namespace

std::shared_ptr<const PreparedRoomTerrain> NativeMapGenerator::prepareRoomTerrain(int level,
    std::shared_ptr<const WorldCatalog> catalog) {
    if (side_!=NativeRoomActivation::Side::Server || &*catalog!=&catalog_ || !activation_.states().empty())
        throw std::runtime_error("Room inputs require a pristine server DRLG");
    auto result=std::make_shared<NativePreparedRoomTerrain>(identities_);
    result->actValue=act_;result->difficulty=difficulty_;result->seed=mapSeed_;result->catalog=std::move(catalog);
    result->mountIdentity=archives_.mountIdentity();
    const auto roots=levelRooms(level);
    std::map<size_t,unsigned> depth;
    std::vector<size_t> queue;
    for (const auto id : roots) {depth.emplace(id,0);queue.push_back(id);}
    for (size_t at=0;at<queue.size();++at) {
        const auto id=queue[at];const auto distance=depth.at(id);
        if (distance>=3) continue;
        for (const auto other : near(id)) if (depth.emplace(other,distance+1).second) queue.push_back(other);
    }
    std::map<const RetailTileSelector *,std::shared_ptr<RetailTileSelector>> selectors;
    std::map<std::string,std::shared_ptr<const MapData>> unitSources;
    const auto key=[&](size_t id) {const auto &r=tiles_.rooms().at(id);return RoomTerrainId{r.level,r.room.x,r.room.y};};
    for (const auto &[id,distance] : depth) {
        const auto &room=tiles_.rooms().at(id);
        auto input=std::make_shared<RoomInput>(room.room);
        input->link={key(id),room.room.width,room.room.height,catalog_.level(room.level).town,{}};
        if (distance<3) {
            for (const auto other : room.near) input->link.near.push_back(key(other));
            for (const auto &warp : room.warps) if (warp.destination) input->warps.emplace_back(warp.id,key(*warp.destination));
        }
        input->room=room.room;input->warpSlots=room.warpSlots;
        const auto &allocated=levels_.at(room.level);
        input->allocationOrder=size_t(std::find(allocated.rooms.begin(),allocated.rooms.end(),id)-allocated.rooms.begin());
        auto &selector=selectors[room.libraries.get()];
        if (!selector) selector=room.libraries->collisionOnly();
        input->libraries=selector;
        if (room.room.preset) {
            const auto &preset=catalog_.presets().at(room.room.preset);
            const auto &path=preset.variants.at(size_t(room.room.file));
            const auto &data=pattern(path);
            input->data=buildRetailPresetRoomData(preset,room.room,data);
            auto &units=unitSources[path];
            if (!units) {auto value=std::make_shared<MapData>();value->version=data.version;value->act=data.act;value->objects=data.objects;units=std::move(value);}
            input->unitSource=units;input->preloaded=preset.scan || preset.pops;
            if (input->preloaded) {
                const RetailPresetKey presetKey{room.level,room.room.preset,room.room.file,room.room.mapX,room.room.mapY};
                if (allocated.outdoor) input->preloadedUnits=allocated.outdoor->presetUnits.at(presetKey);
                else if (allocated.maze) input->preloadedUnits=allocated.maze->presetUnits.at(presetKey);
                else input->preloadedUnits=allocated.preset->units;
            }
        } else {
            const auto &outdoor=*allocated.outdoor;
            // Substitution/shadow choices depend only on the reset room seed;
            // defer linked floor/wall choices to actual room materialization.
            input->data=buildRetailOutdoorRoomData(cache_,catalog_,catalog_.level(room.level),room.room,outdoor.paths,
                [this](const std::string &path)->const MapData & {return pattern(path);},identities_,selector);
        }
        result->links.push_back(input->link);result->inputs.push_back(std::move(input));
    }
    return result;
}
} // namespace d2x
