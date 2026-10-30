#include "navigation_search.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <nlohmann/json.hpp>

namespace d2x {
void DebugNavigationSearch::clear() {results_.clear();reachable_.clear();points_=0;++resets_;}
void DebugNavigationSearch::beginFrame(uint64_t game,uint64_t area,uint64_t geometry) {
    const std::array next{game,area,geometry};
    if(context_!=next) {clear();context_=next;}
    ++frames_;frameQueries_=frameHits_=0;frameMicros_=0;started_={};yielded_=false;
    // Active replay prefixes are touched every continuation. Drop obsolete
    // origins/collision views without evicting the prefix being resumed.
    for(auto at=results_.begin();at!=results_.end();) {
        if(frames_-at->second.used>120) {points_-=at->second.path.size();at=results_.erase(at);}
        else ++at;
    }
    std::erase_if(reachable_,[&](const auto &entry){return !results_.contains(entry.second);});
}
std::deque<Vec> DebugNavigationSearch::path(const Grid &grid,Vec from,Vec to,bool partial,
        MovementCollisionRule rule,bool native,std::source_location caller) {
    // Keys never retain Grid callbacks or borrowed map pointers. The frontend
    // invalidates the scene domain, and collision-view revisions include the
    // actual derived unit footprints (including the ignored interaction unit).
    if(!std::isfinite(from.x) || !std::isfinite(from.y) || !std::isfinite(to.x) || !std::isfinite(to.y)) return {};
    const Key key{grid.obstacleRevision,grid.missileCollisionRevision,bool(grid.collisionSample),grid.width,grid.height,
        rule.mask,rule.size,partial,native,from.x,from.y,to.x,to.y};
    if(const auto found=results_.find(key);found!=results_.end()) {
        ++hits_;++frameHits_;found->second.used=frames_;return found->second.path;
    }
    const auto now=Clock::now();
    if(started_==Clock::time_point{}) started_=now;
    // Grid::path remains atomic. This is a shared query quota and a soft
    // elapsed-time boundary BETWEEN calls, never a hard bound on one AStar.
    if(frameQueries_>=queryBudget || (frameQueries_ &&
        std::chrono::duration<double,std::micro>(now-started_).count()>=microsecondBudget)) {
        if(!yielded_) {yielded_=true;++yields_;}
        throw DebugSearchPending{};
    }
    // Retain a replayable prefix instead of evicting it and repeatedly spending
    // the next frame's quota on the same early candidates. Refuse explicitly
    // at the diagnostic storage bound; never label deferred work unreachable.
    if(results_.size()>=4096) throw std::runtime_error("Debug navigation continuation query capacity exhausted");
    ++queries_;++frameQueries_;
    const std::string_view file=caller.file_name();const auto separator=file.find_last_of("/\\");
    lastFile_=file.substr(separator==file.npos?0:separator+1);lastLine_=caller.line();
    const auto searchStarted=Clock::now();
    auto result=grid.path(from,to,partial,rule,native);
    const double elapsed=std::chrono::duration<double,std::micro>(Clock::now()-searchStarted).count();
    frameMicros_+=elapsed;totalMicros_+=elapsed;
    if(elapsed>=maxMicros_) {
        maxMicros_=elapsed;slowestFile_=lastFile_;slowestLine_=lastLine_;slowestFrom_=from;slowestTo_=to;
    }
    if(result.size()>262144-points_) throw std::runtime_error("Debug navigation continuation path capacity exhausted");
    points_+=result.size();results_.emplace(key,Result{result,frames_});
    for(const auto node:result) {
        if(reachable_.size()>=16384) break;
        auto proof=key;std::get<7>(proof)=false;std::get<11>(proof)=node.x;std::get<12>(proof)=node.y;
        reachable_.try_emplace(std::move(proof),key);
    }
    return result;
}
bool DebugNavigationSearch::reachable(const Grid &grid,Vec from,Vec to,MovementCollisionRule rule,bool native,std::source_location caller) {
    if(!std::isfinite(from.x) || !std::isfinite(from.y) || !std::isfinite(to.x) || !std::isfinite(to.y)) return false;
    const Key key{grid.obstacleRevision,grid.missileCollisionRevision,bool(grid.collisionSample),grid.width,grid.height,
        rule.mask,rule.size,false,native,from.x,from.y,to.x,to.y};
    if(const auto proof=reachable_.find(key);proof!=reachable_.end()) {
        const auto source=results_.find(proof->second);
        if(source!=results_.end()) {source->second.used=frames_;++proofHits_;return true;}
    }
    return !path(grid,from,to,false,rule,native,caller).empty();
}
nlohmann::json DebugNavigationSearch::snapshot() const {
    return {{"frames",frames_},{"queries",queries_},{"cacheHits",hits_},{"reachabilityHits",proofHits_},{"budgetYields",yields_},{"cacheResets",resets_},
        {"queryBudget",queryBudget},{"softBudgetUs",microsecondBudget},{"frameQueries",frameQueries_},{"frameCacheHits",frameHits_},
        {"framePathUs",frameMicros_},{"totalPathUs",totalMicros_},{"maxPathUs",maxMicros_},
        {"lastQuery",{{"file",lastFile_},{"line",lastLine_}}},
        {"slowestQuery",{{"file",slowestFile_},{"line",slowestLine_},{"microseconds",maxMicros_},
            {"from",{slowestFrom_.x,slowestFrom_.y}},{"to",{slowestTo_.x,slowestTo_.y}}}},
        {"cachedQueries",results_.size()},{"cachedPoints",points_},{"reachablePoints",reachable_.size()},{"segmentReuses",segmentReuses_},
        {"yielded",yielded_},{"atomicGridCall",true}};
}
} // namespace d2x
