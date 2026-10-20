#include "headless.hpp"
#include "options.hpp"
#include "debug/debug_pipe.hpp"
#include "debug/server_commands.hpp"
#include "hosting/embedded_realm.hpp"
#include "network/realm_session.hpp"
#include "network/protocol/tcpip.hpp"
#include "persistence/d2s_header.hpp"
#include "resources/archive.hpp"
#include "resources/atomic_file.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace d2x {
namespace {
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
volatile std::sig_atomic_t interrupted{};
void stopSignal(int) { interrupted=1; }
int integer(const Json &j,const char *key,int fallback,int minimum,int maximum) {
    const auto value=j.contains(key)?j.at(key):Json(fallback);
    if(!value.is_number_integer()) throw std::invalid_argument(std::string(key)+" must be an integer");
    const auto number=value.get<int64_t>();
    if(number<minimum || number>maximum) throw std::invalid_argument(std::string(key)+" outside allowed range");
    return int(number);
}
std::filesystem::path pathAt(const Json &j,const char *key,const std::filesystem::path &base) {
    auto path=std::filesystem::path(j.at(key).get<std::string>());
    if(path.empty()) throw std::invalid_argument(std::string("Empty path: ")+key);
    return std::filesystem::absolute(path.is_absolute()?path:base/path).lexically_normal();
}
std::string folded(std::string text) {
    for(auto &c:text) if(c>='A' && c<='Z') c+='a'-'A';
    return text;
}
struct Action {
    std::string kind;
    int duration{},interval{};
    uint16_t skill{};
    int x{},y{};
    bool absolute{};
};
struct Agent {
    net::RealmSession session;
    Bytes input;
    D2sHeader header;
    std::filesystem::path source,destination;
    std::vector<Action> actions;
    std::optional<uint16_t> area;
    std::optional<OnlinePoint> anchor,previous;
    uint64_t areaGeneration{},casts{},moves{},selects{},refused{},positionChanges{},skillEvents{},lastEvent{},saves{};
    double distance{};
    std::map<uint16_t,uint64_t> castsBySkill,observedSkillEvents;
    int cycleMs{};
    int64_t nextInput{};
    size_t activeAction=SIZE_MAX;
    uint64_t lateInputs{};
    int64_t maximumInputDelayMs{};
    std::string unavailable;
    bool selected{},created{},entered{},left{},connected{};
    void observe() {
        session.tick();
        const auto &view=session.read();
        if(areaGeneration!=view.world.areaGeneration) {
            areaGeneration=view.world.areaGeneration; previous.reset(); activeAction=SIZE_MAX;
        }
        if(view.world.playerPosition) {
            if(previous && *previous!=*view.world.playerPosition) {
                ++positionChanges;
                distance+=std::hypot(int(previous->x)-int(view.world.playerPosition->x),int(previous->y)-int(view.world.playerPosition->y));
            }
            previous=view.world.playerPosition;
        }
        for(const auto &event:view.world.combatEvents) if(event.sequence>lastEvent) {
            if(event.skill) ++observedSkillEvents[*event.skill];
            if(event.source.type==0 && event.source.id==view.load.playerUnitId && event.skill) ++skillEvents;
            lastEvent=event.sequence;
        }
        for(auto &bytes:session.take_tcpip_saves()) {
            const auto returned=readD2sHeader(bytes);
            if(returned.name!=header.name || returned.characterClass!=header.characterClass)
                throw std::runtime_error("TCP/IP returned another character; retained output was not replaced");
            writeFileAtomically(destination,bytes,true); ++saves;
        }
        entered|=view.stage==OnlineStage::ProtocolReady;
    }
    void resetMeasurements() {
        casts=moves=selects=refused=positionChanges=skillEvents=0;distance=0;
        castsBySkill.clear();observedSkillEvents.clear();
        lateInputs=0;maximumInputDelayMs=0;
        previous=session.read().world.playerPosition;
        activeAction=SIZE_MAX;nextInput=0;
    }
    void drive(int64_t elapsed) {
        const auto &view=session.read();
        unavailable.clear();
        if(view.stage!=OnlineStage::ProtocolReady || !view.world.playerPosition || onlinePlayerDead(view.world)) {
            unavailable="Player not ready/alive"; return;
        }
        if(area && std::none_of(view.world.rooms.begin(),view.world.rooms.end(),[&](const auto &room){return std::get<0>(room.first)==*area;})) {
            unavailable="Waiting for assigned room level"; return;
        }
        if(actions.empty() || elapsed<0) return;
        if(!anchor) anchor=view.world.playerPosition;
        auto offset=elapsed%cycleMs; size_t index{};
        for(;index+1<actions.size() && offset>=actions[index].duration;++index) offset-=actions[index].duration;
        if(index!=activeAction) { activeAction=index; nextInput=elapsed-offset; }
        const auto &action=actions[index];
        if(action.kind=="wait" || elapsed<nextInput) return;
        const auto scheduled=nextInput;
        maximumInputDelayMs=std::max(maximumInputDelayMs,elapsed-scheduled);
        if(elapsed-scheduled>=action.interval) ++lateInputs;
        // Keep the original epoch/phase cadence; skip rather than replay debt.
        nextInput=elapsed-offset+(offset/action.interval+1)*action.interval;
        const auto base=action.kind=="move"?*anchor:*view.world.playerPosition;
        const int x=(action.absolute?0:int(base.x))+action.x,y=(action.absolute?0:int(base.y))+action.y;
        if(x<0 || y<0 || x>65535 || y>65535 ||
           std::abs(x-int(view.world.playerPosition->x))>50 || std::abs(y-int(view.world.playerPosition->y))>50) {
            unavailable="Configured target outside native request range"; return;
        }
        const OnlinePoint point{uint16_t(x),uint16_t(y)};
        if(action.kind=="move") {
            if(point!=*view.world.playerPosition) { if(session.move_to(point)) ++moves; else ++refused; }
            return;
        }
        const auto rank=view.world.playerSkills.find(action.skill);
        if(rank==view.world.playerSkills.end() || !rank->second) { unavailable="Configured skill not observed as learned"; return; }
        OnlineCombatCommand command; command.hand=OnlineSkillHand::Right; command.skill=action.skill;
        if(!view.world.rightSkill || view.world.rightSkill->skill!=action.skill || view.world.rightSkill->owner!=UINT32_MAX) {
            command.action=OnlineCombatCommand::Action::SelectSkill;
            if(session.submit_combat(command)) ++selects; else ++refused;
        } else {
            command.action=OnlineCombatCommand::Action::Cast; command.point=point; command.stationary=true;
            if(session.submit_combat(command)) {++casts;++castsBySkill[action.skill];} else ++refused;
        }
    }
    Json status() const {
        const auto &view=session.read();
        Json result{{"name",header.name},{"class",header.characterClass},{"stage",int(view.stage)},
            {"ready",view.stage==OnlineStage::ProtocolReady},{"castsSubmitted",casts},{"movesSubmitted",moves},
            {"skillSelections",selects},{"locallyRefused",refused},{"ownSkillEventsObserved",skillEvents},
            {"missedInputIntervals",lateInputs},{"maximumInputDelayMs",maximumInputDelayMs},
            {"action",activeAction<actions.size()?Json(actions[activeAction].kind):Json(nullptr)},
            {"actionIndex",activeAction<actions.size()?Json(activeAction):Json(nullptr)},
            {"positionChanges",positionChanges},{"observedDistance",distance},{"returnedSaves",saves},
            {"receivedBytes",view.gameProtocol.receivedBytes},{"sentBytes",view.gameProtocol.sentBytes},
            {"unavailable",unavailable},{"dead",onlinePlayerDead(view.world)},
            {"requiredRoomLevel",area?Json(*area):Json(nullptr)},
            {"life",view.world.life?Json(*view.world.life):Json(nullptr)},
            {"mana",view.world.mana?Json(*view.world.mana):Json(nullptr)},
            {"position",view.world.playerPosition?Json{{"x",view.world.playerPosition->x},{"y",view.world.playerPosition->y}}:Json(nullptr)},
            {"error",view.error?Json(view.error->message):Json(nullptr)}};
        result["castsBySkill"]=Json::object();result["observedSkillEvents"]=Json::object();
        for(const auto &[skill,count]:castsBySkill) result["castsBySkill"][std::to_string(skill)]=count;
        for(const auto &[skill,count]:observedSkillEvents) result["observedSkillEvents"][std::to_string(skill)]=count;
        result["waypointSource"]=view.world.waypointSource?Json(*view.world.waypointSource):Json(nullptr);
        result["act"]=view.load.act?Json(*view.load.act):Json(nullptr);
        result["assignedLevels"]=Json::array();
        std::set<uint8_t> levels;for(const auto &[key,point]:view.world.rooms) {(void)point;levels.insert(std::get<0>(key));}
        for(const auto level:levels) result["assignedLevels"].push_back(level);
        return result;
    }
};
Json timing(std::vector<double> values) {
    if(values.empty()) return Json{{"samples",0}};
    std::sort(values.begin(),values.end()); double total{};for(auto value:values) total+=value;
    const auto quantile=[&](double fraction){return values[size_t(std::ceil(fraction*values.size()))-1];};
    return Json{{"samples",values.size()},{"meanMs",total/values.size()},{"p50Ms",quantile(.5)},
        {"p95Ms",quantile(.95)},{"p99Ms",quantile(.99)},{"maxMs",values.back()}};
}
}
int runHeadless(const AppOptions &options) {
    const auto profilePath=std::filesystem::absolute(options.headlessProfile);
    std::ifstream profileInput(profilePath);
    if(!profileInput) throw std::runtime_error("Cannot open headless profile");
    const auto profile=Json::parse(profileInput);
    if(integer(profile,"version",1,1,1)!=1) throw std::invalid_argument("Unsupported headless profile");
    const auto mode=profile.at("mode").get<std::string>();
    const bool hosting=mode=="host";
    if(!hosting && mode!="clients") throw std::invalid_argument("mode must be host or clients");
    const int pollMs=integer(profile,"pollMs",hosting?10:20,5,100);
    const int duration=integer(profile,"durationSeconds",60,1,3600);
    const int connectTimeout=integer(profile,"connectTimeoutSeconds",60,5,300);
    const int refillSeconds=integer(profile,"refillSeconds",0,0,3600);
    if(refillSeconds && !hosting) throw std::invalid_argument("refillSeconds requires the local host mode");
    const int port=integer(profile,"port",4000,1,65535);
    const auto address=profile.value("address",std::string("127.0.0.1"));
    const auto output=pathAt(profile,"output",profilePath.parent_path());
    const auto &clients=profile.at("clients");
    if(!clients.is_array() || clients.empty() || clients.size()>8 || (hosting && clients.size()!=1))
        throw std::invalid_argument("clients requires 1..8 entries; host requires exactly one owner");
    std::vector<std::unique_ptr<Agent>> agents;
    std::set<std::string> names;
    for(const auto &config:clients) {
        auto agent=std::make_unique<Agent>();
        agent->source=pathAt(config,"save",profilePath.parent_path());
        const auto size=std::filesystem::file_size(agent->source);
        if(size<d2sHeaderSize || size>=net::protocol::tcpIpSaveLimit) throw std::invalid_argument("Character outside native TCP/IP save size");
        agent->input=readFile(agent->source); agent->header=readD2sHeader(agent->input);
        if(agent->input.size()>=net::protocol::tcpIpSaveLimit) throw std::invalid_argument("Character changed size during read");
        if(!names.insert(folded(agent->header.name)).second) throw std::invalid_argument("Duplicate character name");
        agent->destination=output/"saves"/(agent->header.name+".d2s");
        if(config.contains("area")) agent->area=uint16_t(integer(config,"area",1,1,136));
        const auto readPoint=[](const Json &value,int minimum,int maximum){
            if(!value.is_array() || value.size()!=2) throw std::invalid_argument("Point needs two integer coordinates");
            std::array<int,2> point{};
            for(size_t i=0;i<2;++i) {
                if(!value[i].is_number_integer()) throw std::invalid_argument("Point must use integers");
                const auto coordinate=value[i].get<int64_t>();
                if(coordinate<minimum || coordinate>maximum) throw std::invalid_argument("Point outside allowed range");
                point[i]=int(coordinate);
            }
            return point;
        };
        if(config.contains("anchor")) {const auto p=readPoint(config.at("anchor"),0,65535);agent->anchor=OnlinePoint{uint16_t(p[0]),uint16_t(p[1])};}
        if(config.contains("cycle")) {
            const auto &cycle=config.at("cycle");
            if(!cycle.is_array() || cycle.size()>64) throw std::invalid_argument("cycle must contain at most 64 actions");
            for(const auto &step:cycle) {
                Action action; action.kind=step.at("kind").get<std::string>();
                if(action.kind!="wait" && action.kind!="move" && action.kind!="cast") throw std::invalid_argument("Unknown headless action");
                action.duration=integer(step,"durationMs",1000,100,60000);
                action.interval=integer(step,"intervalMs",250,100,60000);
                if(action.kind=="cast") action.skill=uint16_t(integer(step,"skill",0,0,65535));
                if(action.kind!="wait") {
                    action.absolute=step.contains("point");
                    if(action.absolute && step.contains("offset")) throw std::invalid_argument("Choose point or offset");
                    const auto point=readPoint(step.at(action.absolute?"point":"offset"),action.absolute?0:-50,action.absolute?65535:50);
                    action.x=point[0];action.y=point[1];
                }
                agent->cycleMs+=action.duration; agent->actions.push_back(action);
            }
        }
        agents.push_back(std::move(agent));
    }
    // Every output is isolated. Original input files are only read, never leased or overwritten.
    std::filesystem::create_directories(output.parent_path());
    if(!std::filesystem::create_directory(output)) throw std::invalid_argument("Headless output must be a new directory");
    std::filesystem::create_directory(output/"saves");
    for(const auto &agent:agents) writeFileAtomically(agent->destination,agent->input);
    { std::ofstream savedProfile(output/"profile.json");savedProfile.exceptions(std::ios::badbit|std::ios::failbit);savedProfile<<profile.dump(2); }
    std::ofstream samples(output/"samples.jsonl");samples.exceptions(std::ios::badbit|std::ios::failbit);
    Archives archives;
    std::unique_ptr<EmbeddedRealm> host;
    std::string ownerName;
    if(hosting) {
        archives.mountDirectory(options.mpq);
        if(archives.names.empty()) throw std::runtime_error("No original MPQ content mounted for host");
        host=std::make_unique<EmbeddedRealm>(archives,output/"saves");
        ownerName=host->prepareStartup(agents.front()->destination.string(),{},{});
        host->listen(address,uint16_t(port)); host->setTcpIpHost(true);
        auto transports=host->connect();
        agents.front()->session.connect_realm(std::move(transports.realm),std::move(transports.game),{"127.0.0.1",6113},"Headless",uint16_t(port));
    }
    const auto connectClient=[&](Agent &agent) {
        OnlineCharacter identity;identity.name=agent.header.name;identity.characterClass=agent.header.characterClass;
        identity.level=agent.header.level;
        agent.session.connect_tcpip({address,uint16_t(port)},agent.input,std::move(identity));agent.connected=true;
    };
    const bool serialAdmission=profile.value("serialAdmission",false);
    if(!hosting) for(size_t i=0;i<agents.size();++i) {if(!serialAdmission || !i) connectClient(*agents[i]);}
    DebugPipe pipe(options.debugPipe);
    std::signal(SIGINT,stopSignal);std::signal(SIGTERM,stopSignal);interrupted=0;
    bool stopping{},failed{},started{};
    std::string failure;
    auto begin=Clock::now(),last=begin,nextSample=begin;
    std::optional<Clock::time_point> start,leaveDeadline;
    std::vector<double> pumpTimes;
    Json lastTiming=timing({});
    uint64_t refillApplied{},refillFailed{};
    double refillMilliseconds{};
    int64_t nextRefill{};
    bool resumedOnStart{};
    std::vector<Json> pendingInputs;
    const auto submitInput=[&](const Json &request) {
        const auto action=request.at("action").get<std::string>();Json outcomes=Json::array();
        for(auto &agent:agents) {
            bool accepted{};
            if(action=="waypoint-open") accepted=agent->session.interact_map_unit({2,uint32_t(integer(request,"unitId",0,1,INT32_MAX))},OnlineObjectIntent::Waypoint);
            else if(action=="waypoint-travel") accepted=agent->session.use_waypoint(uint16_t(integer(request,"level",0,1,136)),uint8_t(integer(request,"waypointNumber",0,0,255)));
            else if(action=="select-profile-skill") {
                const auto first=std::find_if(agent->actions.begin(),agent->actions.end(),[](const auto &value){return value.kind=="cast";});
                if(first!=agent->actions.end()) {OnlineCombatCommand command;command.action=OnlineCombatCommand::Action::SelectSkill;command.skill=first->skill;accepted=agent->session.submit_combat(command);}
            } else if(action=="stop") {OnlineCombatCommand command;command.action=OnlineCombatCommand::Action::Stop;accepted=agent->session.submit_combat(command);}
            else throw std::invalid_argument("Unknown headless input action");
            outcomes.push_back({{"name",agent->header.name},{"accepted",accepted}});
        }
        return Json{{"ok",true},{"outcomes",outcomes}};
    };
    struct TimedStep { int64_t epoch; hosting::StepTiming value; };
    struct TimedPump { int64_t epoch; hosting::PumpTiming value; };
    std::vector<TimedStep> stepTimings;
    std::vector<TimedPump> detailedPumps;
    std::ofstream stepsFile,pumpsFile;
    const auto epochMs=[] {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();};
    const bool detailed=hosting && profile.value("detailedTiming",false);
    if(detailed) {
        stepsFile.open(output/"ticks.tsv");pumpsFile.open(output/"host-phases.tsv");
        stepsFile.exceptions(std::ios::badbit|std::ios::failbit);pumpsFile.exceptions(std::ios::badbit|std::ios::failbit);
        stepsFile<<"epochMs\tbefore\tafter\tnanoseconds";
        for(const auto &entry:server::systemCatalog()) stepsFile<<'\t'<<entry.name;
        stepsFile<<'\n';pumpsFile<<"epochMs\tsimulationNs\tpreparationNs\teventsNs\tpublicationNs\n";
        host->observePerformance({[&](const auto &value){stepTimings.push_back({epochMs(),value});},
            [&](const auto &value){detailedPumps.push_back({epochMs(),value});}});
    }
    const auto flushTimings=[&] {
        if(!detailed) return;
        for(const auto &entry:stepTimings) {
            stepsFile<<entry.epoch<<'\t'<<entry.value.before<<'\t'<<entry.value.after<<'\t'<<entry.value.nanoseconds;
            for(const auto &stage:entry.value.stages) stepsFile<<'\t'<<stage.nanoseconds;
            stepsFile<<'\n';
        }
        for(const auto &entry:detailedPumps) pumpsFile<<entry.epoch<<'\t'<<entry.value.simulation<<'\t'<<entry.value.preparation<<'\t'<<entry.value.events<<'\t'<<entry.value.publication<<'\n';
        stepTimings.clear();detailedPumps.clear();stepsFile.flush();pumpsFile.flush();
    };
    const auto status=[&] {
        Json result{{"ok",true},{"mode",mode},{"running",started && !stopping},{"stopping",stopping},
            {"elapsedSeconds",start?std::max(0.,std::chrono::duration<double>(Clock::now()-*start).count()):0.},
            {"hostPump",lastTiming},{"clients",Json::array()},{"failure",failure}};
        result["resourceRefill"]={{"seconds",refillSeconds},{"applied",refillApplied},
            {"failed",refillFailed},{"milliseconds",refillMilliseconds}};
        for(const auto &agent:agents) result["clients"].push_back(agent->status());
        if(host) {
            const auto state=host->diagnostics();
            result["host"]={{"tick",state.tick},{"participants",state.participants.size()},{"failures",state.failures},{"lastFailure",state.lastFailure}};
        }
        return result;
    };
    std::cout<<"Headless "<<mode<<": "<<agents.size()<<" native session(s), output "<<output<<std::endl;
    try { while(true) {
        const auto iteration=Clock::now();
        if(host && profile.value("resumeOnStart",false) && start && iteration>=*start && !resumedOnStart && !stopping) {
            const auto binding=host->diagnostics().player;
            if(!binding || !host->administer({hosting::AdminOperation::Resume,*binding,{}}).applied()) throw std::runtime_error("Cannot resume host at workload start");
            resumedOnStart=true;
        }
        if(host) {
            const auto pumpBegin=Clock::now();
            host->pump(std::chrono::duration<double>(iteration-last).count(),false);
            pumpTimes.push_back(std::chrono::duration<double,std::milli>(Clock::now()-pumpBegin).count());
            if(auto error=host->takeError();!error.empty()) {failure=error;failed=stopping=true;}
        }
        last=iteration;
        const bool finishingIteration=stopping;
        bool ready=true,finished=true;
        for(size_t agentIndex=0;agentIndex<agents.size();++agentIndex) {
            auto &agent=agents[agentIndex];
            if(!hosting && serialAdmission && !agent->connected && agentIndex && agents[agentIndex-1]->session.read().stage==OnlineStage::ProtocolReady && !stopping)
                connectClient(*agent);
            agent->observe();const auto &view=agent->session.read();
            if(hosting && !stopping) {
                if(view.stage==OnlineStage::CharacterSelection && !agent->selected) agent->selected=agent->session.select_character(ownerName);
                if(view.stage==OnlineStage::Lobby && !agent->created) {
                    net::CreateGameOptions game;game.name=ownerName;game.difficulty=uint8_t(integer(profile,"difficulty",0,0,2));
                    agent->created=agent->session.create_game(std::move(game));
                }
            }
            ready&=view.stage==OnlineStage::ProtocolReady;
            if(view.stage==OnlineStage::Failed || (!stopping && agent->entered && view.stage!=OnlineStage::ProtocolReady && view.stage!=OnlineStage::LoadingGame)) {
                failure=view.error?view.error->message:"Unexpected client departure";failed=stopping=true;
            }
            if(stopping) {
                if(!agent->left && (view.stage==OnlineStage::ProtocolReady || view.stage==OnlineStage::LoadingGame))
                    agent->left=agent->session.leave_game();
                finished&=view.stage!=OnlineStage::ProtocolReady && view.stage!=OnlineStage::LoadingGame && view.stage!=OnlineStage::LeavingGame;
            } else if(start && iteration>=*start) agent->drive(std::chrono::duration_cast<std::chrono::milliseconds>(iteration-*start).count());
        }
        if(!stopping) std::erase_if(pendingInputs,[&](const auto &input) {
            if(input.at("atUnixMs").template get<int64_t>()>std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) return false;
            submitInput(input);return true;
        });
        if(!stopping && host && refillSeconds && start && iteration>=*start) {
            const auto elapsed=std::chrono::duration_cast<std::chrono::seconds>(iteration-*start).count();
            if(elapsed>=nextRefill) {
                const auto refillBegin=Clock::now();
                for(const auto &participant:host->diagnostics().participants) if(participant.entered) {
                    const auto result=host->administer({hosting::AdminOperation::RestoreResources,participant.binding,{}});
                    if(result.applied()) ++refillApplied;else ++refillFailed;
                }
                refillMilliseconds+=std::chrono::duration<double,std::milli>(Clock::now()-refillBegin).count();
                nextRefill=elapsed+refillSeconds;
            }
        }
        pipe.poll([&](const std::string &line) {
            try {
                const auto request=Json::parse(line);const auto command=request.at("command").get<std::string>();
                if(command=="headless-status" || command=="status") return status().dump();
                if(command=="headless-start") {
                    if(start || stopping || !ready) throw std::invalid_argument("All clients must be ready; run can start only once");
                    int64_t delay=integer(request,"delayMs",1000,0,60000);
                    if(request.contains("atUnixMs")) {
                        if(request.contains("delayMs") || !request.at("atUnixMs").is_number_integer())
                            throw std::invalid_argument("Use integer atUnixMs or delayMs");
                        const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                        const auto at=request.at("atUnixMs").get<int64_t>();
                        if(at<now || at>now+60000) throw std::invalid_argument("atUnixMs must be within the next 60 seconds");
                        delay=at-now;
                    }
                    for(auto &agent:agents) agent->resetMeasurements();
                    start=Clock::now()+std::chrono::milliseconds(delay);started=true;
                    return status().dump();
                }
                if(command=="headless-input") {
                    if(stopping || !ready) throw std::invalid_argument("All clients must be ready for headless input");
                    if(request.contains("atUnixMs")) {
                        const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                        if(!request.at("atUnixMs").is_number_integer() || request.at("atUnixMs").get<int64_t>()<now || request.at("atUnixMs").get<int64_t>()>now+60000 || pendingInputs.size()>=32)
                            throw std::invalid_argument("Input time must be within60 seconds; at most32 pending inputs");
                        pendingInputs.push_back(request);return Json{{"ok",true},{"queued",true}}.dump();
                    }
                    return submitInput(request).dump();
                }
                if(command=="quit") {stopping=true;return Json{{"ok",true},{"saving",true}}.dump();}
                if(auto result=serverDebugCommand(request,host.get(),[&](const hosting::AdminRequest &value){return host->administer(value);})) return *result;
                throw std::invalid_argument("Unknown headless command");
            } catch(const std::exception &error) {return Json{{"ok",false},{"error",error.what()}}.dump();}
        });
        if(!start && ready && !profile.value("manualStart",true)) {
            for(auto &agent:agents) agent->resetMeasurements();
            start=iteration+std::chrono::seconds(2);started=true;
        }
        if(start && iteration>=*start+std::chrono::seconds(duration)) stopping=true;
        if(!stopping && !ready && !started && iteration-begin>std::chrono::seconds(connectTimeout)) {failure="Client admission timed out";failed=stopping=true;}
        if(interrupted) stopping=true;
        if(iteration>=nextSample) {
            lastTiming=timing(std::exchange(pumpTimes,{}));
            auto sample=status();sample["uptimeSeconds"]=std::chrono::duration<double>(iteration-begin).count();
            samples<<sample.dump()<<'\n';samples.flush();nextSample=iteration+std::chrono::seconds(1);
            flushTimings();
        }
        if(stopping) {
            if(!leaveDeadline) leaveDeadline=iteration+std::chrono::seconds(30);
            if(finishingIteration && finished && !pipe.responsePending()) break;
            if(iteration>*leaveDeadline) {failure="Native save/leave timed out; isolated originals and last returned saves retained";failed=true;break;}
        }
        std::this_thread::sleep_until(iteration+std::chrono::milliseconds(pollMs));
    } } catch(const std::exception &error) {failure=error.what();failed=stopping=true;}
    flushTimings();
    if(host) try {host->shutdown();}
    catch(const std::exception &error) {
        if(!failure.empty()) failure+="; ";
        failure+=std::string("Host shutdown: ")+error.what();failed=true;
    }
    auto final=status();final["ok"]=!failed;
    std::ofstream summary(output/"summary.json");summary.exceptions(std::ios::badbit|std::ios::failbit);summary<<final.dump(2)<<'\n';
    std::cout<<final.dump()<<std::endl;
    return failed?1:0;
}
}
