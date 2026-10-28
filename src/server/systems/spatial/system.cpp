#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/travel/system.hpp"
#include <algorithm>
#include <cmath>
#include <utility>
#include <tuple>
#include <chrono>

namespace d2x::server::spatial {
namespace {
struct CollisionQuery {
    AreaStore &areas;
    EntityId self,target;
    std::optional<NativeFootprint> extra;
    std::optional<AreaStore::CollisionExclusion> exclusion;
    size_t depth{};
    CollisionQuery(AreaStore &owner,EntityId mover,EntityId destination,std::optional<NativeFootprint> footprint)
        :areas(owner),self(mover),target(destination),extra(footprint) {}
    void enter() {
        if(depth++==0) {
            exclusion.emplace(areas,self,target);
            if(extra) areas.writeFootprint(*extra,false);
        }
    }
    void leave() {
        if(!--depth) {
            if(extra) areas.writeFootprint(*extra,true);
            exclusion.reset();
        }
    }
    struct Scope {
        CollisionQuery &query;
        explicit Scope(CollisionQuery &value):query(value) {query.enter();}
        ~Scope() {query.leave();}
    };
};
}
std::optional<NativeFootprint> System::targetFootprint(EntityId target) const {
    if(!target) return {};
    // Actor footprints already belong to CollisionExclusion. Do not search
    // every area's object list for an ordinary unit pursuit target.
    if(ports_.areas.hasRoomUnit(target)) return {};
    const auto item=ports_.items.read().world.items.find(target);
    if(item!=ports_.items.read().world.items.end()) if(const auto *at=std::get_if<GroundLocation>(&item->second.location))
        return ports_.areas.rectangleFootprint(at->region,at->position,1,1,0x200);
    const auto portal=[&](const travel::Portal &value)->std::optional<NativeFootprint> {
        if(value.fieldId==target && value.rule.collision[size_t(value.isOpened(false)?2:1)]) return ports_.areas.rectangleFootprint(value.field,value.fieldPosition,value.rule.width,value.rule.height,value.rule.collisionMask);
        if(value.townId==target && value.rule.collision[size_t(value.isOpened(true)?2:1)]) return ports_.areas.rectangleFootprint(value.town,value.townPosition,value.rule.width,value.rule.height,value.rule.collisionMask);
        return {};
    };
    for(const auto &[id,value]:ports_.travel.read().portals) { (void)id;if(auto footprint=portal(value)) return footprint; }
    for(const auto &[id,value]:ports_.travel.read().specialPortals) { (void)id;if(auto footprint=portal(value)) return footprint; }
    for(const auto &[region,state]:ports_.areas.all()) for(const auto &object:state.definition.collision.obstacles) if(object.id==target) {
        auto footprint=ports_.areas.rectangleFootprint(region,{float(object.x)+float(object.width/2),float(object.y)+float(object.height/2)},object.width,object.height,object.mask);
        return footprint;
    }
    return {};
}
std::function<uint16_t(RoomView,int32_t,int32_t)> System::nativeCollisionSample(uint16_t mask) const {
    return [areas=&ports_.areas,mask](RoomView room,int32_t x,int32_t y)->uint16_t {
        return areas->sampleNativeCollision(room,x,y,mask);
    };
}
Grid System::navigationGrid(RegionId region,EntityId self,EntityId target) const {
    ports_.areas.observeCollisionNavigation();
    const auto &area=ports_.areas.at(region).definition;
    const auto *terrain=&area.collision;
    Grid view;
    view.width=terrain->width;view.height=terrain->height;
    view.neighbours=terrain->neighbours;
    view.obstacleRevision=terrain->obstacleRevision;view.missileCollisionRevision=terrain->missileCollisionRevision;
    Vec navigationOrigin;auto navigation=terrain->linkedNavigation(navigationOrigin);
    auto query=std::make_shared<CollisionQuery>(ports_.areas,self,target,targetFootprint(target));
    view.collisionQuery=[query](bool entering) {if(entering) query->enter();else query->leave();};
    view.collisionSample=[query,navigation=std::move(navigation),navigationOrigin,target](int x,int y,EntityId ignored)->uint16_t {
        const CollisionQuery::Scope scope{*query};
        query->areas.observeCollisionRead();
        return navigation.nativeMovementMask(x-int(navigationOrigin.x),y-int(navigationOrigin.y),ignored?ignored:target);
    };
    return view;
}
Grid System::movementGrid(RegionId region,AreaStore::CollisionMove &movement) const {
    const auto &area=ports_.areas.at(region).definition;
    Grid view;view.width=area.collision.width;view.height=area.collision.height;
    view.collisionStep=[&movement,origin=area.origin](int x,int y,int nextX,int nextY,MovementCollisionRule rule) {
        return movement.step(x+int(origin.x),y+int(origin.y),nextX+int(origin.x),nextY+int(origin.y),rule);
    };
    return view;
}
void System::restoreRoomFootprints(RoomView room) const {
    ports_.areas.restoreUnitCollision(room);
    const auto rectangle=[&](RegionId area,Vec point,int width,int height,uint16_t mask) {
        ports_.areas.writeFootprint(ports_.areas.rectangleFootprint(area,point,width,height,mask),true,room);
    };
    for(const auto &[id,player]:ports_.players.all()) if(player.collisionActive) {
        (void)id;for(const auto &corpse:player.persistent.corpses) rectangle(corpse.region,corpse.position,3,3,0x8000);
    }
    const auto portal=[&](const travel::Portal &value) {
        if(value.rule.collision[size_t(value.isOpened(false)?2:1)]) rectangle(value.field,value.fieldPosition,value.rule.width,value.rule.height,value.rule.collisionMask);
        if(value.rule.collision[size_t(value.isOpened(true)?2:1)]) rectangle(value.town,value.townPosition,value.rule.width,value.rule.height,value.rule.collisionMask);
    };
    for(const auto &[id,value]:ports_.travel.read().portals) {(void)id;portal(value);}
    for(const auto &[id,value]:ports_.travel.read().specialPortals) {(void)id;portal(value);}
}
namespace {
constexpr float cellSize=16.f;
int cell(float value) {return int(std::floor(value/cellSize));}
}
int System::maximumSize(RegionId area) const {
    const auto found=regions_.find(area);
    return found==regions_.end()?0:found->second.maximumSize;
}
DomainResult<std::vector<UnitRef>> System::query(const Query &request) const {
    if(!std::isfinite(request.center.x) || !std::isfinite(request.center.y) || !std::isfinite(request.radius) ||
       request.radius<0 || std::abs(request.center.x)>float(INT32_MAX/4) || std::abs(request.center.y)>float(INT32_MAX/4) || request.radius>float(INT32_MAX/4))
        return {DomainStatus::InvalidRequest,{}};
    if(!state_.generation) return {DomainStatus::Unavailable,{}};
    ++state_.queries;
    std::vector<UnitRef> result;
    const auto found=regions_.find(request.area);
    if(found==regions_.end()) return {DomainStatus::Applied,std::move(result)};
    const int lowX=cell(request.center.x-request.radius),highX=cell(request.center.x+request.radius);
    const int lowY=cell(request.center.y-request.radius),highY=cell(request.center.y+request.radius);
    const auto &cells=found->second.cells;
    for(auto column=cells.lower_bound(lowX);column!=cells.end() && column->first<=highX;++column) {
        for(auto bucket=column->second.lower_bound(lowY);bucket!=column->second.end() && bucket->first<=highY;++bucket)
        for(const auto &[id,entry]:bucket->second) {
            (void)id;
            ++state_.candidates;
            const auto delta=entry.position-request.center;
            if(delta.x*delta.x+delta.y*delta.y<=request.radius*request.radius) result.push_back(entry.unit);
        }
    }
    std::sort(result.begin(),result.end(),[](const auto &first,const auto &second){return first.id<second.id;});
    state_.returned+=result.size();
    return {DomainStatus::Applied,std::move(result)};
}
void System::erase(EntityId id, const Location &location) {
    auto region=regions_.find(location.area);
    auto column=region->second.cells.find(location.x);
    auto bucket=column->second.find(location.y);
    bucket->second.erase(id);
    if(bucket->second.empty()) column->second.erase(bucket);
    if(column->second.empty()) region->second.cells.erase(column);
    if(region->second.cells.empty()) regions_.erase(region);
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    if(state_.generation==UINT64_MAX) {regions_.clear();locations_.clear();state_.generation=0;}
    const auto generation=state_.generation+1;
    for(auto &[id,region]:regions_) {(void)id;region.maximumSize=0;}
    if(state_.indexedTick!=tick.tick) state_.queries=state_.candidates=state_.returned=0;
    const auto insert=[&](RegionId area,Vec position,UnitRef unit,int size) {
        const int x=cell(position.x),y=cell(position.y);
        auto [location,fresh]=locations_.try_emplace(unit.id,Location{area,x,y,generation});
        auto &previous=location->second;
        const bool moved=!fresh && (previous.area!=area || previous.x!=x || previous.y!=y);
        if(moved) erase(unit.id,previous);
        auto &region=regions_[area];region.maximumSize=std::max(region.maximumSize,size);
        if(fresh || moved) {
            auto &entry=region.cells[x][y].insert_or_assign(unit.id,Entry{unit,position}).first->second;
            previous={area,x,y,generation,&entry};
        } else {
            previous.generation=generation;
            auto &entry=*previous.entry;
            if(entry.unit.kind!=unit.kind || entry.unit.revision!=unit.revision ||
                entry.position.x!=position.x || entry.position.y!=position.y)
                entry={unit,position};
        }
    };
    for(const auto &[id,player]:ports_.players.all())
        if(player.entered && player.persistent.player.hp>0) insert(player.area,player.position,{player.actor,UnitKind::Player,0},2);
    for(const auto &[id,monster]:ports_.monsters.read().actors)
        if(monster.alive() && ports_.areas.loaded(monster.area,monster.position)) insert(monster.area,monster.position,{id,UnitKind::Monster,monster.revision},monster.rule.size);
    std::erase_if(locations_,[&](const auto &entry) {
        if(entry.second.generation==generation) return false;
        erase(entry.first,entry.second);return true;
    });
    state_.indexedTick=tick.tick;state_.generation=generation;state_.indexedUnits=locations_.size();
    return StepStatus::Complete;
}
}
