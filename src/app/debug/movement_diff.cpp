#include "movement_diff.hpp"
#include "contracts/online.hpp"
#include "presentation/remote/remote_scene.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace d2x::debug {
namespace {
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
constexpr size_t sampleLimit=8192, queueLimit=8, fileLimit=64*1024*1024;
constexpr size_t npcLimit=6;
constexpr double npcRadius=16;
Json point(Vec value) {return Json::array({value.x,value.y});}
Json point(const std::optional<Vec> &value) {return value?point(*value):Json(nullptr);}
double distance(Vec first,Vec second) {return std::hypot(double(first.x-second.x),double(first.y-second.y));}
bool finite(Vec value) {return std::isfinite(value.x) && std::isfinite(value.y);}
struct NpcSample {
    server::NpcMovementObservation host;
    std::optional<RemoteScene::MovementObservation> client;
    std::optional<OnlinePoint> reported, destination;
    std::optional<uint16_t> clientClass;
    std::optional<uint8_t> mode;
    uint64_t assignment{}, action{}, positionRevision{};
    bool assigned{};
};
struct Sample {
    double ms{},dt{},error{},displayError{},age{};
    uint64_t frame{},generation{},discontinuity{},request{};
    int area{-1},mode{-1};
    std::optional<uint32_t> secondarySeed;
    std::optional<server::MovementObservation> host;
    std::optional<Vec> path,display,destination;
    std::optional<Vec> next,end;
    std::vector<NpcSample> npcs;
    size_t nearbyNpcs{};
    std::string status;
};
Json encode(const NpcSample &sample) {
    const auto &host=sample.host;
    Json result={{"guid",host.guid},{"class",host.nativeClass},
        {"host",{{"position",point(host.position)},{"goal",point(host.goal)},{"next",point(host.next)},
            {"end",point(host.end)},{"mode",host.mode},{"pathType",host.pathType},{"moving",host.moving},
            {"collisionCell",point(host.collisionCell)},{"collisionMask",host.collisionMask},
            {"collisionPattern",host.collisionPattern}}},{"client",nullptr}};
    if(sample.assigned) {
        const auto wirePoint=[](std::optional<OnlinePoint> value) {
            return value?Json::array({value->x,value->y}):Json(nullptr);
        };
        result["client"]={{"reported",wirePoint(sample.reported)},{"destination",wirePoint(sample.destination)},
            {"assignmentRevision",sample.assignment},{"actionRevision",sample.action},{"positionRevision",sample.positionRevision},
            {"class",sample.clientClass?Json(*sample.clientClass):Json(nullptr)},
            {"mode",sample.mode?Json(*sample.mode):Json(nullptr)}};
        if(const auto &client=sample.client) {
            auto &entry=result["client"];
            entry["position"]=point(client->position);
            entry["positionSource"]=client->predicted?"path":"packet";
            entry["cell"]=Json::array({std::floor(client->position.x+.5f),std::floor(client->position.y+.5f)});
            entry["collisionMask"]=client->collisionMask;
            entry["collisionPattern"]=client->collisionPattern?Json(*client->collisionPattern):Json(nullptr);
            entry["next"]=point(client->next);entry["end"]=point(client->end);
            entry["moving"]=client->predicted?Json(client->moving):Json(nullptr);
            entry["pathType"]=client->pathType?Json(*client->pathType):Json(nullptr);
            entry["plannedFrom"]=point(client->plannedFrom);entry["plannedGoal"]=point(client->plannedGoal);
            entry["plannedEnd"]=point(client->plannedEnd);
            entry["pathResult"]=client->pathResult;entry["collisionReplans"]=client->collisionReplans;
            entry["positionCheckResult"]=client->positionCheckResult;entry["stopApproach"]=client->stopApproach;
            result["positionError"]=distance(client->position,host.position);
        }
    }
    return result;
}
void collectNpcs(Sample &sample,const OnlineView &view,const RemoteScene &scene,
    const server::MovementObservation &host) {
    std::vector<std::pair<double,NpcSample>> nearest;
    const auto distanceToPlayer=[&](Vec position) {
        return sample.path && finite(*sample.path)?std::min(distance(position,host.position),distance(position,*sample.path)):
            distance(position,host.position);
    };
    for(const auto &npc:host.npcs) {
        if(!finite(npc.position)) continue;
        const OnlineUnitKey key{1,npc.guid};
        NpcSample entry;
        entry.client=scene.unitMovementObservation(view,key);
        if(entry.client && !finite(entry.client->position)) entry.client.reset();
        double proximity=distanceToPlayer(npc.position);
        if(entry.client) proximity=std::min(proximity,distanceToPlayer(entry.client->position));
        if(proximity>npcRadius) continue;
        entry.host=npc;
        if(const auto unit=view.world.units.find(key);unit!=view.world.units.end()) {
            entry.assigned=true;
            entry.reported=unit->second.position;
            entry.destination=unit->second.destination;
            entry.clientClass=unit->second.classId;
            entry.mode=unit->second.mode;
            entry.assignment=unit->second.assignmentRevision;
            entry.action=unit->second.actionRevision;
            entry.positionRevision=unit->second.positionRevision;
        }
        nearest.emplace_back(proximity,std::move(entry));
    }
    sample.nearbyNpcs=nearest.size();
    std::sort(nearest.begin(),nearest.end(),[](const auto &a,const auto &b) {
        if(a.first!=b.first) return a.first<b.first;
        return a.second.host.guid<b.second.host.guid;
    });
    for(size_t i=0;i<std::min(nearest.size(),npcLimit);++i)
        sample.npcs.push_back(std::move(nearest[i].second));
}
Json encode(const Sample &sample) {
    Json result={{"ms",sample.ms},{"dtMs",sample.dt},{"frame",sample.frame},{"gameGeneration",sample.generation},
        {"area",sample.area},{"status",sample.status},{"clientMode",sample.mode},
        {"discontinuity",sample.discontinuity},{"requestRevision",sample.request},
        {"secondarySeed",sample.secondarySeed?Json(*sample.secondarySeed):Json(nullptr)},
        {"path",sample.path?point(*sample.path):Json(nullptr)},
        {"display",sample.display?point(*sample.display):Json(nullptr)},
        {"clientDestination",point(sample.destination)},{"clientNext",point(sample.next)},{"clientEnd",point(sample.end)}};
    if(sample.host) {
        const auto &host=*sample.host;
        result["host"]={{"gameSlot",host.game.slot},{"generation",host.game.generation},{"entity",host.actor.value},
            {"guid",host.nativeGuid},{"tick",host.tick},{"area",int(host.area)},{"position",point(host.position)},
            {"destination",point(host.destination)},{"next",point(host.next)},{"end",point(host.end)},
            {"mapSeed",host.mapSeed},{"teleportRevision",host.teleportRevision},
            {"commandSequence",host.commandSequence},{"movementSequence",host.movementSequence},
            {"acceptedSequence",host.commandSequence},{"executedSequence",host.executedSequence},{"simulationPending",host.simulationPending},
            {"pendingPhase",host.pendingPhase},
            {"targetEntity",host.target.value},{"targetType",host.targetType},
            {"moving",host.moving},{"running",host.running},{"alive",host.alive},
            {"knockback",host.knockback},{"paused",host.paused},{"tickAgeMs",sample.age}};
        if(sample.path && sample.display) {
            result["pathError"]=sample.error;result["displayError"]=sample.displayError;
            result["delta"]=point(*sample.path-host.position);
        }
    }
    if(sample.nearbyNpcs) {
        result["nearbyNpcCount"]=sample.nearbyNpcs;
        result["npcs"]=Json::array();
        for(const auto &npc:sample.npcs) result["npcs"].push_back(encode(npc));
    }
    return result;
}
struct Stats {
    uint64_t frames{},valid{},ticks{},longFrames{};
    double elapsed{},covered{},sum{},squares{},maximum{},displayMaximum{},ageMaximum{};
    double above2{},above5{},run2{},run5{},longest2{},longest5{},oneSide{},oneSideRun{},longestOneSide{},areaRun{},longestArea{};
    double clientTravel{},hostTravel{},deltaX{},deltaY{};
    std::map<std::string,uint64_t> skipped;
    std::array<double,1025> histogram{};
    void breakRun() {run2=run5=oneSideRun=areaRun=0;}
    void add(const Sample &sample,const std::optional<Sample> &previous) {
        ++frames;elapsed+=sample.dt;
        if(sample.dt>100) ++longFrames;
        if(sample.status!="valid") {
            ++skipped[sample.status];run2=run5=oneSideRun=0;
            areaRun=sample.status=="area-mismatch" && sample.dt<=100?areaRun+sample.dt:0;
            longestArea=std::max(longestArea,areaRun);return;
        }
        ++valid;areaRun=0;
        maximum=std::max(maximum,sample.error);displayMaximum=std::max(displayMaximum,sample.displayError);
        ageMaximum=std::max(ageMaximum,sample.age);
        if(!previous || previous->status!="valid") {breakRun();return;}
        if(sample.dt>100) {breakRun();return;}
        const double weight=sample.dt;
        covered+=weight;sum+=sample.error*weight;squares+=sample.error*sample.error*weight;
        const auto delta=*sample.path-sample.host->position;
        deltaX+=delta.x*weight;deltaY+=delta.y*weight;
        histogram[size_t(std::min(sample.error*16,double(histogram.size()-1)))]+=weight;
        const auto accumulate=[&](bool over,double &total,double &run,double &longest) {
            if(over) {total+=weight;run+=weight;longest=std::max(longest,run);} else run=0;
        };
        accumulate(sample.error>2,above2,run2,longest2);
        accumulate(sample.error>5,above5,run5,longest5);
        const double client=distance(*sample.path,*previous->path),host=distance(sample.host->position,previous->host->position);
        clientTravel+=client;hostTravel+=host;
        const bool stopped=sample.error>2 && ((client>.01 && !sample.host->moving) ||
            (sample.host->moving && sample.mode>=0 && sample.mode!=2 && sample.mode!=3 && sample.mode!=6));
        accumulate(stopped,oneSide,oneSideRun,longestOneSide);
        if(sample.host->tick!=previous->host->tick) ++ticks;
    }
    bool abnormal() const {return longest2>=500 || longest5>=200 || longestOneSide>=500 || longestArea>=1000;}
    Json json() const {
        double cumulative=0,p95=0;
        for(size_t index=0;index<histogram.size();++index) {
            cumulative+=histogram[index];
            if(covered>0 && cumulative>=covered*.95) {p95=double(index+1)/16;break;}
        }
        return {{"frames",frames},{"validFrames",valid},{"hostTickChanges",ticks},{"elapsedMs",elapsed},
            {"coveredMs",covered},{"coverage",elapsed>0?covered/elapsed:0},{"longFrames",longFrames},{"skipped",skipped},
            {"mean",covered>0?sum/covered:0},{"rms",covered>0?std::sqrt(squares/covered):0},
            {"p95Approx",p95},{"p95Overflow",histogram.back()>covered*.05 && covered>0},
            {"max",maximum},{"displayMax",displayMaximum},{"hostTickAgeMaxMs",ageMaximum},
            {"above2Ms",above2},{"above5Ms",above5},{"longestAbove2Ms",longest2},{"longestAbove5Ms",longest5},
            {"oneSideMs",oneSide},{"longestOneSideMs",longestOneSide},{"longestAreaMismatchMs",longestArea},
            {"clientTravel",clientTravel},{"hostTravel",hostTravel},
            {"meanDelta",Json::array({covered>0?deltaX/covered:0,covered>0?deltaY/covered:0})}};
    }
};
struct Report {Json metadata;std::vector<Sample> samples;};
}
struct MovementDiff::Impl {
    Clock::time_point started=Clock::now(),last=started,tickChanged=started;
    uint64_t frame{},incident{},lastTick{};
    double windowStart{},settleUntil{},postUntil{},lastDump{-1};
    bool haveTick{},postPending{},disabled{};
    Stats window,total;
    std::optional<Sample> previous;
    std::optional<Sample> peak;
    std::deque<Sample> ring;
    std::ofstream stream;
    size_t written{};
    uint64_t dropped{};
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Report> queue;
    bool closing{};
    std::thread writer;
    explicit Impl(const std::string &path) {
        const std::filesystem::path destination(path);
        if(std::filesystem::exists(destination)) throw std::runtime_error("Movement diff log already exists: "+path);
        if(destination.has_parent_path()) std::filesystem::create_directories(destination.parent_path());
        stream.open(destination,std::ios::out|std::ios::binary);
        if(!stream) throw std::runtime_error("Unable to create movement diff log: "+path);
        writer=std::thread([this] {writeLoop();});
        submit({{{"event","header"},{"schema",2},{"units","global subtile feet"},{"intervalMs",5000},
            {"npcRadius",npcRadius},{"npcLimit",npcLimit},
            {"ringMs",30000},{"ringLimit",sampleLimit},{"fileLimitBytes",fileLimit},
            {"thresholds",{{"distance2Ms",500},{"distance5Ms",200},{"oneSideMs",500},{"areaMismatchMs",1000}}}}, {}});
    }
    ~Impl() {
        try {
            flush("shutdown");
            if(postPending) submit({{{"event","followup"},{"incident",incident},{"reason","shutdown"}},samplesSince(lastDump)});
            submit({{{"event","summary"},{"ms",milliseconds(Clock::now())},{"total",total.json()},{"droppedReports",dropped},
                {"peakSample",peak?encode(*peak):Json(nullptr)}}, {}});
        }
        catch(const std::exception &error) {std::cerr<<"[movement-diff] finalization: "<<error.what()<<'\n';}
        {std::lock_guard lock(mutex);closing=true;}
        wake.notify_one();if(writer.joinable()) writer.join();
    }
    double milliseconds(Clock::time_point now) const {return std::chrono::duration<double,std::milli>(now-started).count();}
    void submit(Report report) {
        {std::lock_guard lock(mutex);
            if(queue.size()>=queueLimit) {++dropped;return;}
            queue.push_back(std::move(report));
        }
        wake.notify_one();
    }
    void writeLoop() noexcept {
        try {
            for(;;) {
                Report report;
                {std::unique_lock lock(mutex);wake.wait(lock,[&]{return closing || !queue.empty();});
                    if(queue.empty() && closing) break;
                    report=std::move(queue.front());queue.pop_front();
                }
                const auto line=[&](const Json &value) {
                    auto text=value.dump();text+='\n';
                    if(written+text.size()>fileLimit) return false;
                    stream.write(text.data(),std::streamsize(text.size()));written+=text.size();
                    if(!stream) throw std::runtime_error("Unable to write movement diff log");
                    return true;
                };
                if(!line(report.metadata)) {std::cerr<<"[movement-diff] 64 MiB log limit reached; recording stopped\n";return;}
                for(const auto &sample:report.samples) if(!line({{"event","sample"},{"incident",report.metadata.value("incident",uint64_t{})},{"sample",encode(sample)}})) {
                    std::cerr<<"[movement-diff] 64 MiB log limit reached; recording stopped\n";return;
                }
                stream.flush();if(!stream) throw std::runtime_error("Unable to flush movement diff log");
            }
        } catch(const std::exception &error) {std::cerr<<"[movement-diff] writer: "<<error.what()<<'\n';}
    }
    std::vector<Sample> samplesSince(double since) const {
        std::vector<Sample> result;
        for(const auto &sample:ring) if(sample.ms>since) result.push_back(sample);
        return result;
    }
    void flush(const char *reason) {
        if(!window.frames) return;
        if(window.abnormal()) {
            ++incident;
            submit({{{"event","anomaly"},{"incident",incident},{"reason",reason},{"fromMs",windowStart},
                {"toMs",previous?previous->ms:windowStart},{"window",window.json()},{"total",total.json()},
                {"droppedReports",dropped}},samplesSince(lastDump)});
            lastDump=previous?previous->ms:windowStart;postUntil=lastDump+5000;postPending=true;
        }
        window={};windowStart=previous?previous->ms:windowStart;
    }
    void record(std::optional<server::MovementObservation> observation,const OnlineView &view,
        const RemoteScene *scene,std::optional<int> area,bool suspended) {
        const auto now=Clock::now();Sample sample;
        sample.ms=milliseconds(now);sample.dt=std::chrono::duration<double,std::milli>(now-last).count();last=now;
        sample.frame=++frame;sample.generation=view.gameGeneration;sample.area=area.value_or(-1);
        sample.secondarySeed=view.load.secondarySeed;
        if(scene) {sample.path=scene->playerPathPosition(view);sample.display=scene->playerDisplayPosition(view);
            sample.mode=scene->playerMovementMode(view).value_or(uint8_t{255});}
        if(observation && scene && finite(observation->position) && sample.area==int(observation->area) &&
            view.load.playerUnitId==observation->nativeGuid)
            collectNpcs(sample,view,*scene,*observation);
        if(observation) std::vector<server::NpcMovementObservation>{}.swap(observation->npcs);
        sample.host=std::move(observation);
        const auto &host=sample.host;
        if(view.world.movementRequest) {
            sample.request=view.world.movementRequest->revision;
            if(view.world.movementRequest->destination) sample.destination=Vec{float(view.world.movementRequest->destination->x),float(view.world.movementRequest->destination->y)};
        }
        if(view.load.playerUnitId) if(const auto unit=view.world.units.find({0,*view.load.playerUnitId});unit!=view.world.units.end())
            sample.discontinuity=unit->second.positionDiscontinuity;
        if(scene && sample.path && view.load.playerUnitId) {
            if(const auto movement=scene->unitMovementObservation(view,{0,*view.load.playerUnitId})) {
                sample.next=movement->next;
                sample.end=movement->end;
            }
        }
        const bool boundary=previous && (sample.generation!=previous->generation || bool(host)!=bool(previous->host) ||
            (host && previous->host && (host->game!=previous->host->game || host->actor!=previous->host->actor || host->area!=previous->host->area ||
                host->teleportRevision!=previous->host->teleportRevision || host->alive!=previous->host->alive)) ||
            sample.discontinuity!=previous->discontinuity || sample.area!=previous->area);
        if(boundary) {
            flush("boundary");window.breakRun();total.breakRun();settleUntil=sample.ms+200;
            haveTick=false;
        }
        if(host && (!haveTick || host->tick!=lastTick)) {tickChanged=now;lastTick=host->tick;haveTick=true;}
        sample.age=std::chrono::duration<double,std::milli>(now-tickChanged).count();
        if(!host) sample.status="no-single-player-host";
        else if(suspended || host->paused) sample.status="paused-or-minimized";
        else if(!host->alive) sample.status="dead";
        else if(!view.load.playerUnitId || *view.load.playerUnitId!=host->nativeGuid) sample.status="identity-mismatch";
        else if(!sample.path || !sample.display) sample.status="no-client-position";
        else if(!finite(*sample.path) || !finite(*sample.display) || !finite(host->position)) sample.status="nonfinite";
        else if(sample.area!=int(host->area)) sample.status="area-mismatch";
        else if(sample.ms<settleUntil) sample.status="transition";
        else sample.status="valid";
        if(host && sample.path && sample.display) {sample.error=distance(*sample.path,host->position);sample.displayError=distance(*sample.display,host->position);}
        window.add(sample,boundary?std::nullopt:previous);total.add(sample,boundary?std::nullopt:previous);
        if(sample.status=="valid" && (!peak || sample.error>peak->error)) peak=sample;
        previous=sample;ring.push_back(std::move(sample));
        while(ring.size()>sampleLimit || (!ring.empty() && ring.front().ms<previous->ms-30000)) ring.pop_front();
        if(postPending && previous->ms>=postUntil) {
            submit({{{"event","followup"},{"incident",incident},{"toMs",previous->ms}},samplesSince(lastDump)});
            lastDump=previous->ms;postPending=false;
        }
        if(previous->ms-windowStart>=5000) flush("five-second-window");
    }
};
MovementDiff::MovementDiff(const std::string &path) {if(!path.empty()) impl_=std::make_unique<Impl>(path);}
MovementDiff::~MovementDiff()=default;
void MovementDiff::record(std::optional<server::MovementObservation> host,const OnlineView &view,
    const RemoteScene *scene,std::optional<int> area,bool suspended) noexcept {
    if(!impl_ || impl_->disabled) return;
    try {impl_->record(std::move(host),view,scene,area,suspended);}
    catch(const std::exception &error) {impl_->disabled=true;std::cerr<<"[movement-diff] recording stopped: "<<error.what()<<'\n';}
}
}
