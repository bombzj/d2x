#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::spatial {
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
        if(!fresh && (location->second.area!=area || location->second.x!=x || location->second.y!=y)) erase(unit.id,location->second);
        location->second={area,x,y,generation};
        auto &region=regions_[area];region.maximumSize=std::max(region.maximumSize,size);
        region.cells[x][y].insert_or_assign(unit.id,Entry{unit,position});
    };
    for(const auto &[id,player]:ports_.players.all())
        if(player.entered && player.persistent.player.hp>0) insert(player.area,player.position,{player.actor,UnitKind::Player,0},2);
    for(const auto &[id,monster]:ports_.monsters.read().actors)
        if(monster.life>0 && ports_.areas.loaded(monster.area,monster.position)) insert(monster.area,monster.position,{id,UnitKind::Monster,monster.revision},monster.rule.size);
    std::erase_if(locations_,[&](const auto &entry) {
        if(entry.second.generation==generation) return false;
        erase(entry.first,entry.second);return true;
    });
    state_.indexedTick=tick.tick;state_.generation=generation;state_.indexedUnits=locations_.size();
    return StepStatus::Complete;
}
}
