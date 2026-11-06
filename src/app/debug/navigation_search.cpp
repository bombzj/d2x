#include "navigation_search.hpp"
#include "client/remote_town.hpp"
#include "world/navigation_snapshot.hpp"
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
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace d2x {
namespace {
thread_local std::shared_ptr<std::atomic<bool>> activeNavigationCancellation;
bool unchangedRoute(const NavigationSnapshot &before,const NavigationSnapshot &after,const NavigationRoute &route) {
    Vec previous=route.origin;
    for(const auto next:route.points) {
        if(!before.sameRegion(after,int(std::floor(std::min(previous.x,next.x)))-1,
            int(std::floor(std::min(previous.y,next.y)))-1,int(std::ceil(std::max(previous.x,next.x)))+1,
            int(std::ceil(std::max(previous.y,next.y)))+1)) return false;
        previous=next;
    }
    return !route.points.empty();
}
void checkNavigationCancellation() {
    navigationWorkCheckpoint();
    if(activeNavigationCancellation && activeNavigationCancellation->load(std::memory_order_relaxed))
        throw std::runtime_error("Navigation cancelled");
}
struct NavigationCapture {
    static size_t metadataBytes(const nlohmann::json &value) {
        size_t bytes=sizeof(nlohmann::json)+64;
        if(value.is_string()) return bytes+value.get_ref<const std::string &>().capacity();
        if(value.is_object()) for(auto entry=value.begin();entry!=value.end();++entry)
            bytes+=entry.key().capacity()+metadataBytes(entry.value());
        else if(value.is_array()) for(const auto &entry:value) bytes+=metadataBytes(entry);
        return bytes;
    }
    std::filesystem::path directory;
    std::atomic<uint64_t> sequence{},written{};
    std::atomic<uint64_t> dropped{};
    std::mutex mutex;
    std::string lastFile,error;
    std::map<std::tuple<uint64_t,uint32_t,int,int,int>,uint16_t> exploredCells;
    std::chrono::steady_clock::time_point observedAt{};
    std::chrono::steady_clock::time_point readinessAt{};
    std::optional<std::tuple<uint64_t,uint64_t,uint64_t,uint64_t,std::optional<bool>,std::optional<bool>,size_t>> readinessKey;
    std::atomic<uint64_t> requestSequence{};
    uint64_t traceSequence{},traceBytes{};
    std::ofstream timeline;
    std::map<NavigationSnapshot::Block,std::string> storedBlocks;
    uint64_t blockSequence{};
    std::mutex outputMutex;
    std::condition_variable outputWake;
    struct Output {std::function<void()> work;size_t bytes;};
    std::deque<Output> outputQueue;
    size_t outputBytes{};
    bool closing{};
    std::jthread outputThread;
    struct Active {std::shared_ptr<const Grid> grid;nlohmann::json details;std::chrono::steady_clock::time_point started;bool reported{};};
    std::mutex activeMutex;
    std::map<uint64_t,Active> active;
    std::condition_variable_any activeWake;
    std::jthread monitorThread;
    bool enqueue(std::function<void()> work,size_t bytes=4096) {
        if(directory.empty()) return false;
        {
            std::lock_guard lock(outputMutex);
            if(closing) return false;
            if(outputQueue.size()>=256 || bytes>64*1024*1024 || outputBytes>64*1024*1024-bytes) {
                ++dropped;std::lock_guard status(mutex);error="Navigation diagnostic queue full; capture incomplete";return false;
            }
            outputQueue.push_back({std::move(work),bytes});outputBytes+=bytes;
        }
        outputWake.notify_one();
        return true;
    }
    void arm(uint64_t id,std::shared_ptr<const Grid> grid,nlohmann::json details) {
        std::lock_guard lock(activeMutex);active.emplace(id,Active{std::move(grid),std::move(details),std::chrono::steady_clock::now(),false});
        activeWake.notify_all();
    }
    void disarm(uint64_t id) {std::lock_guard lock(activeMutex);active.erase(id);}
    void trace(nlohmann::json event) {
        if(directory.empty()) return;
        const auto at=std::chrono::steady_clock::now();
        event["steadyUs"]=std::chrono::duration_cast<std::chrono::microseconds>(at.time_since_epoch()).count();
        event["unixMs"]=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const auto bytes=metadataBytes(event);
        enqueue([this,event=std::move(event)]() mutable {
        if(traceBytes>=32*1024*1024) {
            std::lock_guard lock(mutex);error="Navigation timeline limit (32 MiB) reached; capture incomplete";return;
        }
        try {
            event["sequence"]=++traceSequence;
            const auto line=event.dump()+"\n";
            if(!timeline.is_open()) {
                std::filesystem::create_directories(directory);
                timeline.exceptions(std::ios::badbit|std::ios::failbit);
                timeline.open(directory/"timeline.jsonl",std::ios::binary|std::ios::app);
            }
            timeline<<line;traceBytes+=line.size();
            if(traceSequence%64==0) timeline.flush();
        } catch(const std::exception &failure) {std::lock_guard lock(mutex);error=failure.what();}
        },bytes);
    }
    NavigationCapture() {
        if(const auto *value=std::getenv("D2X_NAVIGATION_DUMP");value && *value) {
            const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            directory=std::filesystem::path(value)/("capture-"+std::to_string(now));
        }
        if(directory.empty()) return;
        outputThread=std::jthread([this] {
            std::unique_lock lock(outputMutex);
            while(true) {
                outputWake.wait(lock,[&]{return closing || !outputQueue.empty();});
                if(outputQueue.empty() && closing) break;
                auto entry=std::move(outputQueue.front());outputQueue.pop_front();
                lock.unlock();
                try {entry.work();} catch(const std::exception &failure) {std::lock_guard status(mutex);error=failure.what();}
                lock.lock();
                outputBytes-=entry.bytes;
            }
            lock.unlock();
            try {if(timeline.is_open()) timeline.close();}
            catch(const std::exception &failure) {std::lock_guard status(mutex);error=failure.what();}
        });
        monitorThread=std::jthread([this](std::stop_token stop) {
            std::unique_lock lock(activeMutex);
            while(!stop.stop_requested()) {
                activeWake.wait_for(lock,stop,std::chrono::milliseconds(100),[]{return false;});
                if(stop.stop_requested()) break;
                const auto now=std::chrono::steady_clock::now();
                std::vector<Active> expired;
                for(auto &[id,entry]:active) {
                    (void)id;
                    if(!entry.reported && now-entry.started>=std::chrono::seconds(1)) {
                        entry.reported=true;expired.push_back(entry);
                    }
                }
                lock.unlock();
                for(auto &entry:expired) {
                    entry.details["trigger"]="running-over-1000ms";
                    entry.details["elapsedMs"]=std::chrono::duration<double,std::milli>(now-entry.started).count();
                    save(*entry.grid,std::move(entry.details));
                }
                lock.lock();
            }
        });
    }
    ~NavigationCapture() {
        if(monitorThread.joinable()) {monitorThread.request_stop();activeWake.notify_all();monitorThread.join();}
        {std::lock_guard lock(outputMutex);closing=true;}
        outputWake.notify_one();if(outputThread.joinable()) outputThread.join();
    }
    void save(const Grid &grid,nlohmann::json details) {
        if(directory.empty()) return;
        auto frozen=std::make_shared<Grid>(grid);
        const size_t bytes=size_t(grid.width+2)*(grid.height+2)*sizeof(uint16_t)+metadataBytes(details);
        enqueue([this,frozen,details=std::move(details)]() mutable {saveNow(*frozen,std::move(details));},bytes);
    }
    void saveNow(const Grid &grid,nlohmann::json details) {
        const auto number=++sequence;
        if(number>128) {
            std::lock_guard lock(mutex);error="Navigation capture limit (128) reached";return;
        }
        try {
            std::filesystem::create_directories(directory);
            const auto stem=directory/("search-"+std::to_string(number));
            const auto origin=details.value("coordinateOrigin",nlohmann::json::array({0,0}));
            const int originX=int(origin.at(0).get<float>()),originY=int(origin.at(1).get<float>());
            auto chunks=nlohmann::json::array();
            for(int row=NavigationSnapshot::blockCoordinate(originY-1);row<=NavigationSnapshot::blockCoordinate(originY+grid.height);++row)
                for(int column=NavigationSnapshot::blockCoordinate(originX-1);column<=NavigationSnapshot::blockCoordinate(originX+grid.width);++column) {
                    NavigationSnapshot::Block values;
                    for(int localY=0;localY<32;++localY) for(int localX=0;localX<32;++localX)
                        values[size_t(localY)*32+localX]=grid.movementMask(column*32+localX-originX,row*32+localY-originY);
                    auto found=storedBlocks.find(values);
                    if(found==storedBlocks.end()) {
                        if(storedBlocks.size()>=8192) storedBlocks.clear();
                        const auto name="chunk-"+std::to_string(++blockSequence)+".u16le";
                        std::ofstream output(directory/name,std::ios::binary);
                        output.exceptions(std::ios::badbit|std::ios::failbit);
                        for(const uint16_t mask:values) {
                            const char bytes[]{char(mask&255),char(mask>>8)};output.write(bytes,2);
                        }
                        output.close();found=storedBlocks.emplace(std::move(values),name).first;
                    }
                    chunks.push_back({{"file",found->second},{"globalOrigin",{column*32,row*32}}});
                }
            details["format"]="d2x-navigation-capture-v2";
            details["knowledgePolicy"]="loaded-collision-input-not-explored-archive";
            details["collision"]={{"encoding","chunk32-uint16-le"},{"order","row-major"},
                {"width",grid.width+2},{"height",grid.height+2},{"localOrigin",{-1,-1}},
                {"coordinateOrigin",{originX,originY}},{"unknownMask",65535},{"chunks",std::move(chunks)}};
            details["grid"]={{"width",grid.width},{"height",grid.height}};
            const auto file=stem.string()+".json";
            std::ofstream metadata(file,std::ios::binary);
            metadata.exceptions(std::ios::badbit|std::ios::failbit);
            metadata<<details.dump(2)<<'\n';metadata.close();
            ++written;
            std::lock_guard lock(mutex);lastFile=file;
        } catch(const std::exception &failure) {
            std::lock_guard lock(mutex);error=failure.what();
        }
    }
    nlohmann::json snapshot() {
        std::lock_guard lock(mutex);
        return {{"directory",directory.string()},{"written",written.load()},{"lastFile",lastFile},{"error",error},
            {"dropped",dropped.load()},{"asynchronousWriter",true}};
    }
};
nlohmann::json captureRooms(const std::vector<DebugNavigationSearch::TeleportRoom> &rooms) {
    auto result=nlohmann::json::array();
    for(const auto &room:rooms) result.push_back({{"x",room.x},{"y",room.y},{"width",room.width},{"height",room.height},{"near",room.near}});
    return result;
}
}

nlohmann::json debugMapReadiness(const OnlineSceneView &scene) {
    const auto &loading=scene.mapLoading;const auto &rendering=scene.mapRendering;
    const auto known=[](std::optional<bool> value) {return value?nlohmann::json(*value):nlohmann::json(nullptr);};
    return {{"nativeMapReady",scene.nativeMapReady},{"reason",scene.nativeMapReason},
        {"receivedSequence",loading.receivedSequence},{"consumedSequence",loading.consumedSequence},
        {"pendingEvents",loading.pendingEvents},{"pendingMilliseconds",loading.pendingMilliseconds},
        {"terrainRevision",loading.terrainRevision},{"assignedRooms",loading.assignedRooms},{"loadedRooms",loading.loadedRooms},
        {"deferred",loading.deferred},{"terrainDirty",loading.terrainDirty},{"updateMilliseconds",loading.updateMilliseconds},
        {"clientRoomLoaded",known(loading.clientRoomLoaded)},{"clientRoomAssigned",known(loading.clientRoomAssigned)},
        {"serverRoomLoaded",known(loading.serverRoomLoaded)},{"serverRoomAssigned",known(loading.serverRoomAssigned)},
        {"rendering",{{"sample",rendering.sample},{"drawn",rendering.drawn},{"gameGeneration",rendering.gameGeneration},
            {"areaGeneration",rendering.areaGeneration},{"origin",{rendering.origin.x,rendering.origin.y}},
            {"observer",{rendering.observer.x,rendering.observer.y}},{"halfExtent",{rendering.halfExtent.x,rendering.halfExtent.y}},
            {"viewportTiles",rendering.viewportTiles},{"submittedTiles",rendering.submittedTiles},
            {"missingTextures",rendering.missingTextures},{"transparentTiles",rendering.transparentTiles},
            {"visibleObservationTiles",rendering.visibleObservationTiles}}}};
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
    struct Queued {std::weak_ptr<Task> task;unsigned priority;};
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<Queued> queue_;
    unsigned suspendedTasks_{};
    std::shared_ptr<Task> take(unsigned ceiling=std::numeric_limits<unsigned>::max()) {
        std::erase_if(queue_,[](const auto &entry){return entry.task.expired();});
        const auto next=std::min_element(queue_.begin(),queue_.end(),
            [](const auto &left,const auto &right){return left.priority<right.priority;});
        if(next==queue_.end() || next->priority>=ceiling) return {};
        auto task=next->task.lock();queue_.erase(next);return task;
    }
    std::jthread thread_{[this](std::stop_token stop) {
        std::unique_lock lock(mutex_);
        while(!stop.stop_requested()) {
            wake_.wait(lock,stop,[&]{return !queue_.empty();});
            if(stop.stop_requested()) break;
            auto task=take();
            lock.unlock();
            if(task) (*task)();
            lock.lock();
        }
    }};
  public:
        std::shared_ptr<NavigationCapture> capture=std::make_shared<NavigationCapture>();
    ~Worker() {
        for(auto &[key,job]:paths) {(void)key;job.trace->state->cancelled->store(true);}
        for(auto &[key,plan]:teleportPlans) {(void)key;if(plan.job) plan.job->trace->state->cancelled->store(true);}
        for(auto &[key,plan]:teleportPlans) {(void)key;if(plan.continuation) plan.continuation->trace->state->cancelled->store(true);}
        if(roomPlan && roomPlan->job) roomPlan->job->trace->state->cancelled->store(true);
        if(viewpointPlan && viewpointPlan->job) viewpointPlan->job->trace->state->cancelled->store(true);
        if(backtrackPlan && backtrackPlan->job) backtrackPlan->job->trace->state->cancelled->store(true);
        for(auto &plan:candidatePlans) if(plan && plan->job) plan->job->trace->state->cancelled->store(true);
        if(componentJob) componentJob->trace->state->cancelled->store(true);
        thread_.request_stop();wake_.notify_all();
        if(thread_.joinable()) thread_.join();
    }
    struct Path {std::deque<Vec> points;double micros;};
    struct TraceState {
        std::shared_ptr<NavigationCapture> capture;
        uint64_t id{};
        std::atomic<bool> computed{},consumed{};
        std::shared_ptr<std::atomic<bool>> cancelled=std::make_shared<std::atomic<bool>>(false);
        ~TraceState()=default;
    };
    struct TraceOwner {
        std::shared_ptr<TraceState> state;
        ~TraceOwner() {
            if(!state->consumed) state->cancelled->store(true,std::memory_order_relaxed);
            if(!state->consumed) state->capture->trace({{"event","discarded"},{"request",state->id},{"computed",state->computed.load()}});
        }
    };
    template<typename T> struct Job {
        std::future<T> future;
        std::shared_ptr<Task> ticket;
        uint64_t used;
        std::unique_ptr<TraceOwner> trace;
        bool ready() const {return future.wait_for(std::chrono::seconds(0))==std::future_status::ready;}
        T get() {
            if(trace) {
                trace->state->consumed=true;
                trace->state->capture->trace({{"event","consume-begin"},{"request",trace->state->id}});
            }
            auto result=future.get();
            if(trace) trace->state->capture->trace({{"event","consumed"},{"request",trace->state->id}});
            return result;
        }
    };
    std::map<Key,Job<Path>> paths;
    struct RoomTreePlan {
        std::vector<PlanningRoom> rooms;
        size_t root{};int level{},range{};Vec origin;
        uint64_t geometry{};
        std::shared_ptr<const Grid> snapshot;
        std::optional<Job<RoomTree>> job;
        std::optional<RoomTree> result;
    };
    std::optional<RoomTreePlan> roomPlan;
    struct CandidatePlan {
        Vec from,origin;uint64_t geometry{};int range{};bool restricted{};
        std::vector<Candidate> candidates;
        std::vector<TeleportRoom> rooms;
        std::shared_ptr<const Grid> snapshot;
        std::optional<Job<CandidateRoute>> job;
        std::optional<CandidateRoute> result;
    };
    std::array<std::optional<CandidatePlan>,6> candidatePlans;
    struct ViewpointPlan {
        uint64_t geometry{},observation{};
        Vec from;
        NavigationViewpointInput input;
        std::shared_ptr<const Grid> snapshot;
        std::optional<Job<NavigationExploration>> job;
        NavigationExploration result;
    };
    std::optional<ViewpointPlan> viewpointPlan;
    struct BacktrackPlan {
        NavigationBacktrackInput input;
        std::shared_ptr<const Grid> snapshot;
        std::optional<Job<NavigationBacktrack>> job;
        NavigationBacktrack result;
    };
    std::optional<BacktrackPlan> backtrackPlan;
    using PassageCache=NavigationPassageCache;
    std::shared_ptr<PassageCache> passageCache=std::make_shared<PassageCache>();
    std::shared_ptr<NavigationDerivedCache> derivedCache_=std::make_shared<NavigationDerivedCache>();
    using TeleportKey=std::tuple<float,float,int,bool>;
    struct TeleportPlan {
        TravelRoute route;size_t cursor{};uint64_t geometry{},used{};bool planned{};
        std::optional<Job<TravelRoute>> job;
        std::optional<Job<TravelRoute>> continuation;
        std::optional<TravelRoute> prepared;
        uint64_t continuationGeometry{};
        std::shared_ptr<const Grid> snapshot,continuationSnapshot;
        std::vector<TeleportRoom> rooms;
        struct Landing {Vec waypoint,requested;};
        std::optional<Landing> landing;
    };
    std::map<TeleportKey,TeleportPlan> teleportPlans;
    // The graph is touched only by serialized worker jobs. The app thread
    // owns the shared pointer and its immutable input identity, not its edges.
    std::optional<std::pair<float,float>> teleportOrigin;
    std::optional<Key> componentKey;
    std::optional<Job<Component>> componentJob;
    Component component,unavailableComponent;
    std::vector<Component> otherComponents;
    using Domain=std::tuple<uint64_t,uint64_t,int,int>;
    struct Frozen {std::optional<Domain> domain;std::shared_ptr<const Grid> grid;std::shared_ptr<const NavigationSnapshot> publication;};
    std::array<Frozen,2> snapshots;
    std::shared_ptr<const Grid> freeze(const Grid &source) {
        const auto started=Clock::now();
        auto &cached=snapshots[bool(source.collisionSample)];
        const Domain domain{source.obstacleRevision,source.missileCollisionRevision,source.width,source.height};
        if(source.freezeMovement) {
            const auto published=source.freezeMovement();
            if(published) {
                if(cached.publication==published || (cached.publication && published->base &&
                    cached.publication->base==published->base && cached.publication->overlay==published->overlay)) {
                    cached.domain=domain;return cached.grid;
                }
                cached={domain,published->grid(),published};
                capture->trace({{"event","snapshot-shared"},{"publication",published->publication},
                    {"scannedCells",published->scannedCells},{"copiedBytes",published->copiedBytes},{"reusedBlocks",published->reusedBlocks}});
                return cached.grid;
            }
            throw std::runtime_error("Navigation publication is unavailable");
        }
        if(cached.domain==domain) return cached.grid;
        capture->trace({{"event","freeze-begin"},{"width",source.width},{"height",source.height},
            {"obstacleRevision",source.obstacleRevision},{"missileCollisionRevision",source.missileCollisionRevision}});
        // Include the one-cell halo sampled by native footprint checks.
        const int stride=source.width+2,rows=source.height+2;
        auto masks=source.movementMaskSnapshot();
        auto grid=std::make_shared<Grid>();grid->width=source.width;grid->height=source.height;
        grid->collisionSample=[stride,rows,masks=std::move(masks)](int x,int y,EntityId)->uint16_t {
            if(x< -1 || y< -1 || x>=stride-1 || y>=rows-1) return 0xffff;
            return masks[size_t(y+1)*stride+(x+1)];
        };
        cached={domain,grid,{}};
        capture->trace({{"event","freeze-end"},{"elapsedMs",std::chrono::duration<double,std::milli>(Clock::now()-started).count()}});
        return grid;
    }
    template<typename F> auto schedule(F work,uint64_t frame,std::shared_ptr<const Grid> grid={},nlohmann::json details={}) {
        using T=std::invoke_result_t<F>;
        const unsigned priority=std::is_same_v<T,Path>?0:std::is_same_v<T,CandidateRoute>?1:
            details.value("continuation",false)?3:2;
        auto trace=std::make_shared<TraceState>();trace->capture=capture;trace->id=++capture->requestSequence;
        if(!capture->directory.empty()) {
            details["request"]=trace->id;
            auto submitted=details;submitted["event"]="submitted";submitted["frame"]=frame;
            capture->trace(std::move(submitted));
        }
        const auto queued=std::chrono::steady_clock::now();
        auto task=std::make_shared<std::packaged_task<T()>>([this,priority,work=std::move(work),grid,details=std::move(details),queued,capture=capture,trace,cache=derivedCache_]() mutable {
            NavigationCacheScope cacheScope(*cache);
            struct CancellationScope {
                std::shared_ptr<std::atomic<bool>> previous;
                ~CancellationScope() {activeNavigationCancellation=std::move(previous);}
            } cancellationScope{activeNavigationCancellation};
            activeNavigationCancellation=trace->cancelled;
            auto checkpointAt=Clock::now();Clock::duration suspended{};
            NavigationWorkScope workScope([&] {
                if(trace->cancelled->load(std::memory_order_relaxed)) throw std::runtime_error("Navigation cancelled");
                const auto now=Clock::now();
                if(now-checkpointAt<std::chrono::milliseconds(2)) return;
                std::shared_ptr<Task> urgent;
                if(!suspendedTasks_) {std::lock_guard lock(mutex_);urgent=take(priority);}
                if(urgent) {
                    ++suspendedTasks_;
                    const auto before=Clock::now();(*urgent)();suspended+=Clock::now()-before;
                    --suspendedTasks_;
                }
                checkpointAt=Clock::now();
            });
            if(trace->cancelled->load(std::memory_order_relaxed)) throw std::runtime_error("Navigation request cancelled before execution");
            if(!grid || capture->directory.empty()) {
                auto result=work();trace->computed=true;return result;
            }
            const auto started=std::chrono::steady_clock::now();
            details["queueMs"]=std::chrono::duration<double,std::milli>(started-queued).count();
            details["startedUnixMs"]=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            capture->trace({{"event","started"},{"request",trace->id},{"queueMs",details["queueMs"]}});
            capture->arm(trace->id,grid,details);
            const auto finish=[&] {capture->disarm(trace->id);};
            try {
                const auto computeStarted=Clock::now();
                auto result=work();
                const double elapsed=std::chrono::duration<double,std::milli>(Clock::now()-computeStarted-suspended).count();
                trace->computed=true;
                capture->trace({{"event","computed"},{"request",trace->id},{"computeMs",elapsed},
                    {"suspendedMs",std::chrono::duration<double,std::milli>(suspended).count()},
                    {"workingBudgetPeakBytes",cache->peakActiveBytes},{"walkingBlockHits",cache->hits},
                    {"walkingBlockBuilds",cache->builds},{"walkingBlocks",cache->walking.size()}});
                finish();
                bool failed=false;
                if constexpr(std::is_same_v<T,Path> || std::is_same_v<T,TravelRoute>) {
                    failed=result.points.empty();details["points"]=nlohmann::json::array();
                    for(const auto at:result.points) details["points"].push_back({at.x,at.y});
                    if constexpr(std::is_same_v<T,TravelRoute>) details["outcome"]=int(result.outcome);
                } else if constexpr(std::is_same_v<T,CandidateRoute>) {
                    failed=!result.found;details["outcome"]=int(result.route.outcome);
                    details["travelCost"]=result.travelCost;details["score"]=result.score;
                    details["costUnit"]=result.costInSeconds?"seconds":"relative";
                    details["selectedCandidate"]=result.found?nlohmann::json(result.candidate):nlohmann::json(nullptr);
                    details["expanded"]=result.expanded;details["points"]=nlohmann::json::array();
                    for(const auto point:result.route.points) details["points"].push_back({point.x,point.y});
                } else if constexpr(std::is_same_v<T,NavigationExploration> || std::is_same_v<T,NavigationBacktrack>) {
                    failed=!result.selection.found;details["outcome"]=int(result.selection.route.outcome);
                    details["travelCost"]=result.selection.travelCost;details["score"]=result.selection.score;
                    details["costUnit"]=result.selection.costInSeconds?"seconds":"relative";
                    details["expanded"]=result.selection.expanded;
                    details["selectedCandidate"]=result.selection.found?nlohmann::json(result.selection.candidate):nlohmann::json(nullptr);
                    details["points"]=nlohmann::json::array();
                    for(const auto point:result.selection.route.points) details["points"].push_back({point.x,point.y});
                    if constexpr(std::is_same_v<T,NavigationBacktrack>) {
                        details["destination"]=result.selection.found?nlohmann::json(result.destination):nlohmann::json(nullptr);
                        details["candidateCount"]=result.candidates;
                        details["previous"]=result.tree.previous;details["depth"]=result.tree.depth;
                    }
                } else if constexpr(std::is_same_v<T,RoomTree>) {
                    details["previous"]=result.previous;details["depth"]=result.depth;
                    failed=std::count_if(result.previous.begin(),result.previous.end(),[&](size_t value) {
                        return value<result.previous.size();
                    })<=1;
                } else if constexpr(std::is_same_v<T,bool>) failed=!result;
                nlohmann::json summary{{"event","result"},{"request",trace->id},{"empty",failed}};
                for(const auto *field:{"outcome","expanded","travelCost","score","costUnit","selectedCandidate","destination"})
                    if(details.contains(field)) summary[field]=details.at(field);
                if(details.contains("points")) summary["routePointCount"]=details.at("points").size();
                if constexpr(std::is_same_v<T,NavigationExploration>) summary["candidateCount"]=result.viewpoints.size();
                if constexpr(std::is_same_v<T,NavigationBacktrack>) summary["candidateCount"]=result.candidates;
                if constexpr(std::is_same_v<T,Path> || std::is_same_v<T,TravelRoute> || std::is_same_v<T,CandidateRoute> ||
                    std::is_same_v<T,NavigationExploration> || std::is_same_v<T,NavigationBacktrack>) {
                    const auto &points=[&]() -> const std::deque<Vec> & {
                        if constexpr(std::is_same_v<T,Path> || std::is_same_v<T,TravelRoute>) return result.points;
                        else if constexpr(std::is_same_v<T,CandidateRoute>) return result.route.points;
                        else return result.selection.route.points;
                    }();
                    if(!points.empty()) summary["endpoint"]={points.back().x,points.back().y};
                    if constexpr(!std::is_same_v<T,Path>) {
                        const auto &route=[&]() -> const NavigationRoute & {
                            if constexpr(std::is_same_v<T,TravelRoute>) return result;
                            else if constexpr(std::is_same_v<T,CandidateRoute>) return result.route;
                            else return result.selection.route;
                        }();
                        float length=0;auto previous=route.origin;
                        for(const auto point:points) {length+=(point-previous).length();previous=point;}
                        summary["routeLength"]=length;
                    }
                }
                capture->trace(std::move(summary));
                if(!trace->cancelled->load() && (failed || elapsed>1000 || details["queueMs"].get<double>()>1000)) {
                    details["trigger"]=failed?"no-result":"slow-completed";details["elapsedMs"]=elapsed;
                    if(details.contains("outcome")) {
                        const auto outcome=NavigationOutcome(details.at("outcome").get<int>());
                        if(outcome==NavigationOutcome::NoRoute) details["trigger"]="no-route";
                        else if(outcome==NavigationOutcome::NeedObservation) details["trigger"]="need-observation";
                        else if(outcome==NavigationOutcome::ResourceLimited) details["trigger"]="resource-limited";
                        else if(outcome==NavigationOutcome::StartBlocked) details["trigger"]="start-blocked";
                    }
                    capture->save(*grid,std::move(details));
                }
                capture->trace({{"event","returning"},{"request",trace->id},
                    {"workerMs",std::chrono::duration<double,std::milli>(Clock::now()-started).count()}});
                return result;
            } catch(...) {
                finish();capture->trace({{"event",trace->cancelled->load()?"cancelled":"exception"},{"request",trace->id}});
                if(!trace->cancelled->load()) {details["trigger"]="exception";capture->save(*grid,std::move(details));}
                throw;
            }
        });
        Job<T> job{task->get_future(),std::make_shared<Task>([task]{(*task)();}),frame,{}};
        job.trace=std::make_unique<TraceOwner>(trace);
        {std::lock_guard lock(mutex_);
            std::erase_if(queue_,[](const auto &entry){return entry.task.expired();});
            if(queue_.size()>=256) throw std::runtime_error("Navigation request queue capacity exhausted");
            queue_.push_back({job.ticket,priority});
        }
        wake_.notify_one();return job;
    }
    void reset(bool domain=true) {
        auto discardedRequests=nlohmann::json::array();
        if(!capture->directory.empty()) {
            const auto append=[&](const auto &job) {discardedRequests.push_back(job.trace->state->id);};
            for(const auto &[key,job]:paths) {(void)key;append(job);}
            if(componentJob) append(*componentJob);
            if(domain) {
                if(roomPlan && roomPlan->job) append(*roomPlan->job);
                if(viewpointPlan && viewpointPlan->job) append(*viewpointPlan->job);
                if(backtrackPlan && backtrackPlan->job) append(*backtrackPlan->job);
                for(const auto &plan:candidatePlans) if(plan && plan->job) append(*plan->job);
                for(const auto &[key,plan]:teleportPlans) {
                    (void)key;if(plan.job) append(*plan.job);if(plan.continuation) append(*plan.continuation);
                }
            }
        }
        capture->trace({{"event","reset"},{"reason",domain?"game-or-area-clear":"geometry-changed"},
            {"paths",paths.size()},{"teleportPlans",teleportPlans.size()},{"discardedRequests",std::move(discardedRequests)}});
        paths.clear();componentKey.reset();componentJob.reset();component={};unavailableComponent={};otherComponents.clear();
        if(domain) {
            snapshots={};
            roomPlan.reset();
            viewpointPlan.reset();
            backtrackPlan.reset();
            for(auto &plan:candidatePlans) plan.reset();
            passageCache=std::make_shared<PassageCache>();
            derivedCache_=std::make_shared<NavigationDerivedCache>();
            teleportPlans.clear();teleportOrigin.reset();
            std::lock_guard lock(mutex_);queue_.clear();
        }
    }
};
DebugNavigationSearch::DebugNavigationSearch()=default;
DebugNavigationSearch::~DebugNavigationSearch()=default;
void DebugNavigationSearch::traceDecision(nlohmann::json details) {
    if(!worker_ || worker_->capture->directory.empty()) return;
    details["event"]="decision";details["context"]=context_;details["frame"]=frames_;
    details["lastRequest"]=worker_->capture->requestSequence.load();
    details["pending"]=nlohmann::json::array();
    const auto append=[&](const auto &job) {
        if(job.trace) details["pending"].push_back({{"request",job.trace->state->id},{"ready",job.ready()},
            {"computed",job.trace->state->computed.load()}});
    };
    for(const auto &[key,job]:worker_->paths) {(void)key;append(job);}
    for(const auto &[key,plan]:worker_->teleportPlans) {(void)key;if(plan.job) append(*plan.job);}
    for(const auto &[key,plan]:worker_->teleportPlans) {(void)key;if(plan.continuation) append(*plan.continuation);}
    if(worker_->componentJob) append(*worker_->componentJob);
    if(worker_->roomPlan && worker_->roomPlan->job) append(*worker_->roomPlan->job);
    if(worker_->viewpointPlan && worker_->viewpointPlan->job) append(*worker_->viewpointPlan->job);
    if(worker_->backtrackPlan && worker_->backtrackPlan->job) append(*worker_->backtrackPlan->job);
    for(const auto &plan:worker_->candidatePlans) if(plan && plan->job) append(*plan->job);
    worker_->capture->trace(std::move(details));
}
void DebugNavigationSearch::observeExplored(const RemoteTown &town,const OnlineView &view,std::optional<Vec> position) {
    const auto *enabled=std::getenv("D2X_NAVIGATION_DUMP");
    if(!enabled || !*enabled) return;
    if(!worker_) worker_=std::make_unique<Worker>();
    auto &capture=*worker_->capture;
    const auto now=Clock::now();
    const auto &loading=town.read().mapLoading;
    const auto key=std::tuple{view.gameGeneration,view.world.areaGeneration,loading.receivedSequence,
        loading.consumedSequence,loading.clientRoomAssigned,loading.serverRoomAssigned,town.read().mapRendering.missingTextures};
    if(capture.readinessKey!=key || now-capture.readinessAt>=std::chrono::milliseconds(250)) {
        capture.readinessKey=key;capture.readinessAt=now;
        capture.trace({{"event","map-readiness"},{"context",context_},{"frame",frames_},
            {"clientPosition",position?nlohmann::json{position->x,position->y}:nlohmann::json(nullptr)},
            {"serverPosition",view.world.playerPosition?nlohmann::json{view.world.playerPosition->x,view.world.playerPosition->y}:nlohmann::json(nullptr)},
            {"state",debugMapReadiness(town.read())}});
    }
    if(now-capture.observedAt<std::chrono::milliseconds(250)) return;
    capture.observedAt=now;
    const auto observation=town.navigationObservation();
    const auto *map=town.map();const auto &scene=town.read();
    if(!observation || !map || !scene.area || !scene.origin || !view.load.mapSeed) return;
    const auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    nlohmann::json record{{"format","d2x-explored-collision-v1"},{"unixMs",stamp},{"game",view.gameGeneration},
        {"level",*scene.area},{"mapSeed",*view.load.mapSeed},{"geometry",context_[2]},
        {"clientPosition",position?nlohmann::json{position->x,position->y}:nlohmann::json(nullptr)},
        {"serverPosition",view.world.playerPosition?nlohmann::json{view.world.playerPosition->x,view.world.playerPosition->y}:nlohmann::json(nullptr)},
        {"cells",nlohmann::json::array()}};
    for(int row=0;row<observation->height;++row) for(int column=0;column<observation->width;++column) {
        if(!observation->seen[size_t(row)*observation->width+column]) continue;
        const int globalX=(observation->tileX+column)*5,globalY=(observation->tileY+row)*5;
        for(int dy=0;dy<5;++dy) for(int dx=0;dx<5;++dx) {
            const int x=globalX+dx,y=globalY+dy;
            const auto mask=map->grid.movementMask(x-scene.origin->x,y-scene.origin->y);
            if(mask==0xffff) continue;
            const auto key=std::tuple{view.gameGeneration,*view.load.mapSeed,int(*scene.area),x,y};
            const auto previous=capture.exploredCells.find(key);
            if(previous!=capture.exploredCells.end() && previous->second==mask) continue;
            record["cells"].push_back({x,y,mask});
        }
    }
    const auto folder=capture.directory/("game-"+std::to_string(view.gameGeneration)+"-seed-"+std::to_string(*view.load.mapSeed));
    const auto cells=record["cells"];
    const auto recordBytes=NavigationCapture::metadataBytes(record);
    if(capture.exploredCells.size()+cells.size()>512*1024) capture.exploredCells.clear();
    const bool accepted=capture.enqueue([folder,record=std::move(record)]() mutable {
        std::filesystem::create_directories(folder);
        if(!record["cells"].empty()) {
            std::ofstream output(folder/("level-"+std::to_string(record["level"].get<int>())+".jsonl"),std::ios::binary|std::ios::app);
            output.exceptions(std::ios::badbit|std::ios::failbit);
            output<<record.dump()<<'\n';output.close();
        }
        record.erase("cells");
        std::ofstream positions(folder/"positions.jsonl",std::ios::binary|std::ios::app);
        positions.exceptions(std::ios::badbit|std::ios::failbit);
        positions<<record.dump()<<'\n';positions.close();
    },recordBytes);
    if(accepted) for(const auto &cell:cells)
        capture.exploredCells[{view.gameGeneration,*view.load.mapSeed,int(*scene.area),cell[0].get<int>(),cell[1].get<int>()}]=cell[2].get<uint16_t>();
}
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
    lastDeferred_={};waitingSince_={};travelAnchors_.clear();walkingRoutes_.clear();results_.clear();
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
    std::erase_if(walkingRoutes_,[&](const auto &entry){return frames_-entry.second.used>3600;});
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
        },frames_,snapshot,{{"kind","path"},{"context",context_},{"from",{from.x,from.y}},{"to",{to.x,to.y}},
            {"partial",partial},{"native",native},{"mask",rule.mask},{"size",rule.size},
            {"obstacleRevision",grid.obstacleRevision},{"missileCollisionRevision",grid.missileCollisionRevision},
            {"liveCollision",bool(grid.collisionSample)},{"caller",caller.file_name()},{"line",caller.line()}});
        worker_->paths.emplace(key,std::move(job));++queries_;++frameQueries_;defer();
    }
    pending->second.used=frames_;
    if(!pending->second.ready()) defer();
    auto job=std::move(pending->second);worker_->paths.erase(pending);auto answer=job.get();
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
DebugNavigationSearch::RoomTree DebugNavigationSearch::roomTree(const Grid &terrain,
        const std::vector<PlanningRoom> &rooms,size_t root,int level,int range,Vec origin) {
    if(!worker_) worker_=std::make_unique<Worker>();
    auto &plan=worker_->roomPlan;
    const auto currentSnapshot=worker_->freeze(terrain);
    if(plan && plan->snapshot==currentSnapshot) plan->geometry=context_[2];
    const bool identity=plan && plan->root==root && plan->level==level && plan->range==range &&
        plan->origin.x==origin.x && plan->origin.y==origin.y && plan->rooms==rooms;
    if(!identity) plan.reset();
    if(plan && plan->job) {
        if(!plan->job->ready()) defer(true);
        plan->result=plan->job->get();plan->job.reset();
        if(plan->snapshot==currentSnapshot) return *plan->result;
    }
    if(plan && plan->snapshot==currentSnapshot && plan->result) return *plan->result;
    auto snapshot=currentSnapshot;
    const auto geometry=context_[2];
    plan.emplace();plan->rooms=rooms;plan->root=root;plan->level=level;plan->range=range;
    plan->origin=origin;plan->geometry=geometry;plan->snapshot=snapshot;
    const auto cache=worker_->passageCache;
    auto capturedRooms=nlohmann::json::array();
    if(!worker_->capture->directory.empty()) for(const auto &room:rooms) capturedRooms.push_back({{"level",room.level},{"x",room.x},{"y",room.y},
        {"width",room.width},{"height",room.height},{"loaded",room.loaded},{"exploredTiles",room.exploredTiles},{"near",room.near}});
    plan->job=worker_->schedule([snapshot,rooms,root,level,range,origin,geometry,cache] {
        return planRoomTree(*snapshot,rooms,root,level,range,origin,geometry,*cache,activeNavigationCancellation);
    },frames_,snapshot,{{"kind","room-tree"},{"context",context_},{"root",root},{"level",level},{"range",range},
        {"coordinateOrigin",{origin.x,origin.y}},{"rooms",std::move(capturedRooms)}});
    defer(true);
}
NavigationExploration DebugNavigationSearch::exploration(const Grid &terrain,
    const NavigationViewpointInput &input,uint64_t observationRevision,bool prefetch) {
    if(!worker_) worker_=std::make_unique<Worker>();
    auto &plan=worker_->viewpointPlan;
    const auto currentSnapshot=worker_->freeze(terrain);
    if(plan && plan->snapshot==currentSnapshot) plan->geometry=context_[2];
    const auto sameInput=[&] {
        if(!plan) return false;
        const auto &old=plan->input;
        if(old.origin.x!=input.origin.x || old.origin.y!=input.origin.y || old.halfExtent.x!=input.halfExtent.x ||
            old.halfExtent.y!=input.halfExtent.y || old.teleportRange!=input.teleportRange || old.outdoor!=input.outdoor ||
            old.mobility!=input.mobility || old.objective!=input.objective || old.cost!=input.cost ||
            old.restricted!=input.restricted || old.branchScale!=input.branchScale || old.rooms!=input.rooms || old.unseenBands!=input.unseenBands ||
            old.hub.has_value()!=input.hub.has_value() || old.exclusions.size()!=input.exclusions.size() ||
            old.boundaries.size()!=input.boundaries.size()) return false;
        if(old.hub && (old.hub->x!=input.hub->x || old.hub->y!=input.hub->y)) return false;
        for(size_t index=0;index<old.exclusions.size();++index) {
            const auto &left=old.exclusions[index],&right=input.exclusions[index];
            if(left.point.x!=right.point.x || left.point.y!=right.point.y || left.radius!=right.radius) return false;
        }
        for(size_t index=0;index<old.boundaries.size();++index) {
            const auto &left=old.boundaries[index],&right=input.boundaries[index];
            if(left.side!=right.side || left.plane!=right.plane || left.start!=right.start || left.end!=right.end) return false;
        }
        return true;
    };
    const bool changed=plan && (plan->snapshot!=currentSnapshot || plan->observation!=observationRevision ||
        (plan->from-input.from).length()>=2 || !sameInput());
    if(changed && (!plan->job || plan->input.origin.x!=input.origin.x || plan->input.origin.y!=input.origin.y ||
        plan->input.teleportRange!=input.teleportRange || plan->input.restricted!=input.restricted ||
        plan->input.mobility!=input.mobility || plan->input.objective!=input.objective || plan->input.cost!=input.cost)) plan.reset();
    if(prefetch && plan) return {};
    if(plan && plan->job) {
        if(!plan->job->ready()) defer(true);
        plan->result=plan->job->get();plan->job.reset();
        if(changed) {
            auto &selected=plan->result.selection;
            bool valid=selected.found && (plan->from-input.from).length()<2 &&
                navigationRouteValid(terrain,input.rooms,selected.route,input.teleportRange,input.restricted);
            if(valid) {
                const Vec world=selected.route.points.back()+input.origin-Vec{.5f,.5f};
                valid=std::none_of(input.exclusions.begin(),input.exclusions.end(),[&](const auto &entry) {
                    return (entry.point-world).length()<entry.radius;
                });
                size_t gain=0;
                const float margin=input.outdoor?0.f:5.f;
                const float halfX=std::max(0.f,input.halfExtent.x-margin),halfY=std::max(0.f,input.halfExtent.y-margin);
                for(auto band=input.unseenBands.lower_bound(int(std::ceil(world.x-world.y-halfX)));
                    band!=input.unseenBands.end() && band->first<=world.x-world.y+halfX;++band)
                    gain+=std::upper_bound(band->second.begin(),band->second.end(),world.x+world.y+halfY)-
                        std::lower_bound(band->second.begin(),band->second.end(),world.x+world.y-halfY);
                valid=valid && gain>0;
                if(valid) plan->result.viewpoints.at(selected.candidate).gain=gain;
            }
            if(!valid) {plan.reset();return exploration(terrain,input,observationRevision);}
        }
        const auto &selected=plan->result.selection;
        if(selected.route.outcome==NavigationOutcome::ResourceLimited)
            throw std::runtime_error("Exploration exceeded memory budget; no-route is not established");
        if(selected.route.outcome==NavigationOutcome::Cancelled) {plan.reset();defer(true);}
        if(selected.found && !selected.route.points.empty()) {
            const auto target=selected.route.points.back();const auto from=selected.route.origin;
            if(input.teleportRange>0) {
                const std::pair origin{input.origin.x,input.origin.y};
                if(worker_->teleportOrigin!=origin) {worker_->teleportPlans.clear();worker_->teleportOrigin=origin;}
                auto &route=worker_->teleportPlans[{target.x,target.y,input.teleportRange,input.restricted}];
                route.job.reset();route.continuation.reset();route.prepared.reset();route.continuationGeometry=0;
                route.route=selected.route;route.cursor=0;route.geometry=context_[2];route.used=frames_;route.planned=true;route.landing.reset();
                route.snapshot=currentSnapshot;route.rooms=input.rooms;route.continuationSnapshot.reset();
            } else {
                const Key key{terrain.obstacleRevision,terrain.missileCollisionRevision,bool(terrain.collisionSample),
                    terrain.width,terrain.height,playerMovement.mask,playerMovement.size,false,true,from.x,from.y,target.x,target.y};
                const auto existing=results_.find(key);if(existing!=results_.end()) points_-=existing->second.path.size();
                if(selected.route.points.size()>262144-points_ || results_.size()>=4096) {
                    results_.clear();reachable_.clear();points_=0;
                }
                results_[key]={selected.route.points,frames_};points_+=selected.route.points.size();
                travelAnchors_[{target.x,target.y}]={from,Clock::now()};
            }
        }
    }
    if(plan) {
        auto result=plan->result;
        if(changed) plan.reset();
        return result;
    }
    const auto snapshot=currentSnapshot;
    plan.emplace();plan->geometry=context_[2];plan->observation=observationRevision;plan->from=input.from;plan->input=input;plan->snapshot=snapshot;
    auto exclusions=nlohmann::json::array(),boundaries=nlohmann::json::array(),bands=nlohmann::json::array();
    if(!worker_->capture->directory.empty()) {
        for(const auto &entry:input.exclusions) exclusions.push_back({{"point",{entry.point.x,entry.point.y}},{"radius",entry.radius}});
        for(const auto &entry:input.boundaries) boundaries.push_back({entry.side,entry.plane,entry.start,entry.end});
        for(const auto &[diagonal,values]:input.unseenBands) bands.push_back({diagonal,values});
    }
    plan->job=worker_->schedule([snapshot,input] {return planExploration(*snapshot,input,activeNavigationCancellation);},
        frames_,snapshot,{{"kind","exploration-plan"},{"context",context_},{"observation",observationRevision},
            {"from",{input.from.x,input.from.y}},{"coordinateOrigin",{input.origin.x,input.origin.y}},
            {"halfExtent",{input.halfExtent.x,input.halfExtent.y}},{"unseenBands",std::move(bands)},
            {"rooms",worker_->capture->directory.empty()?nlohmann::json::array():captureRooms(input.rooms)},
            {"exclusions",std::move(exclusions)},{"boundaries",std::move(boundaries)},
            {"hub",input.hub?nlohmann::json{input.hub->x,input.hub->y}:nlohmann::json(nullptr)},
            {"branchScale",input.branchScale},{"range",input.teleportRange},{"restricted",input.restricted},{"outdoor",input.outdoor},
            {"mobility",int(input.mobility)},{"objective",int(input.objective)},
            {"cost",{{"walkUnit",input.cost.walkUnit},{"teleportCast",input.cost.teleportCast},
                {"observation",input.cost.observation},{"seconds",input.cost.seconds}}}});
    if(prefetch) return {};
    defer(true);
}
NavigationBacktrack DebugNavigationSearch::backtrack(const Grid &terrain,const NavigationBacktrackInput &input) {
    if(!worker_) worker_=std::make_unique<Worker>();
    auto &plan=worker_->backtrackPlan;
    const auto snapshot=worker_->freeze(terrain);
    const auto same=[&] {
        if(!plan) return false;
        const auto &old=plan->input;
        return old.origin.x==input.origin.x && old.origin.y==input.origin.y && old.from.x==input.from.x && old.from.y==input.from.y &&
            old.rooms==input.rooms && old.routeRooms==input.routeRooms && old.destinations==input.destinations && old.priorities==input.priorities && old.weights==input.weights &&
            old.root==input.root && old.level==input.level && old.range==input.range && old.restricted==input.restricted &&
            old.clearing==input.clearing && old.mobility==input.mobility && old.cost==input.cost &&
            old.exclusions.size()==input.exclusions.size() &&
            std::equal(old.exclusions.begin(),old.exclusions.end(),input.exclusions.begin(),[](const auto &left,const auto &right) {
                return left.point.x==right.point.x && left.point.y==right.point.y && left.radius==right.radius;
            });
    };
    if(!same()) plan.reset();
    if(plan && plan->job) {
        if(!plan->job->ready()) defer(true);
        plan->result=plan->job->get();plan->job.reset();
    }
    if(plan) {
        const auto &selection=plan->result.selection;
        if(selection.route.outcome==NavigationOutcome::ResourceLimited)
            throw std::runtime_error("Backtrack memory budget exhausted; no-route is not established");
        if(selection.route.outcome==NavigationOutcome::Cancelled) {plan.reset();defer(true);}
        if(plan->snapshot!=snapshot && !(selection.found &&
            navigationRouteValid(terrain,plan->result.routeRooms,selection.route,input.range,input.restricted))) plan.reset();
    }
    if(plan) {
        plan->snapshot=snapshot;
        const auto &selection=plan->result.selection;
        if(selection.found && !selection.route.points.empty()) {
            const auto target=selection.route.points.back();
            if(input.range>0) {
                const std::pair origin{input.origin.x,input.origin.y};
                if(worker_->teleportOrigin!=origin) {worker_->teleportPlans.clear();worker_->teleportOrigin=origin;}
                auto &retained=worker_->teleportPlans[{target.x,target.y,input.range,input.restricted}];
                retained.job.reset();retained.continuation.reset();retained.prepared.reset();retained.continuationSnapshot.reset();
                retained.continuationGeometry=0;retained.route=selection.route;retained.cursor=0;retained.geometry=context_[2];
                retained.used=frames_;retained.planned=true;retained.landing.reset();retained.snapshot=snapshot;
                retained.rooms=plan->result.routeRooms;
            } else {
                const auto from=selection.route.origin;
                const Key key{terrain.obstacleRevision,terrain.missileCollisionRevision,bool(terrain.collisionSample),
                    terrain.width,terrain.height,playerMovement.mask,playerMovement.size,false,true,from.x,from.y,target.x,target.y};
                if(const auto found=results_.find(key);found!=results_.end()) points_-=found->second.path.size();
                if(selection.route.points.size()>262144-points_ || results_.size()>=4096) {results_.clear();reachable_.clear();points_=0;}
                results_[key]={selection.route.points,frames_};points_+=selection.route.points.size();
                travelAnchors_[{target.x,target.y}]={from,Clock::now()};
            }
        }
        return plan->result;
    }
    plan.emplace();plan->input=input;plan->snapshot=snapshot;
    auto rooms=nlohmann::json::array(),exclusions=nlohmann::json::array();
    if(!worker_->capture->directory.empty()) {
        for(const auto &room:input.rooms) rooms.push_back({{"level",room.level},{"x",room.x},{"y",room.y},
            {"width",room.width},{"height",room.height},{"loaded",room.loaded},{"exploredTiles",room.exploredTiles},{"near",room.near}});
        for(const auto &entry:input.exclusions) exclusions.push_back({{"point",{entry.point.x,entry.point.y}},{"radius",entry.radius}});
    }
    const auto cache=worker_->passageCache;
    plan->job=worker_->schedule([snapshot,input,cache] {return planBacktrack(*snapshot,input,*cache,activeNavigationCancellation);},
        frames_,snapshot,{{"kind","backtrack-plan"},{"context",context_},{"from",{input.from.x,input.from.y}},
            {"coordinateOrigin",{input.origin.x,input.origin.y}},{"rooms",std::move(rooms)},{"exclusions",std::move(exclusions)},
            {"routeRooms",worker_->capture->directory.empty()?nlohmann::json::array():captureRooms(input.routeRooms)},
            {"destinations",input.destinations},{"priorities",input.priorities},{"weights",input.weights},{"root",input.root},
            {"level",input.level},{"range",input.range},{"restricted",input.restricted},{"clearing",input.clearing},
            {"mobility",int(input.mobility)},{"cost",{{"walkUnit",input.cost.walkUnit},{"teleportCast",input.cost.teleportCast},
                {"observation",input.cost.observation},{"seconds",input.cost.seconds}}}});
    defer(true);
}
DebugNavigationSearch::CandidateRoute DebugNavigationSearch::selectCandidate(const Grid &terrain,Vec from,
    const std::vector<Candidate> &candidates,const std::vector<TeleportRoom> &rooms,int range,bool restricted,Vec origin,PlanningPurpose purpose) {
    if(!worker_) worker_=std::make_unique<Worker>();
    auto &plan=worker_->candidatePlans.at(size_t(purpose));
    const auto currentSnapshot=worker_->freeze(terrain);
    if(plan && plan->snapshot==currentSnapshot) plan->geometry=context_[2];
    const bool same=plan && plan->from.x==from.x && plan->from.y==from.y && plan->range==range &&
        plan->origin.x==origin.x && plan->origin.y==origin.y &&
        plan->restricted==restricted && plan->rooms==rooms && plan->candidates.size()==candidates.size() &&
        std::equal(plan->candidates.begin(),plan->candidates.end(),candidates.begin(),[](const auto &left,const auto &right) {
            return left.point.x==right.point.x && left.point.y==right.point.y && left.weight==right.weight &&
                left.gain==right.gain && left.penalty==right.penalty;
        });
    if(!same) plan.reset();
    if(plan && plan->result && plan->snapshot!=currentSnapshot && plan->result->found &&
        navigationRouteValid(terrain,rooms,plan->result->route,range,restricted)) {
        plan->snapshot=currentSnapshot;plan->geometry=context_[2];
    }
    if(plan && plan->job) {
        if(!plan->job->ready()) defer(true);
        plan->result=plan->job->get();plan->job.reset();
        if(plan->result->route.outcome==NavigationOutcome::ResourceLimited)
            throw std::runtime_error("Navigation candidate search exceeded memory budget; result is not a no-route proof");
        if(plan->result->route.outcome==NavigationOutcome::Cancelled) {plan.reset();defer(true);}
        if(plan->snapshot==currentSnapshot || (plan->result->found &&
            navigationRouteValid(terrain,rooms,plan->result->route,range,restricted))) {
            plan->snapshot=currentSnapshot;plan->geometry=context_[2];
            if(plan->result->found) {
                const auto target=plan->result->route.points.back();
                if(range>0) {
                    const std::pair coordinateOrigin{origin.x,origin.y};
                    if(worker_->teleportOrigin!=coordinateOrigin) {worker_->teleportPlans.clear();worker_->teleportOrigin=coordinateOrigin;}
                    auto &retained=worker_->teleportPlans[{target.x,target.y,range,restricted}];
                    retained.continuation.reset();retained.prepared.reset();retained.continuationGeometry=0;
                    retained.route=plan->result->route;retained.cursor=0;retained.geometry=context_[2];
                    retained.used=frames_;retained.planned=true;retained.job.reset();retained.landing.reset();
                    retained.snapshot=currentSnapshot;retained.rooms=rooms;retained.continuationSnapshot.reset();
                } else {
                    const Key key{terrain.obstacleRevision,terrain.missileCollisionRevision,bool(terrain.collisionSample),
                        terrain.width,terrain.height,playerMovement.mask,playerMovement.size,false,true,from.x,from.y,target.x,target.y};
                    const auto existing=results_.find(key);
                    if(existing!=results_.end()) points_-=existing->second.path.size();
                    if(plan->result->route.points.size()>262144-points_ || results_.size()>=4096) {
                        results_.clear();reachable_.clear();points_=0;
                    }
                    results_[key]={plan->result->route.points,frames_};points_+=plan->result->route.points.size();
                    travelAnchors_[{target.x,target.y}]={from,Clock::now()};
                }
            }
            return *plan->result;
        }
    }
    if(plan && plan->result && plan->snapshot==currentSnapshot) return *plan->result;
    const auto snapshot=currentSnapshot;
    plan.emplace();plan->from=from;plan->origin=origin;plan->geometry=context_[2];plan->range=range;plan->restricted=restricted;
    plan->candidates=candidates;plan->rooms=rooms;plan->snapshot=snapshot;
    auto capturedCandidates=nlohmann::json::array();
    if(!worker_->capture->directory.empty()) for(const auto &candidate:candidates) capturedCandidates.push_back({{"point",{candidate.point.x,candidate.point.y}},
        {"weight",candidate.weight},{"gain",candidate.gain},{"penalty",candidate.penalty}});
    plan->job=worker_->schedule([snapshot,from,candidates,rooms,range,restricted] {
        return planNavigation(*snapshot,rooms,from,candidates,range,restricted,activeNavigationCancellation);
    },frames_,snapshot,{{"kind","candidate-plan"},{"context",context_},{"from",{from.x,from.y}},
        {"purpose",int(purpose)},{"candidates",std::move(capturedCandidates)},
        {"rooms",worker_->capture->directory.empty()?nlohmann::json::array():captureRooms(rooms)},
        {"coordinateOrigin",{origin.x,origin.y}},{"range",range},{"restricted",restricted}});
    defer(true);
}
DebugNavigationSearch::TravelRoute DebugNavigationSearch::teleportRoute(const Grid &terrain,
        const std::vector<TeleportRoom> &rooms,Vec from,Vec to,int range,bool restricted,Vec coordinateOrigin) {
    if(!worker_) worker_=std::make_unique<Worker>();
    const std::pair origin{coordinateOrigin.x,coordinateOrigin.y};
    if(worker_->teleportOrigin!=origin) {
        if(!worker_->capture->directory.empty()) {
            auto requests=nlohmann::json::array();
            for(const auto &[target,retained]:worker_->teleportPlans) {
                (void)target;
                if(retained.job) requests.push_back(retained.job->trace->state->id);
                if(retained.continuation) requests.push_back(retained.continuation->trace->state->id);
            }
            worker_->capture->trace({{"event","teleport-cache-cleared"},{"reason","coordinate-origin-changed"},
                {"context",context_},{"coordinateOrigin",{coordinateOrigin.x,coordinateOrigin.y}},
                {"plans",worker_->teleportPlans.size()},{"discardedRequests",std::move(requests)}});
        }
        worker_->teleportPlans.clear();
        worker_->teleportOrigin=origin;
    }
    // Consume completed jobs even when their caller is temporarily suspended;
    // the task closure otherwise retains an obsolete graph and mask snapshot.
    for(auto &entry:worker_->teleportPlans) {
        auto &pending=entry.second;
        if(pending.job && pending.job->ready()) {
            pending.route=pending.job->get();pending.job.reset();pending.cursor=0;pending.planned=true;pending.landing.reset();
            if(pending.route.outcome==NavigationOutcome::ResourceLimited)
                throw std::runtime_error("Teleport search exceeded memory budget; result is not a no-route proof");
            if(pending.route.outcome==NavigationOutcome::Cancelled) pending.planned=false;
        }
    }
    const Worker::TeleportKey key{to.x,to.y,range,restricted};
    if(!worker_->teleportPlans.contains(key) && worker_->teleportPlans.size()>=32) {
        const auto oldest=std::min_element(worker_->teleportPlans.begin(),worker_->teleportPlans.end(),
            [](const auto &a,const auto &b){return a.second.used<b.second.used;});
        worker_->capture->trace({{"event","teleport-plan-evicted"},{"reason","capacity"},{"context",context_},
            {"target",{std::get<0>(oldest->first),std::get<1>(oldest->first)}},
            {"coordinateOrigin",{coordinateOrigin.x,coordinateOrigin.y}},
            {"request",oldest->second.job?oldest->second.job->trace->state->id:0},
            {"continuationRequest",oldest->second.continuation?oldest->second.continuation->trace->state->id:0}});
        worker_->teleportPlans.erase(oldest);
    }
    auto &plan=worker_->teleportPlans[key];plan.used=frames_;
    const auto currentSnapshot=worker_->freeze(terrain);
    const bool domainChanged=plan.snapshot!=currentSnapshot || plan.rooms!=rooms;
    nlohmann::json replanEvidence;
    if(!worker_->capture->directory.empty()) replanEvidence={
        {"reason",!plan.snapshot?"new-target":domainChanged?"domain-changed":"start-changed-or-route-exhausted"},
        {"snapshotChanged",plan.snapshot!=currentSnapshot},{"roomsChanged",plan.rooms!=rooms},
        {"previousOrigin",{plan.route.origin.x,plan.route.origin.y}},
        {"previousOutcome",int(plan.route.outcome)},{"previousCursor",plan.cursor},
        {"previousPoints",plan.route.points.size()},{"previousGeometry",plan.geometry}};
    const auto transition=[&](const char *reason) {
        if(worker_->capture->directory.empty()) return;
        replanEvidence["reason"]=reason;
        worker_->capture->trace({{"event","teleport-route-transition"},{"reason",reason},{"context",context_},
            {"from",{from.x,from.y}},{"to",{to.x,to.y}},{"coordinateOrigin",{coordinateOrigin.x,coordinateOrigin.y}},
            {"cursor",plan.cursor},{"points",plan.route.points.size()},{"outcome",int(plan.route.outcome)},
            {"request",plan.job?plan.job->trace->state->id:0},
            {"continuationRequest",plan.continuation?plan.continuation->trace->state->id:0}});
    };
    if(plan.continuation && plan.continuation->ready()) {
        plan.prepared=plan.continuation->get();plan.continuation.reset();
        if(plan.prepared->outcome==NavigationOutcome::ResourceLimited)
            throw std::runtime_error("Navigation continuation exceeded memory budget");
        if(plan.prepared->outcome==NavigationOutcome::Cancelled) plan.prepared.reset();
    }
    if(plan.job) {
        if(!plan.job->ready()) defer(true);
        plan.route=plan.job->get();plan.job.reset();plan.cursor=0;plan.planned=true;plan.landing.reset();
    }
    if(domainChanged && !plan.route.points.empty()) {
        const auto roomAt=[&](Vec at) {
            for(size_t index=0;index<rooms.size();++index) {
                const auto &room=rooms[index];
                if(at.x>=room.x && at.y>=room.y && at.x<room.x+room.width && at.y<room.y+room.height) return index;
            }
            return rooms.size();
        };
        bool valid=true;Vec previous=plan.cursor?plan.route.points[plan.cursor-1]:plan.route.origin;
        for(size_t index=plan.cursor;index<plan.route.points.size();++index) {
            const auto next=plan.route.points[index];const auto source=roomAt(previous),destination=roomAt(next);
            if(source>=rooms.size() || destination>=rooms.size() || !terrain.walkable(next,playerMovement) ||
                (next-previous).length()>range || (source!=destination &&
                std::find(rooms[source].near.begin(),rooms[source].near.end(),destination)==rooms[source].near.end()) ||
                (restricted && !terrain.interactionSegment(previous-Vec{.5f,.5f},next-Vec{.5f,.5f},2,{},0x0804,2))) {
                valid=false;break;
            }
            previous=next;
        }
        if(!valid) {
            transition("suffix-validation-failed");
            plan.route={};plan.cursor=0;plan.planned=false;plan.landing.reset();plan.continuation.reset();plan.prepared.reset();
        }
        else {transition("suffix-revalidated");plan.geometry=context_[2];plan.snapshot=currentSnapshot;plan.rooms=rooms;}
    }
    // Only actual feet at a retained waypoint consume it. Merely selecting
    // a cast (or moving sideways in combat) cannot discard the suffix.
    for(size_t i=plan.route.points.size();i>plan.cursor;--i)
        if((from-plan.route.points[i-1]).length()<=2) {plan.cursor=i;break;}
    if(plan.cursor==plan.route.points.size() && plan.prepared) {
        if(plan.continuationSnapshot==currentSnapshot && !domainChanged && (from-plan.prepared->origin).length()<=2) {
            transition("continuation-adopted");
            plan.route=std::move(*plan.prepared);plan.cursor=0;plan.geometry=plan.continuationGeometry;
            plan.planned=true;plan.landing.reset();
        }
        else {
            transition(domainChanged || plan.continuationSnapshot!=currentSnapshot?"continuation-domain-changed":"continuation-origin-mismatch");
            if(domainChanged || plan.continuationSnapshot!=currentSnapshot) plan.planned=false;
        }
        plan.prepared.reset();
    }
    if(plan.cursor==plan.route.points.size() && !plan.route.points.empty() &&
        (plan.route.points.back()-to).length()<1 && (from-to).length()>2) {
        // A completed/adjusted arrival is not a reason to replay the old
        // prefix when walking or combat subsequently moves away from it.
        transition("left-completed-target");
        plan.route={};plan.planned=false;plan.landing.reset();
    }
    if(plan.cursor<plan.route.points.size()) {
        if(plan.route.outcome==NavigationOutcome::Partial && plan.route.points.size()-plan.cursor<=2 &&
            !plan.continuation && !plan.prepared && plan.continuationSnapshot!=currentSnapshot) {
            const auto snapshot=currentSnapshot;const auto start=plan.route.points.back();
            plan.continuationGeometry=context_[2];
            plan.continuationSnapshot=snapshot;
            plan.continuation=worker_->schedule([snapshot,rooms,range,restricted,start,to] {
                const std::array targets{NavigationTarget{to,1,1}};
                return planNavigation(*snapshot,rooms,start,targets,range,restricted,activeNavigationCancellation,true).route;
            },frames_,snapshot,{{"kind","teleport-route"},{"context",context_},{"rooms",captureRooms(rooms)},
                {"range",range},{"restricted",restricted},{"coordinateOrigin",{coordinateOrigin.x,coordinateOrigin.y}},
                {"from",{start.x,start.y}},{"to",{to.x,to.y}},{"partial",true},{"continuation",true},
                {"replan",{{"reason","partial-tail-preparation"},{"remainingPoints",plan.route.points.size()-plan.cursor}}}});
        }
        return {plan.route.origin,std::deque<Vec>(plan.route.points.begin()+plan.cursor,plan.route.points.end()),plan.route.outcome};
    }
    if(plan.continuation) defer(true);
    const auto exhaustedAt=plan.route.points.empty()?plan.route.origin:plan.route.points.back();
    if(plan.planned && !domainChanged && (exhaustedAt-from).length()<2)
        return {from,{},plan.route.outcome};
    const auto snapshot=currentSnapshot;
    plan.geometry=context_[2];
    plan.snapshot=snapshot;plan.rooms=rooms;
    plan.job=worker_->schedule([snapshot,rooms,range,restricted,from,to] {
        const std::array targets{NavigationTarget{to,1,1}};
        return planNavigation(*snapshot,rooms,from,targets,range,restricted,activeNavigationCancellation,true).route;
    },frames_,snapshot,
        {{"kind","teleport-route"},{"context",context_},{"rooms",captureRooms(rooms)},
            {"range",range},{"restricted",restricted},{"coordinateOrigin",{coordinateOrigin.x,coordinateOrigin.y}},
            {"from",{from.x,from.y}},{"to",{to.x,to.y}},{"partial",true},{"replan",std::move(replanEvidence)}});
    defer(true);
}
void DebugNavigationSearch::discardTeleportRoute(Vec to,int range,bool restricted) {
    if(!worker_) return;
    const auto found=worker_->teleportPlans.find({to.x,to.y,range,restricted});
    if(found==worker_->teleportPlans.end()) return;
    worker_->capture->trace({{"event","teleport-plan-evicted"},{"reason","explicit-discard"},{"context",context_},
        {"target",{to.x,to.y}},{"range",range},{"restricted",restricted},
        {"request",found->second.job?found->second.job->trace->state->id:0},
        {"continuationRequest",found->second.continuation?found->second.continuation->trace->state->id:0}});
    worker_->teleportPlans.erase(found);
}
void DebugNavigationSearch::rememberTeleportLanding(Vec to,int range,bool restricted,Vec waypoint,Vec landing) {
    if(!worker_) return;
    const auto found=worker_->teleportPlans.find({to.x,to.y,range,restricted});
    if(found!=worker_->teleportPlans.end() && !found->second.job)
        found->second.landing=Worker::TeleportPlan::Landing{waypoint,landing};
}
void DebugNavigationSearch::confirmTeleportLanding(Vec requested,Vec arrived) {
    if(!worker_ || (requested-arrived).length()>4) return;
    // Parent staging and the mobility child can address different goals.
    // Match their actual accepted landing, retaining each destination's suffix.
    for(auto &[key,plan]:worker_->teleportPlans) {
        if(plan.job || !plan.planned) continue;
        const Vec goal{std::get<0>(key),std::get<1>(key)};
        if((requested-goal).length()<.1f) {
            plan.cursor=plan.route.points.size();plan.landing.reset();continue;
        }
        if(!plan.landing || (plan.landing->requested-requested).length()>=.1f) continue;
        for(size_t i=plan.cursor;i<plan.route.points.size();++i)
            if((plan.route.points[i]-plan.landing->waypoint).length()<.1f) {plan.cursor=i+1;break;}
        plan.landing.reset();
    }
}
DebugNavigationSearch::TravelRoute DebugNavigationSearch::travelRoute(const Grid &terrain,Vec from,Vec to,bool partial,std::source_location caller) {
    // Long routes are addressed by destination in the current terrain domain.
    // A tactical displacement does not cancel a queued job or change its key.
    const std::pair target{to.x,to.y};
    std::optional<std::pair<int,int>> origin;
    std::shared_ptr<const NavigationSnapshot> currentSnapshot;
    if(terrain.freezeMovement) {
        currentSnapshot=terrain.freezeMovement();
        if(currentSnapshot) origin=std::pair{currentSnapshot->originX,currentSnapshot->originY};
    }
    if(auto found=walkingRoutes_.find(target);found!=walkingRoutes_.end()) {
        auto &retained=found->second;
        if(retained.width==terrain.width && retained.height==terrain.height && retained.origin==origin &&
            retained.route.outcome==NavigationOutcome::Complete &&
            ((currentSnapshot && retained.snapshot && unchangedRoute(*retained.snapshot,*currentSnapshot,retained.route)) ||
                navigationRouteValid(terrain,{},retained.route,0,false))) {
            retained.geometry=context_[2];retained.snapshot=currentSnapshot;retained.used=frames_;return retained.route;
        }
        walkingRoutes_.erase(found);
    }
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
    const auto outcome=route.empty()?NavigationOutcome::NoRoute:
        (route.back()-to).length()<.01f?NavigationOutcome::Complete:NavigationOutcome::Partial;
    TravelRoute result{anchor.from,std::move(route),outcome};
    if(result.outcome==NavigationOutcome::Complete) {
        if(walkingRoutes_.size()>=64) {
            const auto oldest=std::min_element(walkingRoutes_.begin(),walkingRoutes_.end(),
                [](const auto &left,const auto &right){return left.second.used<right.second.used;});
            walkingRoutes_.erase(oldest);
        }
        walkingRoutes_[target]={result,context_[2],frames_,terrain.width,terrain.height,origin,currentSnapshot};
    }
    return result;
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
        worker_->component=job.get();
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
                checkNavigationCancellation();
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
        },frames_,snapshot,{{"kind","component"},{"context",context_},{"from",{from.x,from.y}},
            {"mask",playerMovement.mask},{"size",playerMovement.size}});
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
    {"captures",worker_?worker_->capture->snapshot():nlohmann::json(nullptr)},
        {"roomPlanPending",worker_ && worker_->roomPlan && worker_->roomPlan->job.has_value()},
        {"explorationPlanPending",worker_ && worker_->viewpointPlan && worker_->viewpointPlan->job.has_value()},
        {"backtrackPlanPending",worker_ && worker_->backtrackPlan && worker_->backtrackPlan->job.has_value()},
        {"candidatePlanPending",worker_ && std::any_of(worker_->candidatePlans.begin(),worker_->candidatePlans.end(),
            [](const auto &plan){return plan && plan->job.has_value();})},
        {"planner","exact-multi-target"},{"snapshotStorage","shared-32x32-blocks"},
        {"backgroundWorker",bool(worker_)},{"pendingPaths",worker_?worker_->paths.size():0},{"componentPending",worker_ && worker_->componentJob.has_value()},{"componentSearches",componentSearches_},{"routeJoins",routeJoins_},{"travelAnchors",travelAnchors_.size()},{"frameQueries",frameQueries_},{"frameCacheHits",frameHits_},
        {"framePathUs",frameMicros_},{"totalPathUs",totalMicros_},{"maxPathUs",maxMicros_},
        {"lastQuery",{{"file",lastFile_},{"line",lastLine_}}},
        {"slowestQuery",{{"file",slowestFile_},{"line",slowestLine_},{"microseconds",maxMicros_},
            {"from",{slowestFrom_.x,slowestFrom_.y}},{"to",{slowestTo_.x,slowestTo_.y}}}},
        {"cachedQueries",results_.size()},{"cachedPoints",points_},{"reachablePoints",reachable_.size()},{"segmentReuses",segmentReuses_},
        {"yielded",yielded_},{"atomicGridCall",true}};
}
} // namespace d2x
