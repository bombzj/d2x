#pragma once
#include "ai.hpp"
#include "auto_walk.hpp"
#include "client/remote_combat.hpp"
#include "client/remote_control.hpp"
#include "client/remote_inventory.hpp"
#include "content/monsters/monster_identity.hpp"
#include "resources/data_table.hpp"
#include "gameplay/quest/catalog.hpp"
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
    enum class Kind {Travel,Find,Hunt,Acquire,ReturnTown,Deliver,Restock,Quest};
    enum class PotionKind {Life,Mana,Rejuvenation};
    struct Potion {PotionKind kind{};int strength{};std::optional<uint8_t> state;};
    struct Selector {uint8_t type{};uint16_t identity{};std::string name;std::optional<uint16_t> superUnique{};};
    enum class QuestAction {Talk,Clear,Hunt,Acquire,ReadScroll,Stones,Operate,Portal,ReturnTown,Imbue,ActTravel,Travel,UseItem,Combine,SubmitStaff};
    struct QuestStep {
        QuestAction action{};std::optional<uint16_t> area;std::optional<Selector> target,witness;
        std::string item,produced;std::optional<uint16_t> source;
        std::vector<uint16_t> topics,objects,skipAreas;uint16_t flags{};bool optional{},skipPending=true;
        std::optional<QuestId> stateQuest;
        uint16_t skipAny{},parentSkipAny{},clearFlags{};bool trueTomb{},skipTombKnown{},publishesTombSymbol{};
    };
    struct Goal {
        Kind kind{};std::optional<uint16_t> area;std::optional<Selector> target;
        std::string item;std::optional<uint16_t> sourceObject;
        std::string quest;unsigned questSlot{};std::vector<uint16_t> topics;
        bool interact{};int timeout=600;
        QuestId questId{};std::vector<QuestStep> steps;
    };
    struct Definitions {
        DataTable monsters,objects,skills,levels,misc,weapons,armor,types,states,npcs,superuniques;
        std::map<std::string,Potion,std::less<>> potions;
        explicit Definitions(Archives &);
        void loadPotions();
        uint16_t area(const Json &) const;
        uint16_t skill(const Json &) const;
        Selector selector(const Json &) const;
        uint16_t object(const Json &) const;
        uint16_t malusSource() const;
        std::optional<uint16_t> hintArea(const Selector &) const;
        bool item(std::string_view) const;
        Selector supplier(unsigned act) const;
    };
    struct CombatPolicy {
        bool enabled=true;std::string scope="defensive";std::optional<uint16_t> skill,aura;int range=16;
        int radius=6,crowd=4,retreat=8,engage=18;
    } policy;
    struct MobilityPolicy {std::string mode="auto";bool vigor=true;int distance=24;} mobilityPolicy;
    struct SupplyPolicy {
        bool enabled=true,buy=true,returnWhenEmpty=true;
        int lifePercent=60,manaPercent=35,criticalLifePercent=25,healthCount=4,manaCount=4,intervalMs=1500;
    } supplyPolicy;
    struct Context {
        net::RealmSession &session;RemoteTown &town;DebugAutoWalk &walk;RemoteControl &control;
        RemoteCombat &combat;RemoteInventory &inventory;Vec position;bool moving{};const SuppliesView *supplies{};
    };
    Archives &archives;std::unique_ptr<Definitions> definitions;
    std::vector<Goal> goals;size_t cursor{};std::string state="idle",phase,reason;
    std::deque<Json> history;uint64_t decisions{},sequence{},game{},areaGeneration{};
    std::optional<uint16_t> area;std::optional<OnlineUnitKey> target;
    std::optional<uint16_t> tombSymbol; // Learned only from this plan's real journal acknowledgement.
    uint64_t targetAssignment{};
    std::set<std::tuple<int,int,int>> visited;std::optional<std::tuple<int,int,int>> exploring;
    std::set<std::pair<uint16_t,uint16_t>> exploredFrontiers;
    std::set<std::tuple<uint16_t,uint8_t,uint16_t,uint16_t,uint16_t>> checkedHints;
    size_t knownExploredRooms{};
    uint64_t searchGeometryRevision{},mapCacheBuilds{},pathSearches{};
    std::optional<Vec> searchPosition;Clock::time_point searchRetry{};
    std::set<std::tuple<int,int,int>> unavailableRooms;
    std::optional<OnlinePoint> interest;std::optional<uint16_t> travelDestination;
    std::optional<OnlinePoint> approachHint,approachFrontier;
    Json walkIntent;
    std::set<std::pair<OnlineUnitKey,uint64_t>> operatedDoors;
    std::optional<std::pair<OnlineUnitKey,uint64_t>> pendingDoor;
    Clock::time_point doorDeadline{};
    enum class TravelPhase {Route,WaypointOpen,WaypointWait,WalkExit,WarpExit,CrossWait} travelPhase{};
    std::optional<uint16_t> legDestination,waypointDestination;std::optional<OnlineUnitKey> legUnit;
    Clock::time_point next{},goalDeadline{},actionDeadline{},lastAttack{},retreatUntil{},retreatReady{},pausedAt{};
    std::optional<OnlineUnitKey> combatTarget;std::optional<OnlinePoint> retreatPoint;
    std::optional<uint8_t> combatLife;Clock::time_point combatProgress{};
    std::map<std::pair<OnlineUnitKey,uint64_t>,Clock::time_point> bypassedThreats;
    bool combatOwned{},objectIssued{},pickupIssued{},portalIssued{},portalAttempted{},portalUsed{},greetingIssued{};
    std::optional<uint32_t> pickupId;std::set<uint32_t> oldPortals;
    std::set<std::tuple<uint32_t,uint16_t,uint16_t,uint16_t,uint16_t>> pickupApproaches;
    unsigned castRequests{},moveGoals{},interactions{};
    unsigned npcOpenRetries{};
    bool mobilityHold{},mobilityTeleport{};uint64_t mobilityDiscontinuity{};
    std::optional<OnlinePoint> mobilityTarget;Vec mobilityFrom;
    std::optional<OnlinePoint> mobilityGoal;float mobilityBestDistance{};Clock::time_point mobilityProgressAt{};
    Clock::time_point mobilityDeadline{},nextMobility{},lastSelection{},exitRetryAt{};
    std::map<std::tuple<uint16_t,uint16_t,uint16_t,uint16_t,uint16_t>,unsigned> mobilityTrials;
    struct PotionRequest {
        bool buying{};uint32_t item{};uint64_t sequence{},tradeRevision{};std::string code;
        PotionKind kind{};unsigned beforeCount{};std::optional<uint16_t> life,mana;
        Clock::time_point deadline{};
    };
    std::optional<PotionRequest> potionRequest;
    Clock::time_point nextPotion{},shopDeadline{},potionEffectUntil{};
    bool shopIssued{};uint64_t shopGeneration{};std::optional<OnlineUnitKey> supplyNpc;
    enum class SupplyStage {None,ToTown,Shop,Return} supplyStage{};
    std::optional<uint16_t> supplyOrigin;Clock::time_point supplyStarted{},supplyDeadline{};
    bool supplyPortalUsed{};Clock::time_point supplyPortalDeadline{};
    unsigned drinks{},purchases{};
    Json lastPotion,supplyVitals;
    struct QuestPending {
        enum class Action {Object,Portal,Scroll,Topic,Take,Imbue,Place,Travel,Pickup,Use,OpenCube,StoreCube,Transmute,SubmitStaff,Introduction,CloseCube} action{};
        std::optional<OnlineUnitKey> unit;uint64_t assignment{},sequence{},beforeRevision{};
        uint32_t item{};Clock::time_point deadline{};
        bool refreshIssued{};
    };
    struct QuestRun {
        bool initialized{};size_t step{};std::optional<QuestPending> pending;
        std::optional<uint16_t> denRemaining;Clock::time_point sweepReady{};
        unsigned sweeps{},materialPhase{};std::optional<uint32_t> material;
        std::optional<OnlineUnitKey> materialNpc;
        std::set<uint32_t> rewardBefore;
        std::set<uint32_t> materialBefore;std::optional<uint32_t> skillPointsBefore;
        bool rewardTracked{};uint64_t clueBefore{};
    } questRun;

    explicit Impl(Archives &a):archives(a) {}
    bool active() const {return state=="running";}
    bool live() const {return active() || state=="paused";}
    void note(std::string event,std::string detail={});
    Json snapshot() const;
    std::vector<Goal> parse(const Json &);
    CombatPolicy parseCombat(const Json &) const;
    MobilityPolicy parseMobility(const Json &) const;
    SupplyPolicy parseSupplies(const Json &) const;
    unsigned potionCount(const Context &,PotionKind) const;
    bool recovering(const Context &,std::optional<uint8_t>) const;
    bool observePotion(Context &);
    bool drink(Context &);
    bool restock(Context &);
    bool autoSupply(Context &,const Goal &);
    bool supplyDetour(Context &,const Goal &);
    bool supplyCombat(Context &,const Goal &);
    bool reachTown(Context &);
    void configureQuest(Goal &) const;
    void configureActTwoQuest(Goal &,QuestId) const;
    bool prepareQuest(Context &,const Goal &);
    Goal questLeaf(const Goal &) const;
    bool questBusy() const;
    void questStepDone(Context &,std::string);
    void quest(Context &,const Goal &);
    bool questTalk(Context &,const Goal &,const QuestStep &);
    bool questOperate(Context &,const QuestStep &);
    bool questPortal(Context &,const QuestStep &);
    bool questImbue(Context &,const Goal &,const QuestStep &);
    bool questActTravel(Context &,const Goal &,const QuestStep &);
    bool questReadScroll(Context &,const QuestStep &);
    bool questUseItem(Context &,const QuestStep &);
    bool questCombine(Context &,const QuestStep &);
    bool questSubmitStaff(Context &,const QuestStep &);
    bool questItemRequest(Context &,OnlineItemCommand,QuestPending::Action,std::string);
    bool questStoreCursor(Context &,uint32_t);
    void checkQuestPending(const Context &) const;
    void refreshQuestNpc(Context &);
    std::optional<uint16_t> questFlags(const Context &,QuestId) const;
    bool questFinished(const Context &,const Goal &) const;
    void observeQuestInteraction(Context &,const Json &);
    std::optional<OnlineItem> accessibleItem(const Context &,std::string_view) const;
    bool acquireItem(Context &,const Goal &);
    bool approachPickup(Context &,const OnlineItem &);
    bool settleItemInput(Context &);
    bool knownSkill(const Context &,uint16_t,bool left=false) const;
    bool teleportAvailable(const Context &) const;
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
