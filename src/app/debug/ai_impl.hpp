#pragma once
#include "ai.hpp"
#include "auto_walk.hpp"
#include "client/remote_combat.hpp"
#include "client/remote_control.hpp"
#include "client/remote_inventory.hpp"
#include "content/monsters/monster_identity.hpp"
#include "resources/data_table.hpp"
#include <chrono>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#include <nlohmann/json.hpp>

namespace d2x {
struct DebugAi::Impl {
    using Json=nlohmann::json;
    using Clock=std::chrono::steady_clock;
    enum class Kind {Travel,Find,Hunt,Acquire,ReturnTown,Deliver};
    struct Selector {uint8_t type{};uint16_t identity{};std::string name;};
    struct Goal {
        Kind kind{};std::optional<uint16_t> area;std::optional<Selector> target;
        std::string item;std::optional<uint16_t> sourceObject;
        std::string quest;unsigned questSlot{};std::vector<uint16_t> topics;
        bool interact{};int timeout=600;
    };
    struct Definitions {
        DataTable monsters,objects,skills,levels,misc,weapons,armor;
        explicit Definitions(Archives &);
        uint16_t area(const Json &) const;
        uint16_t skill(const Json &) const;
        Selector selector(const Json &) const;
        uint16_t object(const Json &) const;
        uint16_t malusSource() const;
        std::optional<uint16_t> hintArea(const Selector &) const;
        bool item(std::string_view) const;
    };
    struct CombatPolicy {
        bool enabled=true;std::string scope="defensive";std::optional<uint16_t> skill,aura;int range=16;
        int radius=6,crowd=4,retreat=8,engage=18;
    } policy;
    struct MobilityPolicy {std::string mode="auto";bool vigor=true;int distance=24;} mobilityPolicy;
    struct Context {
        net::RealmSession &session;RemoteTown &town;DebugAutoWalk &walk;RemoteControl &control;
        RemoteCombat &combat;RemoteInventory &inventory;Vec position;bool moving{};
    };
    Archives &archives;std::unique_ptr<Definitions> definitions;
    std::vector<Goal> goals;size_t cursor{};std::string state="idle",phase,reason;
    std::deque<Json> history;uint64_t decisions{},sequence{},game{},areaGeneration{};
    std::optional<uint16_t> area;std::optional<OnlineUnitKey> target;
    uint64_t targetAssignment{};
    std::set<std::tuple<int,int,int>> visited;std::optional<std::tuple<int,int,int>> exploring;
    std::set<std::pair<uint16_t,uint16_t>> exploredFrontiers;
    std::set<std::tuple<uint16_t,uint8_t,uint16_t,uint16_t,uint16_t>> checkedHints;
    size_t knownExploredRooms{};
    uint64_t searchGeometryRevision{},mapCacheBuilds{},pathSearches{};
    std::set<std::tuple<int,int,int>> unavailableRooms;
    std::optional<OnlinePoint> interest;std::optional<uint16_t> travelDestination;
    Json walkIntent;
    std::set<std::pair<OnlineUnitKey,uint64_t>> operatedDoors;
    std::optional<std::pair<OnlineUnitKey,uint64_t>> pendingDoor;
    Clock::time_point doorDeadline{};
    enum class TravelPhase {Route,WaypointOpen,WaypointWait,WalkExit,WarpExit,CrossWait} travelPhase{};
    std::optional<uint16_t> legDestination,waypointDestination;std::optional<OnlineUnitKey> legUnit;
    Clock::time_point next{},goalDeadline{},actionDeadline{},lastAttack{},retreatUntil{},retreatReady{},pausedAt{};
    std::optional<OnlineUnitKey> combatTarget;std::optional<OnlinePoint> retreatPoint;
    bool combatOwned{},objectIssued{},pickupIssued{},portalIssued{},portalAttempted{},portalUsed{},greetingIssued{};
    std::optional<uint32_t> pickupId;std::set<uint32_t> oldPortals;
    unsigned castRequests{},moveGoals{},interactions{};
    bool mobilityHold{},mobilityTeleport{};uint64_t mobilityDiscontinuity{};
    std::optional<OnlinePoint> mobilityTarget;Vec mobilityFrom;
    Clock::time_point mobilityDeadline{},nextMobility{},lastSelection{},exitRetryAt{};
    std::map<std::tuple<uint16_t,uint16_t,uint16_t,uint16_t,uint16_t>,unsigned> mobilityTrials;

    explicit Impl(Archives &a):archives(a) {}
    bool active() const {return state=="running";}
    bool live() const {return active() || state=="paused";}
    void note(std::string event,std::string detail={});
    Json snapshot() const;
    std::vector<Goal> parse(const Json &);
    CombatPolicy parseCombat(const Json &) const;
    MobilityPolicy parseMobility(const Json &) const;
    bool knownSkill(const Context &,uint16_t,bool left=false) const;
    bool selectSkill(Context &,uint16_t,OnlineSkillHand);
    std::optional<uint16_t> battleAura(const Context &) const;
    bool mobility(Context &);
    void stop(DebugAutoWalk &,RemoteControl &,RemoteCombat &);
    void finish(std::string,std::string,Context &);
    void advance(Context &,std::string);
    void resetStep();
    void cancelWalk(Context &);
    bool walk(Context &,Json);
    bool walkPoint(Context &,OnlinePoint,bool interact=false);
    void observeChild(Context &,const Goal &);
    std::string walkState(const Context &) const;
    void wait(std::string p,std::string why) {phase=std::move(p);reason=std::move(why);}
    std::vector<int> route(const Context &,int from,int to) const;
    bool travel(Context &,uint16_t,bool allowWaypoint=true);
    bool explore(Context &,std::string);
    bool approachKnown(Context &,const Selector &);
    bool approachPoint(Context &,OnlinePoint,std::string,float vicinity=8);
    bool openDoor(Context &);
    std::optional<OnlineUnit> find(const Context &,const Selector &,bool corpses=false) const;
    bool owned(const Context &,std::string_view) const;
    bool combat(Context &,const Goal &);
    void execute(Context &,const Goal &);
    void acquire(Context &,const Goal &);
    void returnTown(Context &);
    void deliver(Context &,const Goal &);
};
}
