#include "navigation_search.hpp"
#include <algorithm>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <nlohmann/json.hpp>

namespace d2x {
bool debugUnknownFrontier(const Grid &grid,Vec endpoint) {
    // A known wall is not an observation target. A reachable edge adjacent
    // to unavailable collision can reveal another ordinary client room.
    const int x=int(endpoint.x),y=int(endpoint.y);
    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
        if((!dx && !dy) || (dx && dy)) continue;
        for(int step=1;step<=2;++step) {
            const auto mask=grid.movementMask(x+dx*step,y+dy*step);
            if(mask==0xffff) return true;
            if(mask&playerMovement.mask) break;
        }
    }
    return false;
}
// Only value snapshots enter the worker. RemoteTown's collisionSample borrows
// live terrain and cannot be copied into an asynchronous query.
class DebugNavigationSearch::Worker {
    using Task=std::function<void()>;
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<std::weak_ptr<Task>> queue_;
    std::jthread thread_{[this](std::stop_token stop) {
        std::unique_lock lock(mutex_);
        while(!stop.stop_requested()) {
            wake_.wait(lock,stop,[&]{return !queue_.empty();});
            if(stop.stop_requested()) break;
            auto task=queue_.front().lock();queue_.pop_front();
            lock.unlock();
            if(task) (*task)();
            lock.lock();
        }
    }};
  public:
    struct Path {std::deque<Vec> points;double micros;};
    template<typename T> struct Job {
        std::future<T> future;
        std::shared_ptr<Task> ticket;
        uint64_t used;
        bool ready() const {return future.wait_for(std::chrono::seconds(0))==std::future_status::ready;}
    };
    std::map<Key,Job<Path>> paths;
    std::optional<Key> componentKey;
    std::optional<Job<Component>> componentJob;
    Component component;
    using Domain=std::tuple<uint64_t,uint64_t,int,int>;
    struct Frozen {std::optional<Domain> domain;std::shared_ptr<const Grid> grid;};
    std::array<Frozen,2> snapshots;
    std::shared_ptr<const Grid> freeze(const Grid &source) {
        auto &cached=snapshots[bool(source.collisionSample)];
        const Domain domain{source.obstacleRevision,source.missileCollisionRevision,source.width,source.height};
        if(cached.domain==domain) return cached.grid;
        // Include the one-cell halo sampled by native footprint checks.
        const int stride=source.width+2,rows=source.height+2;
        auto masks=source.movementMaskSnapshot();
        auto grid=std::make_shared<Grid>();grid->width=source.width;grid->height=source.height;
        grid->collisionSample=[stride,rows,masks=std::move(masks)](int x,int y,EntityId)->uint16_t {
            if(x< -1 || y< -1 || x>=stride-1 || y>=rows-1) return 0xffff;
            return masks[size_t(y+1)*stride+(x+1)];
        };
        cached={domain,grid};return grid;
    }
    template<typename F> auto schedule(F work,uint64_t frame) {
        using T=std::invoke_result_t<F>;
        auto task=std::make_shared<std::packaged_task<T()>>(std::move(work));
        Job<T> job{task->get_future(),std::make_shared<Task>([task]{(*task)();}),frame};
        {std::lock_guard lock(mutex_);queue_.push_back(job.ticket);}
        wake_.notify_one();return job;
    }
    void reset() {
        paths.clear();componentKey.reset();componentJob.reset();component={};snapshots={};
        std::lock_guard lock(mutex_);queue_.clear();
    }
};
DebugNavigationSearch::DebugNavigationSearch()=default;
DebugNavigationSearch::~DebugNavigationSearch()=default;
void DebugNavigationSearch::defer(bool travel) {
    if(travel) noteTravelWait();
    if(!yielded_) {yielded_=true;++yields_;}
    throw DebugSearchPending{};
}
void DebugNavigationSearch::noteTravelWait() {
    const auto now=Clock::now();
    if(lastDeferred_==Clock::time_point{} || (frames_-lastDeferredFrame_>1 && now-lastDeferred_>std::chrono::milliseconds(180)))
        waitingSince_=now;
    lastDeferred_=now;lastDeferredFrame_=frames_;
}
bool DebugNavigationSearch::prolongedWait() const {
    const auto now=Clock::now();
    return lastDeferred_!=Clock::time_point{} && (frames_-lastDeferredFrame_<=1 || now-lastDeferred_<std::chrono::milliseconds(180)) &&
        now-waitingSince_>=std::chrono::milliseconds(250);
}
void DebugNavigationSearch::clear() {
    if(worker_) worker_->reset();
    lastDeferred_={};waitingSince_={};travelAnchors_.clear();results_.clear();
    reachable_.clear();wallDistances_.clear();points_=0;++resets_;
}
int DebugNavigationSearch::wallClearance(const Grid &terrain,Vec at) {
    const int x=int(std::floor(at.x)),y=int(std::floor(at.y));
    if(x<0 || y<0 || x>=terrain.width || y>=terrain.height) return 0;
    const std::pair block{x/wallBlock,y/wallBlock};
    auto found=wallDistances_.find(block);
    if(found==wallDistances_.end()) {
        // A halo makes the capped eight-neighbour distance exact across block seams.
        // Unknown cells and unassigned rooms provide no evidence of a wall.
        constexpr int pad=wallSearchRadius+1,side=wallBlock+2*pad;
        std::array<int,side*side> field;
        field.fill(wallSearchRadius);
        for(int j=1;j<side-1;++j) for(int i=1;i<side-1;++i) {
            const int column=block.first*wallBlock+i-pad,row=block.second*wallBlock+j-pad;
            if(column<0 || row<0 || column>=terrain.width || row>=terrain.height || terrain.terrainCollision.empty()) continue;
            const size_t cell=size_t(row)*terrain.width+column;
            const int flags=terrain.fullTerrainCollision.empty()?terrain.terrainCollision[cell]:terrain.fullTerrainCollision[cell];
            const auto object=terrain.objectCollision.empty()?0:terrain.objectCollision[cell];
            if(flags!=0xffff && (((flags|object)&playerMovement.mask) || terrain.blocked[cell])) field[j*side+i]=0;
        }
        for(int j=1;j<side-1;++j) for(int i=1;i<side-1;++i) {
            const int k=j*side+i;
            field[k]=std::min(field[k],1+std::min({field[k-1],field[k-side-1],field[k-side],field[k-side+1]}));
        }
        for(int j=side-2;j>0;--j) for(int i=side-2;i>0;--i) {
            const int k=j*side+i;
            field[k]=std::min(field[k],1+std::min({field[k+1],field[k+side-1],field[k+side],field[k+side+1]}));
        }
        if(wallDistances_.size()>=512) wallDistances_.clear();
        found=wallDistances_.try_emplace(block).first;
        auto &values=found->second;
        for(int j=0;j<wallBlock;++j) for(int i=0;i<wallBlock;++i)
            values[j*wallBlock+i]=uint8_t(field[(j+pad)*side+i+pad]);
    }
    return found->second[(y%wallBlock)*wallBlock+x%wallBlock];
}
Vec DebugNavigationSearch::explorationLane(const Grid &terrain,const Grid &collision,Vec from,Vec onRoute,Vec onward,bool walking) {
    const auto direction=onRoute-from;
    const float length=direction.length();
    if(length<4 || (onward-onRoute).length()<1) return onRoute;
    if(wallClearance(terrain,onRoute)==wallSearchRadius &&
        (!walking || wallClearance(terrain,(from+onRoute)*.5f)==wallSearchRadius)) return onRoute;
    const Vec normal{-direction.y/length,direction.x/length};
    Vec best=onRoute;
    const auto cost=[&](Vec at) {
        const int end=wallClearance(terrain,at);
        const int middle=walking?wallClearance(terrain,(from+at)*.5f):end;
        return wallCost(end)+.5f*wallCost(middle)+(at-onRoute).length()*.4f;
    };
    float bestCost=cost(best);
    if(bestCost==0) return best;
    for(int shift=1;shift<=explorationClearance;++shift) for(int sign:{-1,1}) {
        const auto sample=onRoute+normal*float(sign*shift);
        const Vec at{std::floor(sample.x)+.5f,std::floor(sample.y)+.5f};
        const float score=cost(at);
        if(score>=bestCost || !collision.walkable(at,playerMovement) ||
            !collision.segment(at,onward,{},playerMovement) || !collision.nativeMovementSegment(at,onward,playerMovement)) continue;
        if(walking && (!collision.segment(from,at,{},playerMovement) || !collision.nativeMovementSegment(from,at,playerMovement))) continue;
        best=at;bestCost=score;
    }
    return best;
}
Vec DebugNavigationSearch::townWalkLane(const Grid &terrain,const Grid &collision,Vec from,Vec onRoute,Vec onward) {
    if((onRoute-from).length()<2 || (onward-onRoute).length()<1) return onRoute;
    // Score both sides of a bend. Sampling just the pointer misses the
    // inside rail of a bridge when the pointer itself is on open floor.
    const auto exposure=[&](Vec a,Vec b) {
        const Vec leg=b-a;
        const int steps=std::max(1,int(std::ceil(leg.length())));
        float sum=0;int worst=0;
        for(int i=1;i<=steps;++i) {
            const int deficit=std::max(0,townClearance-wallClearance(terrain,a+leg*(float(i)/steps)));
            worst=std::max(worst,deficit);sum+=deficit;
        }
        return float(worst)+sum/steps;
    };
    const auto cost=[&](Vec at) {
        const Vec next=onward-at;
        return exposure(from,at)+exposure(at,at+next.unit()*std::min(6.f,next.length()))+.25f*(at-onRoute).length();
    };
    Vec best=onRoute;float bestCost=cost(best);
    if(bestCost==0) return best;
    // Also try diagonal offsets: an outside elbow can require room in both
    // axes. Keep the ordinary validated bend when a narrow passage needs it.
    for(int r=1;r<=townClearance;++r) for(int dx:{-1,0,1}) for(int dy:{-1,0,1}) {
        if(!dx && !dy) continue;
        const auto at=onRoute+Vec{float(r*dx),float(r*dy)};
        const Vec incoming=onRoute-from,outgoing=onward-onRoute;
        const Vec first=at-from,second=onward-at;
        if(first.x*incoming.x+first.y*incoming.y<=0 || second.x*outgoing.x+second.y*outgoing.y<=0) continue;
        const float score=cost(at);
        if(score>=bestCost || !collision.segment(from,at,{},playerMovement) || !collision.nativeMovementSegment(from,at,playerMovement) ||
            !collision.segment(at,onward,{},playerMovement) || !collision.nativeMovementSegment(at,onward,playerMovement)) continue;
        best=at;bestCost=score;
    }
    return best;
}
void DebugNavigationSearch::beginFrame(uint64_t game,uint64_t area,uint64_t geometry) {
    const std::array next{game,area,geometry};
    if(context_!=next) {clear();context_=next;}
    ++frames_;frameQueries_=frameHits_=0;frameMicros_=0;yielded_=false;
    const auto now=Clock::now();
    std::erase_if(travelAnchors_,[&](const auto &entry){return now-entry.second.touched>std::chrono::seconds(60);});
    const auto retained=[&](const Key &key) {
        const auto anchor=travelAnchors_.find({std::get<11>(key),std::get<12>(key)});
        return !std::get<2>(key) && anchor!=travelAnchors_.end() &&
            anchor->second.from.x==std::get<9>(key) && anchor->second.from.y==std::get<10>(key);
    };
    if(worker_) std::erase_if(worker_->paths,[&](const auto &entry){
        return frames_-entry.second.used>8 && !retained(entry.first);
    });
    // Active replay prefixes are touched every continuation. Drop obsolete
    // origins/collision views without evicting the prefix being resumed.
    for(auto at=results_.begin();at!=results_.end();) {
        if(frames_-at->second.used>120 && !retained(at->first)) {points_-=at->second.path.size();at=results_.erase(at);}
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
    // The direct ray is also Grid::path's first choice. It needs no search
    // and stays usable while the player or an occupied footprint is moving.
    if(!grid.walkable(from,rule) || to.x<0 || to.y<0 || to.x>=grid.width || to.y>=grid.height ||
        (!partial && !grid.walkable(to,rule))) return {};
    if((to-from).length()<=48 && grid.segment(from,to,{},rule) &&
        (!native || grid.nativeMovementSegment(from,to,rule))) return {to};
    if(!worker_) worker_=std::make_unique<Worker>();
    auto pending=worker_->paths.find(key);
    if(pending==worker_->paths.end()) {
        if(results_.size()>=4096) throw std::runtime_error("Debug navigation continuation query capacity exhausted");
        auto snapshot=worker_->freeze(grid);
        auto job=worker_->schedule([snapshot,from,to,partial,rule,native] {
            const auto started=Clock::now();
            auto points=snapshot->path(from,to,partial,rule,native);
            return Worker::Path{std::move(points),std::chrono::duration<double,std::micro>(Clock::now()-started).count()};
        },frames_);
        worker_->paths.emplace(key,std::move(job));++queries_;++frameQueries_;defer();
    }
    pending->second.used=frames_;
    if(!pending->second.ready()) defer();
    auto job=std::move(pending->second);worker_->paths.erase(pending);auto answer=job.future.get();
    auto result=std::move(answer.points);const auto elapsed=answer.micros;
    const std::string_view file=caller.file_name();const auto separator=file.find_last_of("/\\");
    lastFile_=file.substr(separator==file.npos?0:separator+1);lastLine_=caller.line();
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
DebugNavigationSearch::TravelRoute DebugNavigationSearch::travelRoute(const Grid &terrain,Vec from,Vec to,bool partial,std::source_location caller) {
    // Long routes are addressed by destination in the current terrain domain.
    // A tactical displacement does not cancel a queued job or change its key.
    const std::pair target{to.x,to.y};
    if(!travelAnchors_.contains(target) && travelAnchors_.size()>=64) {
        const auto oldest=std::min_element(travelAnchors_.begin(),travelAnchors_.end(),
            [](const auto &a,const auto &b){return a.second.touched<b.second.touched;});
        travelAnchors_.erase(oldest);
    }
    auto &anchor=travelAnchors_.try_emplace(target,Anchor{from,Clock::now()}).first->second;
    anchor.touched=Clock::now();
    std::deque<Vec> route;
    try {
        route=path(terrain,anchor.from,to,partial,playerMovement,true,caller);
        if(route.empty() && (anchor.from-from).length()>1) {
            // A negative result at another origin proves nothing about these feet.
            anchor.from=from;route=path(terrain,from,to,partial,playerMovement,true,caller);
        }
    } catch(const DebugSearchPending &) {defer(true);}
    return {anchor.from,std::move(route)};
}
std::deque<Vec> DebugNavigationSearch::joinTravelRoute(const Grid &terrain,const Grid &live,Vec from,Vec to) {
    auto route=travelRoute(terrain,from,to);
    if(route.points.empty()) return {};
    for(auto it=route.points.end();it!=route.points.begin();) {
        --it;
        if(live.segment(from,*it,{},playerMovement) && live.nativeMovementSegment(from,*it,playerMovement)) {
            if((from-route.origin).length()>1) ++routeJoins_;
            route.points.erase(route.points.begin(),it);
            return std::move(route.points);
        }
    }
    // A bend can hide the entire forward suffix. Join at the original feet
    // using a separately validated local path, leaving the long route cached.
    if((from-route.origin).length()>1) {
        auto connector=path(live,from,route.origin,false,playerMovement,true);
        if(!connector.empty()) {
            connector.insert(connector.end(),route.points.begin(),route.points.end());++routeJoins_;
            return connector;
        }
    }
    return {};
}
const DebugNavigationSearch::Component &DebugNavigationSearch::connectionComponent(const Grid &grid,Vec from) {
    if(!worker_) worker_=std::make_unique<Worker>();
    const Key key{grid.obstacleRevision,grid.missileCollisionRevision,bool(grid.collisionSample),grid.width,grid.height,
        playerMovement.mask,playerMovement.size,false,true,from.x,from.y,0.f,0.f};
    // The static connected set remains identical after movement within it.
    // Let an in-flight flood finish at its original feet before checking
    // membership. Only a genuinely disconnected position needs another flood.
    if(worker_->componentJob) {
        if(!worker_->componentJob->ready()) defer(true);
        auto job=std::move(*worker_->componentJob);worker_->componentJob.reset();
        worker_->component=job.future.get();
    }
    const auto &previous=worker_->component;
    if(from.x>=0 && from.y>=0 && from.x<grid.width && from.y<grid.height && !previous.cells.empty() &&
        previous.cells[size_t(from.y)*grid.width+int(from.x)]) return previous;
    if(worker_->componentKey!=key) {
        auto snapshot=worker_->freeze(grid);
        worker_->componentJob=worker_->schedule([snapshot,from] {
            Component result;
            result.cells=snapshot->reachableFrom(from,playerMovement);
            for(int y=0;y<snapshot->height;++y) for(int x=0;x<snapshot->width;++x) {
                if(!result.cells[size_t(y)*snapshot->width+x]) continue;
                const Vec at{float(x)+.5f,float(y)+.5f};
                if(debugUnknownFrontier(*snapshot,at)) result.frontiers.push_back(at);
            }
            return result;
        },frames_);
        worker_->componentKey=key;++componentSearches_;
    }
    if(worker_->componentJob) defer(true);
    return worker_->component;
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
        {"backgroundWorker",bool(worker_)},{"pendingPaths",worker_?worker_->paths.size():0},{"componentPending",worker_ && worker_->componentJob.has_value()},{"componentSearches",componentSearches_},{"routeJoins",routeJoins_},{"travelAnchors",travelAnchors_.size()},{"frameQueries",frameQueries_},{"frameCacheHits",frameHits_},
        {"framePathUs",frameMicros_},{"totalPathUs",totalMicros_},{"maxPathUs",maxMicros_},
        {"lastQuery",{{"file",lastFile_},{"line",lastLine_}}},
        {"slowestQuery",{{"file",slowestFile_},{"line",slowestLine_},{"microseconds",maxMicros_},
            {"from",{slowestFrom_.x,slowestFrom_.y}},{"to",{slowestTo_.x,slowestTo_.y}}}},
        {"cachedQueries",results_.size()},{"cachedPoints",points_},{"reachablePoints",reachable_.size()},{"segmentReuses",segmentReuses_},
        {"yielded",yielded_},{"atomicGridCall",true}};
}
} // namespace d2x
