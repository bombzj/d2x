#include "navigation_search.hpp"
#include <algorithm>
#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <limits>
#include <iterator>
#include <set>
#include <thread>
#include <type_traits>
#include <vector>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <nlohmann/json.hpp>

namespace d2x {
namespace {
bool teleportRoomPassage(const Grid &grid,const DebugNavigationSearch::TeleportRoom &a,
        const DebugNavigationSearch::TeleportRoom &b,int range) {
    // Exact standing cells, indexed by row. Room rectangles alone cannot
    // prove that a riverbank exists, and sparse samples cannot disprove it.
    std::map<int,std::vector<int>> rows;
    for(int y=std::max(b.y,a.y-range);y<std::min(b.y+b.height,a.y+a.height+range);++y)
        for(int x=std::max(b.x,a.x-range);x<std::min(b.x+b.width,a.x+a.width+range);++x)
            if(grid.walkable(Vec{float(x)+.5f,float(y)+.5f},playerMovement)) rows[y].push_back(x);
    for(int y=std::max(a.y,b.y-range);y<std::min(a.y+a.height,b.y+b.height+range);++y)
        for(int x=std::max(a.x,b.x-range);x<std::min(a.x+a.width,b.x+b.width+range);++x) {
            if(!grid.walkable(Vec{float(x)+.5f,float(y)+.5f},playerMovement)) continue;
            for(auto row=rows.lower_bound(y-range);row!=rows.end() && row->first<=y+range;++row) {
                const auto &xs=row->second;auto next=std::lower_bound(xs.begin(),xs.end(),x);
                if(next!=xs.end() && Vec{float(*next-x),float(row->first-y)}.length()<=range) return true;
                if(next!=xs.begin() && Vec{float(*std::prev(next)-x),float(row->first-y)}.length()<=range) return true;
            }
        }
    return false;
}
// A node represents an open rectangle, never an entire RoomEx. Mixed blocks
// are subdivided down to individual cells, so thin banks do not disappear
// between a room's quarter/centre samples. Edges are cached cast connections.
struct TeleportGraph {
    using Room=DebugNavigationSearch::TeleportRoom;
    struct Node {Vec at;size_t room;};
    std::shared_ptr<const Grid> grid;
    std::vector<Room> rooms;
    std::vector<Node> nodes;
    struct RoomSamples {int x,y,width,height;std::vector<uint16_t> masks;std::vector<Vec> points;};
    std::vector<RoomSamples> samples;
    std::map<std::pair<int,int>,std::vector<size_t>> bins;
    std::vector<std::optional<std::vector<size_t>>> edges;
    int range{};bool restricted{};
    bool inside(size_t room,Vec at) const {
        const auto &r=rooms[room];
        return at.x>=r.x && at.y>=r.y && at.x<r.x+r.width && at.y<r.y+r.height;
    }
    size_t roomAt(Vec at) const {
        for(size_t i=0;i<rooms.size();++i) if(inside(i,at)) return i;
        return rooms.size();
    }
    bool connected(size_t from,size_t to) const {
        if(from>=rooms.size() || to>=rooms.size()) return false;
        const auto &near=rooms[from].near;
        return from==to || std::find(near.begin(),near.end(),to)!=near.end();
    }
    bool ray(Vec from,Vec to) const {
        return !restricted || grid->interactionSegment(from-Vec{.5f,.5f},to-Vec{.5f,.5f},2,{},0x0804,2);
    }
    std::vector<size_t> neighbours(Vec at,size_t room) const {
        std::vector<size_t> result;
        const int bx=int(std::floor(at.x/range)),by=int(std::floor(at.y/range));
        for(int y=by-1;y<=by+1;++y) for(int x=bx-1;x<=bx+1;++x) {
            const auto bin=bins.find({x,y});if(bin==bins.end()) continue;
            for(const auto n:bin->second) if(connected(room,nodes[n].room) &&
                (nodes[n].at-at).length()<=range && ray(at,nodes[n].at)) result.push_back(n);
        }
        return result;
    }
    TeleportGraph(std::shared_ptr<const Grid> terrain,std::vector<Room> observed,int span,bool barrier,
            const std::shared_ptr<TeleportGraph> &previous):
        grid(std::move(terrain)),rooms(std::move(observed)),range(span),restricted(barrier) {
        const int block=std::clamp(range/3,2,8);
        for(size_t room=0;room<rooms.size();++room) {
            const auto &r=rooms[room];std::set<std::pair<int,int>> points;
            RoomSamples cached{r.x,r.y,r.width,r.height,{},{}};
            // Compare exact static masks including the footprint halo. New
            // neighbours need not resample unchanged rooms or their banks.
            for(int y=r.y-1;y<=r.y+r.height;++y) for(int x=r.x-1;x<=r.x+r.width;++x)
                cached.masks.push_back(grid->movementMask(x,y));
            const RoomSamples *reuse=nullptr;
            if(previous && previous->range==range) for(const auto &old:previous->samples)
                if(old.x==r.x && old.y==r.y && old.width==r.width && old.height==r.height && old.masks==cached.masks) {
                    reuse=&old;break;
                }
            const auto split=[&](auto &&self,int x,int y,int w,int h)->void {
                bool clear=true,any=false;
                for(int j=y;j<y+h;++j) for(int i=x;i<x+w;++i) {
                    const bool open=grid->walkable(Vec{float(i)+.5f,float(j)+.5f},playerMovement);
                    clear=clear && open;any=any || open;
                }
                if(!any) return;
                if(clear) {
                    for(const int i:{x,x+w-1}) for(const int j:{y,y+h-1}) points.emplace(i,j);
                    points.emplace(x+w/2,y+h/2);return;
                }
                if(w>=h && w>1) {self(self,x,y,w/2,h);self(self,x+w/2,y,w-w/2,h);}
                else if(h>1) {self(self,x,y,w,h/2);self(self,x,y+h/2,w,h-h/2);}
            };
            if(reuse) cached.points=reuse->points;
            else {
                for(int y=r.y;y<r.y+r.height;y+=block) for(int x=r.x;x<r.x+r.width;x+=block)
                    split(split,x,y,std::min(block,r.x+r.width-x),std::min(block,r.y+r.height-y));
                for(const auto &[x,y]:points) cached.points.push_back({float(x)+.5f,float(y)+.5f});
            }
            for(const Vec at:cached.points) {
                bins[{int(std::floor(at.x/range)),int(std::floor(at.y/range))}].push_back(nodes.size());
                nodes.push_back({at,room});
            }
            samples.push_back(std::move(cached));
        }
        edges.resize(nodes.size());
    }
    DebugNavigationSearch::TravelRoute route(Vec from,Vec to) {
        DebugNavigationSearch::TravelRoute result{from,{}};
        const size_t root=roomAt(from),goal=roomAt(to),none=nodes.size();
        if(root==rooms.size()) return result;
        const bool goalOpen=goal<rooms.size() && grid->walkable(to,playerMovement);
        const auto reachesGoal=[&](Vec at,size_t room) {
            return goalOpen && connected(room,goal) && (to-at).length()<=range && ray(at,to);
        };
        if(reachesGoal(from,root)) {result.points.push_back(to);return result;}
        using Cost=std::pair<unsigned,float>;
        std::vector<Cost> costs(nodes.size(),{std::numeric_limits<unsigned>::max(),0});
        std::vector<size_t> previous(nodes.size(),none);
        using Entry=std::tuple<unsigned,float,unsigned,float,size_t>;
        std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
        const auto enqueue=[&](size_t n) {
            const float distance=(nodes[n].at-to).length();
            queue.emplace(costs[n].first+unsigned(std::ceil(distance/range)),costs[n].second+distance,
                costs[n].first,costs[n].second,n);
        };
        for(const auto n:neighbours(from,root)) {costs[n]={1,(from-nodes[n].at).length()};enqueue(n);}
        size_t best=none;float remaining=(to-from).length();bool complete=false;
        while(!queue.empty()) {
            const auto entry=queue.top();queue.pop();const auto n=std::get<4>(entry);
            const Cost spent{std::get<2>(entry),std::get<3>(entry)};if(spent!=costs[n]) continue;
            const auto &node=nodes[n];const float distance=(node.at-to).length();
            if(distance+1<remaining) {remaining=distance;best=n;}
            if(reachesGoal(node.at,node.room)) {best=n;complete=true;break;}
            if(!edges[n]) edges[n]=neighbours(node.at,node.room);
            for(const auto next:*edges[n]) {
                const Cost total{spent.first+1,spent.second+(node.at-nodes[next].at).length()};
                if(total>=costs[next]) continue;
                costs[next]=total;previous[next]=n;enqueue(next);
            }
        }
        if(best==none) return result;
        for(size_t n=best;n!=none;n=previous[n]) result.points.push_front(nodes[n].at);
        if(complete && (result.points.back()-to).length()>.1f) result.points.push_back(to);
        return result;
    }
};
}
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
    using PassageKey=std::tuple<int,int,int,int,int,int,int,int,int>;
    std::map<PassageKey,Job<bool>> passages;
    std::map<PassageKey,bool> passageResults;
    using TeleportKey=std::tuple<float,float,int,bool>;
    struct TeleportPlan {
        TravelRoute route;size_t cursor{};uint64_t geometry{},used{};bool planned{};
        std::optional<Job<TravelRoute>> job;
    };
    std::map<TeleportKey,TeleportPlan> teleportPlans;
    // The graph is touched only by serialized worker jobs. The app thread
    // owns the shared pointer and its immutable input identity, not its edges.
    std::shared_ptr<TeleportGraph> teleportGraph;
    struct TeleportGraphResult {std::shared_ptr<TeleportGraph> graph;};
    std::optional<Job<TeleportGraphResult>> teleportGraphJob;
    std::vector<TeleportRoom> teleportRooms;
    uint64_t teleportGeometry{};int teleportRange{};bool teleportRestricted{};
    std::optional<std::pair<float,float>> teleportOrigin;
    std::optional<Key> componentKey;
    std::optional<Job<Component>> componentJob;
    Component component,unavailableComponent;
    std::vector<Component> otherComponents;
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
    void reset(bool domain=true) {
        paths.clear();componentKey.reset();componentJob.reset();component={};unavailableComponent={};otherComponents.clear();snapshots={};
        passages.clear();passageResults.clear();
        if(domain) {
            teleportPlans.clear();teleportGraph.reset();teleportGraphJob.reset();teleportRooms.clear();teleportOrigin.reset();
            std::lock_guard lock(mutex_);queue_.clear();
        }
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
    if(context_[0]!=game || context_[1]!=area) clear();
    else if(context_[2]!=geometry) {
        // Keep destination-owned teleport routes and in-flight work when
        // another room is observed. Their next segment is checked on use.
        if(worker_) worker_->reset(false);
        travelAnchors_.clear();results_.clear();reachable_.clear();wallDistances_.clear();points_=0;++resets_;
    }
    context_=next;
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
bool DebugNavigationSearch::teleportPassage(const Grid &terrain,TeleportRoom from,TeleportRoom to,int range) {
    if(!worker_) worker_=std::make_unique<Worker>();
    const Worker::PassageKey key{from.x,from.y,from.width,from.height,to.x,to.y,to.width,to.height,range};
    if(const auto found=worker_->passageResults.find(key);found!=worker_->passageResults.end()) return found->second;
    auto pending=worker_->passages.find(key);
    if(pending==worker_->passages.end()) {
        auto snapshot=worker_->freeze(terrain);
        worker_->passages.emplace(key,worker_->schedule([snapshot,from,to,range] {
            return teleportRoomPassage(*snapshot,from,to,range);
        },frames_));
        defer(true);
    }
    if(!pending->second.ready()) defer(true);
    const bool result=pending->second.future.get();worker_->passages.erase(pending);
    worker_->passageResults.emplace(key,result);return result;
}
DebugNavigationSearch::TravelRoute DebugNavigationSearch::teleportRoute(const Grid &terrain,
        const std::vector<TeleportRoom> &rooms,Vec from,Vec to,int range,bool restricted,Vec coordinateOrigin) {
    if(!worker_) worker_=std::make_unique<Worker>();
    const std::pair origin{coordinateOrigin.x,coordinateOrigin.y};
    if(worker_->teleportOrigin!=origin) {
        worker_->teleportPlans.clear();worker_->teleportGraph.reset();worker_->teleportGraphJob.reset();
        worker_->teleportOrigin=origin;
    }
    // Consume completed jobs even when their caller is temporarily suspended;
    // the task closure otherwise retains an obsolete graph and mask snapshot.
    for(auto &entry:worker_->teleportPlans) {
        auto &pending=entry.second;
        if(pending.job && pending.job->ready()) {
            pending.route=pending.job->future.get();pending.job.reset();pending.cursor=0;pending.planned=true;
        }
    }
    const Worker::TeleportKey key{to.x,to.y,range,restricted};
    if(!worker_->teleportPlans.contains(key) && worker_->teleportPlans.size()>=32) {
        const auto oldest=std::min_element(worker_->teleportPlans.begin(),worker_->teleportPlans.end(),
            [](const auto &a,const auto &b){return a.second.used<b.second.used;});
        worker_->teleportPlans.erase(oldest);
    }
    auto &plan=worker_->teleportPlans[key];plan.used=frames_;
    if(plan.job) {
        if(!plan.job->ready()) defer(true);
        plan.route=plan.job->future.get();plan.job.reset();plan.cursor=0;plan.planned=true;
    }
    // Only actual feet at a retained waypoint consume it. Merely selecting
    // a cast (or moving sideways in combat) cannot discard the suffix.
    for(size_t i=plan.route.points.size();i>plan.cursor;--i)
        if((from-plan.route.points[i-1]).length()<=2) {plan.cursor=i;break;}
    if(plan.cursor==plan.route.points.size() && !plan.route.points.empty() &&
        (plan.route.points.back()-to).length()<1 && (from-to).length()>2) plan.cursor=0;
    if(plan.cursor<plan.route.points.size())
        return {plan.route.origin,std::deque<Vec>(plan.route.points.begin()+plan.cursor,plan.route.points.end())};
    if(plan.planned && plan.geometry==context_[2] && (!plan.route.points.empty() ||
        (plan.route.origin-from).length()<2)) return {from,{}};
    bool graphArrived=false;
    if(worker_->teleportGraphJob) {
        if(!worker_->teleportGraphJob->ready()) defer(true);
        worker_->teleportGraph=worker_->teleportGraphJob->future.get().graph;
        worker_->teleportGraphJob.reset();
        graphArrived=true;
    }
    if(!worker_->teleportGraph || worker_->teleportRange!=range || worker_->teleportRestricted!=restricted ||
        ((!graphArrived || worker_->teleportGraph->roomAt(from)==worker_->teleportGraph->rooms.size()) &&
            (worker_->teleportGeometry!=context_[2] || worker_->teleportRooms!=rooms))) {
        auto snapshot=worker_->freeze(terrain);
        worker_->teleportGeometry=context_[2];worker_->teleportRooms=rooms;
        worker_->teleportRange=range;worker_->teleportRestricted=restricted;
        const auto previous=worker_->teleportGraph;
        worker_->teleportGraphJob=worker_->schedule([snapshot,rooms,range,restricted,previous] {
            return Worker::TeleportGraphResult{std::make_shared<TeleportGraph>(snapshot,rooms,range,restricted,previous)};
        },frames_);
        defer(true);
    }
    const auto graph=worker_->teleportGraph;
    plan.geometry=worker_->teleportGeometry;
    plan.job=worker_->schedule([graph,from,to] {return graph->route(from,to);},frames_);
    defer(true);
}
void DebugNavigationSearch::discardTeleportRoute(Vec to,int range,bool restricted) {
    if(worker_) worker_->teleportPlans.erase({to.x,to.y,range,restricted});
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
    const auto domain=[](const Key &value) {
        return std::tuple{std::get<0>(value),std::get<1>(value),std::get<2>(value),std::get<3>(value),std::get<4>(value)};
    };
    if(worker_->componentKey && domain(*worker_->componentKey)!=domain(key)) {
        worker_->componentKey.reset();worker_->componentJob.reset();worker_->component={};worker_->otherComponents.clear();
    }
    if(!grid.walkable(from,playerMovement)) {
        worker_->unavailableComponent.cells.assign(size_t(grid.width)*grid.height,0);
        return worker_->unavailableComponent;
    }
    // The static connected set remains identical after movement within it.
    // Let an in-flight flood finish at its original feet before checking
    // membership. Only a genuinely disconnected position needs another flood.
    if(worker_->componentJob) {
        if(!worker_->componentJob->ready()) defer(true);
        auto job=std::move(*worker_->componentJob);worker_->componentJob.reset();
        if(!worker_->component.cells.empty()) {
            size_t cells=worker_->component.cells.size();
            for(const auto &cached:worker_->otherComponents) cells+=cached.cells.size();
            if(worker_->otherComponents.size()>=3 || cells>16*1024*1024) worker_->otherComponents.clear();
            worker_->otherComponents.push_back(std::move(worker_->component));
        }
        worker_->component=job.future.get();
    }
    const auto contains=[&](const Component &component) {
        return from.x>=0 && from.y>=0 && from.x<grid.width && from.y<grid.height &&
            component.cells.size()==size_t(grid.width)*grid.height && component.cells[size_t(from.y)*grid.width+int(from.x)];
    };
    if(contains(worker_->component)) return worker_->component;
    for(auto &cached:worker_->otherComponents) if(contains(cached)) {
        std::swap(cached,worker_->component);return worker_->component;
    }
    {
        auto snapshot=worker_->freeze(grid);
        worker_->componentJob=worker_->schedule([snapshot,from] {
            Component result;
            // A component is a reusable route proof only if its elementary
            // edges obey the same native movement rule as the path executor.
            result.cells.resize(size_t(snapshot->width)*snapshot->height);
            std::vector<Vec> pending;
            if(snapshot->walkable(from,playerMovement)) {
                const Vec first{std::floor(from.x)+.5f,std::floor(from.y)+.5f};
                pending.push_back(first);result.cells[size_t(first.y)*snapshot->width+int(first.x)]=1;
            }
            for(size_t i=0;i<pending.size();++i) for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                if(!dx && !dy) continue;
                const Vec at=pending[i]+Vec{float(dx),float(dy)};
                if(!snapshot->walkable(at,playerMovement)) continue;
                const size_t cell=size_t(at.y)*snapshot->width+int(at.x);
                if(result.cells[cell] || !snapshot->segment(pending[i],at,{},playerMovement) ||
                    !snapshot->nativeMovementSegment(pending[i],at,playerMovement)) continue;
                result.cells[cell]=1;pending.push_back(at);
            }
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
    if(!grid.collisionSample && native && rule.mask==playerMovement.mask && rule.size==playerMovement.size) {
        if(!grid.walkable(from,rule) || !grid.walkable(to,rule)) return false;
        const auto &component=connectionComponent(grid,from);++proofHits_;
        return component.cells[size_t(to.y)*grid.width+int(to.x)]!=0;
    }
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
