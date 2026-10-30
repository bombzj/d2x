#pragma once
#include "ai.hpp"
#include "auto_walk.hpp"
#include "action_state.hpp"
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
    enum class QuestAction {Talk,Clear,Hunt,Acquire,ReadScroll,Stones,Operate,Portal,ReturnTown,Imbue,ActTravel,Travel,UseItem,Combine,SubmitStaff,SearchDrop,EquipWeapon,SmashOrb};
    struct QuestStep {
        QuestAction action{};std::optional<uint16_t> area;std::optional<Selector> target,witness;
        std::string item,produced;std::optional<uint16_t> source;
        std::vector<uint16_t> topics,objects,skipAreas;uint16_t flags{};bool optional{},skipPending=true;
        std::optional<QuestId> stateQuest;
        std::optional<QuestId> skipGranted;
        uint16_t skipAny{},parentSkipAny{},clearFlags{};bool trueTomb{},skipTombKnown{},publishesTombSymbol{};
        std::optional<uint16_t> sourceArea;
        bool preferWaypoint{},refreshFlags{};
        bool clearCombat{};std::vector<std::string> ingredients;std::vector<uint16_t> searchAreas;
    };
    struct QuestFact {
        enum class Kind {Item,BackpackItem,Flags,Area,TrueTombArea,TombClue,Waypoint,Corpse,BookUsed,PortalObserved,JournalRead,ActChange,Equipped,EquipmentRestored,ObjectOpened} kind{};
        QuestId quest{};uint16_t value{},clear{};bool any{};std::string item;
    };
    struct QuestRequirement {
        std::string name;std::vector<size_t> needs;
        std::vector<std::vector<QuestFact>> alternatives;
        bool retainObservation{};
        int townOrder{};
    };
    struct Goal {
        Kind kind{};std::optional<uint16_t> area;std::optional<Selector> target;
        std::string item;std::optional<uint16_t> sourceObject;
        std::string quest;unsigned questSlot{};std::vector<uint16_t> topics;
        bool interact{};int timeout=600;
        QuestId questId{};std::vector<QuestStep> steps;
        std::optional<uint16_t> questTown;
        std::vector<QuestRequirement> requirements;std::vector<size_t> roots;
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
        std::vector<uint16_t> fallbackSkills;
        int noProgressSeconds=6,stallTimeoutSeconds=90,retreatLifePercent=45,lifeDropPercent=10;
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
    std::optional<uint16_t> tombSymbol; // Actual journal-qualified server payload, including valid initialization.
    std::optional<uint64_t> journalReadGame; // Confirmed reading in this process/game, never a saved portal/unlock fact.
    uint64_t targetAssignment{};
    std::set<std::tuple<int,int,int>> visited;std::optional<std::tuple<int,int,int>> exploring;
    struct FrontierAttempt {
        Vec position;uint64_t geometryRevision{};Clock::time_point retryAt;
        bool reached{};std::string outcome,reason;
    };
    std::map<std::pair<uint16_t,uint16_t>,FrontierAttempt> frontierAttempts;
    struct ViewpointAttempt {size_t observedCells{};Clock::time_point retryAt;};
    std::map<std::pair<uint16_t,uint16_t>,ViewpointAttempt> viewpointAttempts;
    struct BacktrackAttempt {size_t observedCells{};Clock::time_point retryAt;std::string reason;};
    std::map<std::tuple<int,int,int>,BacktrackAttempt> backtrackAttempts;
    std::map<std::tuple<int,int,int,int,int,int>,bool> observedRoomPassages;
    std::set<std::tuple<uint16_t,uint8_t,uint16_t,uint16_t,uint16_t>> checkedHints;
    size_t knownExploredRooms{};
    uint64_t searchGeometryRevision{},mapCacheBuilds{},pathSearches{};
    std::optional<Vec> searchPosition;Clock::time_point searchRetry{};
    std::set<std::tuple<int,int,int>> unavailableRooms;
    std::optional<OnlinePoint> interest;std::optional<uint16_t> travelDestination;
    std::optional<OnlinePoint> approachHint,approachFrontier,approachDetour;
    Json walkIntent;
    Json searchDiagnostics,approachDiagnostics,mobilityDiagnostics,travelDiagnostics;
    std::set<std::pair<OnlineUnitKey,uint64_t>> operatedDoors;
    std::optional<std::pair<OnlineUnitKey,uint64_t>> pendingDoor;
    Clock::time_point doorDeadline{};
    struct WaypointActivation {OnlineUnitKey unit;uint64_t assignment{};uint16_t area{};bool menu{};};
    std::optional<WaypointActivation> pendingWaypoint;
    std::set<uint16_t> activatedWaypointAreas;
    Clock::time_point waypointDeadline{};
    struct LocalPad {
        OnlineUnitKey unit;uint64_t assignment{};uint16_t area{},identity{};OnlinePoint source{},from{};
        uint64_t discontinuity{},requestRevision{};Clock::time_point deadline{};bool waiting{};
    };
    struct PadObservation {OnlinePoint destination;Clock::time_point used{};unsigned visits{};};
    std::optional<LocalPad> localPad;
    std::map<std::tuple<uint16_t,uint16_t,uint16_t,uint16_t>,PadObservation> padObservations;
    std::optional<OnlinePoint> padArrival;
    Clock::time_point padArrivedAt{};
    size_t padProgressCells{},padProgressTargets{};unsigned padTransfersWithoutDiscovery{};
    enum class TravelPhase {Route,WaypointOpen,WaypointWait,WalkExit,WarpExit,CrossWait} travelPhase{};
    std::optional<uint16_t> legDestination,waypointDestination;std::optional<OnlineUnitKey> legUnit;
    bool warpRetryUsed{};
    Clock::time_point next{},goalDeadline{},actionDeadline{},lastAttack{},retreatUntil{},retreatReady{},pausedAt{};
    bool actionWaitSubmitted{};
    void setActionDeadline(std::chrono::seconds duration,bool submitted=true) {
        actionDeadline=Clock::now()+duration;actionWaitSubmitted=submitted;
    }
    DebugExecutionClock executionClock;
    Json actionDiagnostics;
    bool inputBlocked{};std::optional<uint32_t> closeWaypoint;
    std::optional<OnlineUnitKey> combatTarget;std::optional<OnlinePoint> retreatPoint;
    struct CombatRun {
        uint64_t assignment{};size_t skillIndex{};
        std::vector<uint16_t> skills;
        std::optional<uint8_t> bestLife,lastLife;
        Clock::time_point progressAt{},strategyAt{},approachUntil{},nextCast{},suspendedAt{};
        bool repositioned{},repeating{},healingObserved{},qualificationWait{};unsigned casts{},hits{};
        uint64_t hitRevision{};std::string obstacle;
        std::optional<OnlinePoint> reposition;
        std::set<std::pair<uint16_t,uint16_t>> triedPositions;
        Json attempts=Json::array();
    } combatRun;
    struct LifeSample {Clock::time_point at;uint16_t life;};
    std::deque<LifeSample> combatLifeSamples;
    std::optional<int> combatMaxLife;
    Json combatDiagnostics;
    std::map<std::pair<OnlineUnitKey,uint64_t>,Clock::time_point> bypassedThreats;
    bool combatOwned{},objectIssued{},pickupIssued{},portalIssued{},portalAttempted{},portalUsed{},greetingIssued{};
    std::optional<uint32_t> pickupId;std::set<uint32_t> oldPortals;
    std::set<std::tuple<uint32_t,uint16_t,uint16_t,uint16_t,uint16_t>> pickupApproaches;
    unsigned castRequests{},moveGoals{},interactions{};
    unsigned npcOpenRetries{};
    bool mobilityHold{},mobilityTeleport{};uint64_t mobilityDiscontinuity{};
    std::optional<OnlinePoint> mobilityTarget;Vec mobilityFrom;
    std::optional<OnlinePoint> mobilityGoal;uint16_t mobilityArea{};
    std::vector<Vec> mobilityRoute;float mobilityRouteProgress{},mobilityPendingProgress{},mobilityRouteLength{};
    Clock::time_point mobilityDeadline{},nextMobility{},lastSelection{},exitRetryAt{};
    std::map<std::tuple<uint16_t,uint16_t,uint16_t,uint16_t,uint16_t>,unsigned> mobilityTrials;
    struct PotionRequest {
        bool buying{};uint32_t item{};uint64_t sequence{},tradeRevision{};std::string code;
        PotionKind kind{};unsigned beforeCount{};std::optional<uint16_t> life,mana;
        Clock::time_point deadline{};
    };
    std::optional<PotionRequest> potionRequest;
    Clock::time_point nextPotion{},shopDeadline{},potionEffectUntil{};
    bool shopIssued{},shelfObserved{};uint64_t shopGeneration{};std::optional<OnlineUnitKey> supplyNpc;
    enum class SupplyStage {None,ToTown,Shop,Return} supplyStage{};
    std::optional<uint16_t> supplyOrigin;Clock::time_point supplyStarted{},supplyDeadline{};
    bool supplyPortalUsed{};Clock::time_point supplyPortalDeadline{};
    unsigned drinks{},purchases{};
    Json lastPotion,supplyVitals;
    struct QuestPending {
        enum class Action {Object,Portal,Scroll,Topic,Take,Imbue,Place,Travel,Pickup,Use,OpenCube,StoreCube,Transmute,SubmitStaff,Introduction,CloseCube} action{};
        std::optional<OnlineUnitKey> unit;uint64_t assignment{},sequence{},beforeRevision{};
        uint32_t item{};Clock::time_point deadline{};
        bool refreshIssued{},statusRequested{},resultObserved{};
    };
    struct QuestRun {
        bool initialized{};size_t step{};std::optional<QuestPending> pending;
        std::optional<uint16_t> denRemaining;Clock::time_point sweepReady{};
        unsigned sweeps{},materialPhase{};std::optional<uint32_t> material;
        std::optional<OnlineUnitKey> materialNpc;
        std::set<uint32_t> rewardBefore;
        std::set<uint32_t> materialBefore;std::optional<uint32_t> skillPointsBefore;
        bool rewardTracked{};uint64_t clueBefore{};
        std::set<size_t> observations;
        bool selected{};
    } questRun;
    Json questPlanDiagnostics;
    std::optional<uint32_t> questWeaponRestore;

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
    void configureActThreeQuest(Goal &,QuestId) const;
    bool questSearchDrop(Context &,const QuestStep &);
    bool questEquipWeapon(Context &,const QuestStep &);
    bool questSmashOrb(Context &,const QuestStep &);
    bool prepareQuest(Context &,const Goal &);
    bool selectQuestRequirement(Context &,const Goal &);
    bool questFactSatisfied(const Context &,const Goal &,size_t,const QuestFact &) const;
    bool questRequirementSatisfied(const Context &,const Goal &,size_t) const;
    Goal questLeaf(const Goal &) const;
    bool questTransaction(const Context &) const;
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
    void observeActions(Context &);
    void observeQuestResult(Context &);
    bool observeMobility(Context &);
    bool questWaitRequiresInput(const Context &) const;
    std::vector<DebugActionContract> actionContracts(const Context &) const;
    bool canInput(const Context &,DebugActionOwner,unsigned) const;
    void describeActions(const Context &);
    std::string walkState(const Context &) const;
    void wait(std::string p,std::string why) {phase=std::move(p);reason=std::move(why);}
    std::vector<int> route(const Context &,int from,int to) const;
    float navigationCost(const Context &,int level,std::optional<Vec> start,std::optional<OnlinePoint> target) const;
    bool exploreLocalPad(Context &);
    void observeLocalPad(Context &);
    float routeCost(const Context &,const std::vector<int> &,std::optional<Vec> start={}) const;
    void refreshNavigationAttempts(const Context &);
    void recordFrontierAttempt(const Context &,OnlinePoint,std::string outcome,std::string reason={});
    bool frontierCoolingDown(OnlinePoint,float radius) const;
    bool travel(Context &,uint16_t,bool allowWaypoint=true);
    bool explore(Context &,std::string,bool recoveryOnly=false);
    bool approachKnown(Context &,const Selector &);
    bool approachPoint(Context &,OnlinePoint,std::string,float vicinity=8);
    bool openDoor(Context &,bool frontier=false);
    bool activateWaypoint(Context &);
    std::optional<OnlineUnit> find(const Context &,const Selector &,bool corpses=false) const;
    bool owned(const Context &,std::string_view) const;
    bool combat(Context &,const Goal &);
    void observeCombat(Context &);
    void releaseCombat(Context &);
    void execute(Context &,const Goal &);
    void acquire(Context &,const Goal &);
    void returnTown(Context &);
    void deliver(Context &,const Goal &);
};
}
