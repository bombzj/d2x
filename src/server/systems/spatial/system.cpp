#include "system.hpp"
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
    for(auto bucket=cells.lower_bound({lowX,INT32_MIN});bucket!=cells.end() && bucket->first.first<=highX;++bucket) {
        if(bucket->first.second<lowY || bucket->first.second>highY) continue;
        for(const auto &entry:bucket->second) {
            ++state_.candidates;
            const auto delta=entry.position-request.center;
            if(delta.x*delta.x+delta.y*delta.y<=request.radius*request.radius) result.push_back(entry.unit);
        }
    }
    std::sort(result.begin(),result.end(),[](const auto &first,const auto &second){return first.id<second.id;});
    state_.returned+=result.size();
    return {DomainStatus::Applied,std::move(result)};
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    regions_.clear();state_.indexedUnits=0;
    if(state_.indexedTick!=tick.tick) state_.queries=state_.candidates=state_.returned=0;
    const auto insert=[&](RegionId area,Vec position,UnitRef unit,int size) {
        auto &region=regions_[area];region.maximumSize=std::max(region.maximumSize,size);
        region.cells[{cell(position.x),cell(position.y)}].push_back({unit,position});++state_.indexedUnits;
    };
    for(const auto &[id,player]:ports_.players.all())
        if(player.entered && player.persistent.player.hp>0) insert(player.area,player.position,{player.actor,UnitKind::Player,0},2);
    for(const auto &[id,monster]:ports_.monsters.read().actors)
        if(monster.life>0) insert(monster.area,monster.position,{id,UnitKind::Monster,monster.revision},monster.rule.size);
    state_.indexedTick=tick.tick;++state_.generation;
    return StepStatus::Complete;
}
}
