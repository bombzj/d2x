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

namespace d2x::server::spatial {
std::function<uint16_t(int,int32_t,int32_t)> System::nativeFootprints(uint16_t requestedMask,
    std::optional<std::span<const EntityId>> units,EntityId self,EntityId target) const {
    using Cell=std::tuple<int,int32_t,int32_t>;
    std::vector<std::pair<Cell,uint16_t>> masks;
    masks.reserve(5*(units?units->size():ports_.players.all().size()+ports_.monsters.read().actors.size()));
    const auto stamp=[&](RegionId region,Vec position,int32_t x,int32_t y,uint16_t mask) {
        mask&=requestedMask;if(!mask) return;
        if(const auto room=ports_.areas.roomAtPosition(region,position+Vec{.5f,.5f},x,y))
            masks.emplace_back(Cell{ports_.areas.at(room->area).definition.act,x,y},mask);
    };
    const auto footprint=[&](RegionId region,Vec position,NativeCollisionPattern pattern,uint16_t mask) {
        const auto *area=ports_.areas.find(region);if(!area) return;
        const auto x=int32_t(std::floor(position.x+area->definition.origin.x+.5f)),y=int32_t(std::floor(position.y+area->definition.origin.y+.5f));
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
            if(const auto value=nativeCollisionFootprint(pattern,dx,dy,mask)) stamp(region,position,x+dx,y+dy,value);
    };
    const auto rectangle=[&](RegionId region,Vec position,int width,int height,uint16_t mask) {
        mask&=requestedMask;
        const auto *area=ports_.areas.find(region);if(!area || !mask || width<=0 || height<=0) return;
        const int x=int(std::floor(position.x+area->definition.origin.x+.5f))-(width>>1),y=int(std::floor(position.y+area->definition.origin.y+.5f))-(height>>1);
        for(int dy=0;dy<height;++dy) for(int dx=0;dx<width;++dx) stamp(region,position,x+dx,y+dy,mask);
    };
    const auto playerFootprint=[&](const PlayerState &player) {
        // SmallUnit adds presence (0x1000) independently of its 0x80 mask.
        // Corpses are separate 0x8000 rectangles, including retained bodies.
        if(!(requestedMask&0x9080) || !player.collisionActive) return;
        if(player.actor!=self && !(player.actor==target && player.persistent.player.hp>0)) {
            if(player.persistent.player.hp>0) footprint(player.area,player.position,NativeCollisionPattern::SmallUnit,0x80);
            else rectangle(player.area,player.position,3,3,0x8000);
        }
        if(requestedMask&0x8000) for(const auto &corpse:player.persistent.corpses)
            rectangle(corpse.region,corpse.position,3,3,0x8000);
    };
    const auto monsterFootprint=[&](const monsters::Actor &unit) {
        // Unit/Pet presence, the 0x100 body/reservation, and corpse bits are
        // the only possible contributions. Do not cull by body mask alone.
        if(!(requestedMask&0xb100) || unit.id==self || (unit.id==target && unit.alive()) || !ports_.areas.loaded(unit.area,unit.position+Vec{.5f,.5f})) return;
        if(unit.spawnReservation) footprint(unit.area,*unit.spawnReservation,NativeCollisionPattern::SmallUnit,0x100);
        if(unit.hatching) {rectangle(unit.area,unit.position,3,3,0x8000);return;}
        if(!unit.alive() && !unit.rule.deadCollision) rectangle(unit.area,unit.position,3,3,0x8000);
        else if(!unit.burrowed && !unit.airborne && !unit.submerged) footprint(unit.area,unit.position,unit.collisionPattern.value_or(unit.rule.collisionPattern),0x100);
    };
    if(units) {
        // Contact callers supply the real adjacent-room unit chains. Avoid a
        // whole-world projection for every missile in a crowded frame.
        for(const auto id:*units) {
            if(const auto *player=ports_.players.findActor(id)) playerFootprint(*player);
            else if(const auto *monster=ports_.monsters.find(id)) monsterFootprint(*monster);
        }
    } else {
        for(const auto &[id,player]:ports_.players.all()) {(void)id;playerFootprint(player);}
        for(const auto &[id,monster]:ports_.monsters.read().actors) {(void)id;monsterFootprint(monster);}
    }
    if(requestedMask&0x200) for(const auto &[id,item]:ports_.items.read().world.items) {
        if(id==target) continue;
        if(const auto *ground=std::get_if<GroundLocation>(&item.location)) rectangle(ground->region,ground->position,1,1,0x200);
    }
    const auto portal=[&](const travel::Portal &value) {
        const auto mode=value.opened?2:1;
        if(value.rule.collision[size_t(mode)]) {
            if(value.fieldId!=target) rectangle(value.field,value.fieldPosition,value.rule.width,value.rule.height,value.rule.collisionMask);
            if(value.townId!=target) rectangle(value.town,value.townPosition,value.rule.width,value.rule.height,value.rule.collisionMask);
        }
    };
    for(const auto &[id,value]:ports_.travel.read().portals) {(void)id;portal(value);}
    for(const auto &[id,value]:ports_.travel.read().specialPortals) {(void)id;portal(value);}
    // Sorting only completed footprint cells cannot change unit traversal,
    // target exclusion, native masks or random consumption. Coalesce overlaps
    // with the same OR as the former tree, without one allocation per cell.
    std::sort(masks.begin(),masks.end(),[](const auto &a,const auto &b){return a.first<b.first;});
    size_t count{};
    for(const auto &entry:masks) {
        if(count && masks[count-1].first==entry.first) masks[count-1].second|=entry.second;
        else masks[count++]=entry;
    }
    masks.resize(count);
    return [masks=std::move(masks)](int act,int32_t x,int32_t y)->uint16_t {
        const Cell key{act,x,y};
        const auto found=std::lower_bound(masks.begin(),masks.end(),key,
            [](const auto &entry,const Cell &value){return entry.first<value;});
        return found!=masks.end() && found->first==key?found->second:0;
    };
}
std::function<uint16_t(RoomView,int32_t,int32_t)> System::nativeCollisionSample(uint16_t mask,
    std::optional<std::span<const EntityId>> units,EntityId self,EntityId target) const {
    return [sample=nativeFootprints(mask,units,self,target),areas=&ports_.areas,target](RoomView room,int32_t x,int32_t y)->uint16_t {
        const auto &area=areas->at(room.area).definition;
        return area.collision.movementMask(x-int32_t(area.origin.x),y-int32_t(area.origin.y),target) | sample(area.act,x,y);
    };
}
Grid System::navigationGrid(RegionId region,EntityId self,EntityId target) const {
    const auto &area=ports_.areas.at(region).definition;
    const auto *terrain=&area.collision;
    Grid view;
    view.width=terrain->width;view.height=terrain->height;
    view.neighbours=terrain->neighbours;
    view.obstacleRevision=terrain->obstacleRevision;
    view.missileCollisionRevision=terrain->missileCollisionRevision;
    Vec navigationOrigin;
    auto navigation=terrain->linkedNavigation(navigationOrigin);
    view.collisionSample=[sample=nativeFootprints(0xffff,{},self,target),navigation=std::move(navigation),navigationOrigin,origin=area.origin,act=area.act,target]
        (int x,int y,EntityId ignored)->uint16_t {
        return navigation.movementMask(x-int(navigationOrigin.x),y-int(navigationOrigin.y),ignored?ignored:target) |
            sample(act,x+int(origin.x),y+int(origin.y));
    };
    return view;
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
