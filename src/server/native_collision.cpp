#include "area_store.hpp"
#include <cmath>
#include <exception>
#include <algorithm>
#include <chrono>

namespace d2x::server {
namespace {
struct CollisionObservation {
    using Clock=std::chrono::steady_clock;
    spatial::FootprintMetrics *metrics;
    Clock::time_point started;
    explicit CollisionObservation(spatial::FootprintMetrics *value):metrics(value),started(value?Clock::now():Clock::time_point{}) {}
    ~CollisionObservation() {
        if(metrics) metrics->nativeUpdateNs+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-started).count());
    }
};
}
uint16_t AreaStore::sampleNativeCollision(RoomView room,int32_t x,int32_t y,uint16_t mask) const {
    observeCollisionRead();const auto &area=at(room.area).definition;
    return area.collision.nativeMovementMask(x-int32_t(area.origin.x),y-int32_t(area.origin.y))&mask;
}
NativeFootprint AreaStore::patternFootprint(RegionId area,Vec position,NativeCollisionPattern pattern,uint16_t mask) const noexcept {
    const auto found=find(area);if(!found) return {};
    const auto point=position+found->definition.origin;
    return {area,int32_t(std::floor(point.x+.5f)),int32_t(std::floor(point.y+.5f)),pattern,mask};
}
NativeFootprint AreaStore::rectangleFootprint(RegionId area,Vec position,int width,int height,uint16_t mask) const noexcept {
    auto value=patternFootprint(area,position,NativeCollisionPattern::None,mask);
    value.width=width;value.height=height;return value;
}
void AreaStore::writeNativeCell(RegionId region,size_t cell,uint16_t value) noexcept {
    auto &grid=areas_.at(region).definition.collision;
    if(cell>=grid.nativeCollision.size()) std::terminate();
    auto &previous=grid.nativeCollision[cell];if(previous==value) return;
    if(collisionMove_) {
        auto &move=*collisionMove_;
        move.undo_.push_back({region,cell,previous});
    }
    previous=value;++grid.nativeCollisionRevision;
    if(profileFootprints_) ++footprintMetrics_.nativeWrites;
}
void AreaStore::writeFootprint(const NativeFootprint &footprint,bool occupied,std::optional<RoomView> onlyRoom) noexcept {
    if(!footprint.mask) return;
    const CollisionObservation observation{profileFootprints_?&footprintMetrics_:nullptr};
    const auto *source=find(footprint.area);if(!source) return;
    const bool rectangle=footprint.width>0 && footprint.height>0;
    const int left=rectangle?-(footprint.width/2):-1,top=rectangle?-(footprint.height/2):-1;
    const int right=rectangle?left+footprint.width:2,bottom=rectangle?top+footprint.height:2;
    const Vec observer{float(footprint.x)-source->definition.origin.x+.5f,float(footprint.y)-source->definition.origin.y+.5f};
    if(!loaded(footprint.area,observer)) return;
    for(int dy=top;dy<bottom;++dy) for(int dx=left;dx<right;++dx) {
        const auto mask=rectangle?footprint.mask:nativeCollisionFootprint(footprint.pattern,dx,dy,footprint.mask);
        if(!mask) continue;
        const auto room=roomAtPosition(footprint.area,observer,footprint.x+dx,footprint.y+dy);
        if(!room || (onlyRoom && *room!=*onlyRoom)) continue;
        const auto &area=at(room->area).definition;const auto &grid=area.collision;
        const int x=footprint.x+dx-int(area.origin.x),y=footprint.y+dy-int(area.origin.y);
        if(x<0 || y<0 || x>=grid.width || y>=grid.height) continue;
        const auto cell=size_t(y)*grid.width+x;
        const auto previous=grid.nativeCollision.at(cell);
        writeNativeCell(room->area,cell,occupied?uint16_t(previous|mask):uint16_t(previous&~mask));
    }
}
void AreaStore::setUnitCollision(EntityId id,NativeFootprint body,NativeFootprint reservation,bool alive) noexcept {
    const auto found=roomUnits_.find(id);if(found==roomUnits_.end()) std::terminate();
    auto &entry=found->second;entry.collisionAlive=alive;
    if(entry.body!=body) {
        // PATH_SetUnitDeadCollision adds CORPSE without replacing the player's
        // path footprint mask. Resurrect removes the old live mask, not the
        // independent corpse left by PlrModes::D2GAME_CORPSE_Handler.
        const bool playerRevival=entry.kind==0 && entry.body.mask==0x8000 && body.mask==0x80;
        if(playerRevival) {
            auto live=entry.body;live.pattern=NativeCollisionPattern::SmallUnit;live.mask=0x80;live.width=live.height=0;
            writeFootprint(live,false);
        } else writeFootprint(entry.body,false);
        entry.body=body;writeFootprint(entry.body,true);
    }
    if(entry.reservation!=reservation) {writeFootprint(entry.reservation,false);entry.reservation=reservation;writeFootprint(entry.reservation,true);}
}
void AreaStore::restoreUnitCollision(RoomView room) noexcept {
    // Room creation is rare; queries never enumerate this registry. Restrict
    // writes to the newly created room so retained neighbours keep their bits.
    for(const auto &[id,entry]:roomUnits_) {
        (void)id;writeFootprint(entry.body,true,room);writeFootprint(entry.reservation,true,room);
    }
}
void AreaStore::restoreNativeRoomBase(RoomView room) noexcept {
    auto &area=areas_.at(room.area);auto &grid=area.definition.collision;
    for(const auto &bounds:area.definition.activation.rooms()) {
        if((int(area.definition.origin.x)+bounds.x)/5!=room.x || (int(area.definition.origin.y)+bounds.y)/5!=room.y) continue;
        for(int y=std::max(0,bounds.y);y<std::min(grid.height,bounds.y+bounds.height);++y)
            for(int x=std::max(0,bounds.x);x<std::min(grid.width,bounds.x+bounds.width);++x) {
            const auto cell=size_t(y)*grid.width+x;
            grid.nativeCollision[cell]=grid.fullTerrainCollision[cell]|grid.objectCollision[cell]|grid.missileCollision[cell];
        }
        ++grid.nativeCollisionRevision;return;
    }
}
void AreaStore::rememberCollisionRoom(std::function<void()> restore) {
    if(collisionMove_) collisionMove_->roomRestorations_.push_back(std::move(restore));
}
AreaStore::CollisionExclusion::CollisionExclusion(AreaStore &areas,EntityId self,EntityId target)
    :areas_(areas),units_{self,target==self?EntityId{}:target} {
    if(areas_.profileFootprints_) ++areas_.footprintMetrics_.nativeQueryScopes;
    for(size_t index=0;index<units_.size();++index) {
        const auto found=areas_.roomUnits_.find(units_[index]);
        if(found==areas_.roomUnits_.end() || (index && !found->second.collisionAlive)) {units_[index]={};continue;}
        footprints_[index]={found->second.body,found->second.reservation};
        // PATH_RemoveCollisionFootprintForUnit withdraws the body. A separate
        // destination reservation stays until its own skill lifecycle clears it.
        areas_.writeFootprint(footprints_[index][0],false);
    }
}
AreaStore::CollisionExclusion::~CollisionExclusion() {
    // D2Common_10142 restores the target before restoring the mover.
    for(size_t index=units_.size();index-->0;) if(units_[index]) {
        const auto found=areas_.roomUnits_.find(units_[index]);
        if(found==areas_.roomUnits_.end() || found->second.body!=footprints_[index][0] ||
            found->second.reservation!=footprints_[index][1]) std::terminate();
        areas_.writeFootprint(footprints_[index][0],true);
    }
}
AreaStore::CollisionMove::CollisionMove(AreaStore &areas,EntityId unit,RegionId source)
    :areas_(areas),unit_(unit),source_(source),before_(areas.roomUnits_.at(unit).body) {
    if(areas_.collisionMove_) std::terminate();
    areas_.collisionMove_=this;
}
AreaStore::CollisionMove::~CollisionMove() {if(!finished_) rollback();}
void AreaStore::CollisionMove::commit() noexcept {areas_.collisionMove_=nullptr;finished_=true;}
void AreaStore::CollisionMove::rollback() noexcept {
    if(areas_.profileFootprints_) ++areas_.footprintMetrics_.nativeRollbacks;
    areas_.collisionMove_=nullptr;
    while(!undo_.empty()) {
        const auto undo=undo_.back();undo_.pop_back();
        auto &grid=areas_.areas_.at(undo.area).definition.collision;
        grid.nativeCollision.at(undo.cell)=undo.value;++grid.nativeCollisionRevision;
    }
    areas_.roomUnits_.at(unit_).body=before_;finished_=true;
    for(const auto &restore:roomRestorations_) restore();
}
bool AreaStore::CollisionMove::step(int32_t fromX,int32_t fromY,int32_t toX,int32_t toY,MovementCollisionRule rule) {
    if(areas_.profileFootprints_) ++areas_.footprintMetrics_.nativeMoveProbes;
    if(undo_.empty()) undo_.reserve(10*2*9);
    auto &body=areas_.roomUnits_.at(unit_).body;
    const auto previous=body;
    areas_.writeFootprint(previous,false);
    const auto &source=areas_.at(source_).definition;
    const Vec observer{float(fromX)-source.origin.x+.5f,float(fromY)-source.origin.y+.5f};
    bool clear=true;
    for(int dy=-1;dy<=1 && clear;++dy) for(int dx=-1;dx<=1;++dx) {
        if(!nativeCollisionSizeCell(rule.size,dx,dy)) continue;
        const auto room=areas_.roomAtPosition(source_,observer,toX+dx,toY+dy);
        uint16_t value=0xffff;
        if(room) {
            areas_.observeCollisionRead();
            const auto &area=areas_.at(room->area).definition;
            value=area.collision.nativeMovementMask(toX+dx-int(area.origin.x),toY+dy-int(area.origin.y));
        }
        if(value&rule.mask) {clear=false;break;}
    }
    if(!clear) {areas_.writeFootprint(previous,true);return false;}
    const auto destination=areas_.roomAtPosition(source_,observer,toX,toY);
    if(body.mask) {
        if(destination) body.area=destination->area;
        body.x=toX;body.y=toY;areas_.writeFootprint(body,true);
    }
    if(destination) source_=destination->area;
    return true;
}
}
