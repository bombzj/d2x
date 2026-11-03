#pragma once
#include "ai.hpp"
#include "ai_behavior_log.hpp"
#include "auto_walk.hpp"
#include "action_state.hpp"
#include "client/remote_combat.hpp"
#include "client/remote_control.hpp"
#include "client/remote_inventory.hpp"
#include "content/monsters/monster_identity.hpp"
#include "resources/data_table.hpp"
#include "gameplay/quest/catalog.hpp"
#include "gameplay/rules/formula.hpp"
#include <chrono>
#include <deque>
#include <functional>
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
    struct Potion {PotionKind kind{};int strength{};bool bulkBelt{};std::optional<uint8_t> state;};
    struct Selector {uint8_t type{};uint16_t identity{};std::string name;std::optional<uint16_t> superUnique{};bool eliteOnly{};};
    enum class QuestAction {Talk,Clear,Hunt,Acquire,ReadScroll,Stones,Operate,Portal,ReturnTown,Imbue,ActTravel,Travel,UseItem,Combine,SubmitStaff,SearchDrop,EquipWeapon,SmashOrb,SmashForge,Service,Rescue,Encounter};
    struct QuestStep {
        QuestAction action{};std::optional<uint16_t> area;std::optional<Selector> target,witness;
        std::string item,produced;std::optional<uint16_t> source;
        std::vector<uint16_t> topics,objects,skipAreas;uint16_t flags{};bool optional{},skipPending=true;
        std::optional<QuestId> stateQuest;
        std::optional<QuestId> skipGranted;
        uint16_t skipAny{},parentSkipAny{},clearFlags{};bool trueTomb{},skipTombKnown{},publishesTombSymbol{};
        std::optional<uint16_t> sourceArea;
        bool preferWaypoint{},refreshFlags{},activateWaypoint{};
        bool clearCombat{};std::vector<std::string> ingredients;std::vector<uint16_t> searchAreas;
        unsigned skillPointReward{};
        std::vector<std::string> rewardCodes;bool rareReward{};
        std::optional<uint8_t> status,dropNoticeStatus;
        std::optional<uint8_t> cinematic;
        unsigned resultWaitSeconds{};int residualRange{};std::optional<uint8_t> stageSound;
        std::optional<Vec> witnessOffset;
    };
    struct QuestFact {
        enum class Kind {Item,BackpackItem,Flags,Area,TrueTombArea,TombClue,Waypoint,Corpse,BookUsed,PortalObserved,JournalRead,ActChange,Equipped,EquipmentRestored,ObjectOpened,GroundItem,MonsterObserved,Status,Cinematic,InitiallyRewarded,CairnOrder,SummitPassage} kind{};
        QuestId quest{};uint16_t value{},clear{};bool any{};std::string item;std::optional<uint16_t> area;
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
        std::string quest,skipReason;unsigned questSlot{};std::vector<uint16_t> topics;
        bool interact{}, requiredWaypoint{};int timeout=600;
        bool initiallyRewarded{},useServiceRewards{};
        QuestId questId{};std::vector<QuestStep> steps;
        std::optional<uint16_t> questTown;
        std::vector<QuestRequirement> requirements;std::vector<size_t> roots;
        std::optional<size_t> routeDrop; // Collect a requested quest drop while pursuing another root.
    };
    struct Definitions {
        DataTable monsters,objects,skills,levels,misc,weapons,armor,types,states,npcs,superuniques,missiles,difficulties;
        std::optional<uint16_t> harrogathGate;
        uint16_t staticFieldSkill{};size_t staticFieldRow{};RuleFormula staticFieldRange;
        std::map<int,int> hazardSizes;
        std::set<int> dangerousMissiles;
        std::map<uint16_t,size_t> skillRows;
        std::map<uint16_t,uint16_t> attackMasks;
        std::map<std::string,Potion,std::less<>> potions;
        explicit Definitions(Archives &);
        void loadPotions();
        void loadCombatObservations();
        uint16_t area(const Json &) const;
        uint16_t skill(const Json &) const;
        Selector selector(const Json &) const;
        uint16_t object(const Json &) const;
        uint16_t malusSource() const;
        std::optional<uint16_t> hintArea(const Selector &) const;
        bool item(std::string_view) const;
        bool itemHasType(std::string_view,std::string_view) const;
        std::optional<int> staticRadius(unsigned) const;
        Selector supplier(unsigned act,bool portal=false) const;
    };
    struct CombatPolicy {
        bool enabled=true,staticField=true,avoidHazards=true;std::string scope="defensive";std::optional<uint16_t> skill,aura;int range=16;
        int radius=6,crowd=4,retreat=8,engage=18;
        std::vector<uint16_t> fallbackSkills;
        int noProgressSeconds=6,stallTimeoutSeconds=90,retreatLifePercent=45,lifeDropPercent=10;
        bool rangedKite=true;
        int kiteIntervalMs=900,kiteDistance=6,kiteMinDistance=8,hazardLookaheadMs=350,hazardPadding=3;
    } policy;
    struct MobilityPolicy {std::string mode="auto";bool vigor=true;int distance=24;} mobilityPolicy;
    struct EventPolicy {int waitSeconds=90,encounterRadius=32,standoffMin=8,standoffMax=24;} eventPolicy;
    struct SupplyPolicy {
        bool enabled=true,buy=true,returnWhenEmpty=true,fillPortalBook=true;
        int lifePercent=60,manaPercent=35,criticalLifePercent=25,healthCount=4,manaCount=4,intervalMs=1500;
    } supplyPolicy;
    struct Context {
        net::RealmSession &session;RemoteTown &town;DebugAutoWalk &walk;RemoteControl &control;
        RemoteCombat &combat;RemoteInventory &inventory;Vec position;bool moving{};const SuppliesView *supplies{};
    };
    struct VisualThreat {int id{};Vec from,to;float radius{};bool unitSource{}, dangerous{};};
    std::vector<VisualThreat> visibleThreats(const Context &) const;
    static float threatClearance(std::span<const VisualThreat>,Vec);
    Archives &archives;std::unique_ptr<Definitions> definitions;
    std::vector<Goal> goals;size_t cursor{};std::string state="idle",phase,reason,configurationFile;
    std::deque<Json> history;uint64_t decisions{},sequence{},game{},areaGeneration{};
    std::filesystem::path logDirectory;
    AiBehaviorLog behaviorLog;
    uint64_t observedWalkFailures{};
    bool phaseReported{};
    void goalStarted();
    void cancel(DebugAutoWalk &,RemoteControl &,RemoteCombat &,std::string);
    std::optional<uint16_t> area;std::optional<OnlineUnitKey> target;
    std::optional<uint16_t> tombSymbol; // Actual journal-qualified server payload, including valid initialization.
    // Present only after the summit door refused this player during this visit.
    std::optional<std::set<uint16_t>> summitTrialKills;
    std::optional<uint64_t> journalReadGame; // Confirmed reading in this process/game, never a saved portal/unlock fact.
    uint64_t targetAssignment{};
    struct ExplorationReturn {
        uint16_t area{};bool returning{};Clock::time_point deadline;
        std::set<std::tuple<int,int,int>> visited;
    };
    std::optional<ExplorationReturn> explorationReturn;
    std::map<std::tuple<int,int,int>,size_t> hubReturnCoverage;
    bool resumeExplorationReturn(Context &);
    std::set<std::tuple<int,int,int>> visited;std::optional<std::tuple<int,int,int>> exploring;
    struct FrontierAttempt {
        Vec position;uint64_t geometryRevision{};Clock::time_point retryAt;
        bool reached{},retainAfterMove{};std::string outcome,reason;std::optional<OnlinePoint> relayGoal;
    };
    std::map<std::pair<uint16_t,uint16_t>,FrontierAttempt> frontierAttempts;
    struct ViewpointAttempt {size_t observedCells{};Clock::time_point retryAt;};
    std::map<std::pair<uint16_t,uint16_t>,ViewpointAttempt> viewpointAttempts;
    struct ViewpointRoute {OnlinePoint target;size_t observed;std::vector<Vec> origins;};
    std::optional<ViewpointRoute> viewpointRoute;
    struct BacktrackAttempt {size_t observedCells{};Clock::time_point retryAt;std::string reason;};
    std::map<std::tuple<int,int,int>,BacktrackAttempt> backtrackAttempts;
    std::map<std::tuple<int,int,int,int,int,int>,bool> observedRoomPassages;
    std::set<std::tuple<uint16_t,uint8_t,uint16_t,uint16_t,uint16_t>> checkedHints;
    size_t knownExploredRooms{};
    uint64_t searchGeometryRevision{},mapCacheBuilds{},pathSearches{};
    std::optional<Vec> searchPosition;Clock::time_point searchRetry{};
    std::set<std::tuple<int,int,int>> unavailableRooms;
    std::optional<OnlinePoint> interest;std::optional<uint16_t> travelDestination;
    std::optional<OnlinePoint> boundaryAim;
    std::vector<int> travelRoute;
    std::set<int> travelVisitedAreas;
    std::optional<uint16_t> unavailableWaypointArea;
    std::map<std::pair<int,int>,unsigned> failedWalkEdges;
    float travelLegCost(const Context &,int from,int to,std::optional<Vec> start) const;
    std::optional<OnlinePoint> approachHint,approachFrontier,approachDetour;
    Json walkIntent;
    Json searchDiagnostics,approachDiagnostics,mobilityDiagnostics,travelDiagnostics;
    std::set<std::pair<OnlineUnitKey,uint64_t>> operatedDoors;
    std::optional<std::pair<OnlineUnitKey,uint64_t>> pendingDoor;
    Clock::time_point doorDeadline{};
    struct WaypointActivation {OnlineUnitKey unit;uint64_t assignment{};uint16_t area{};bool menu{};};
    std::optional<WaypointActivation> pendingWaypoint;
    struct WaypointDeferral {OnlineUnitKey unit;uint64_t assignment{};Clock::time_point retryAt{};Vec position;};
    std::optional<WaypointDeferral> deferredWaypoint;
    std::optional<uint64_t> handledWaypointTimeout;
    std::set<uint16_t> requiredWaypointAreas;
    std::optional<uint16_t> waypointDepartureArea,nearbyWaypointArea;
    std::optional<OnlinePoint> missingWaypointAt;
    std::set<uint16_t> knownWaypointAreas; // Sole AI unlock memory; raw 63 remains the protocol snapshot.
    std::set<uint16_t> waypointTownArrivals;
    uint64_t waypointMemoryGame{},waypointMemoryRevision{};
    std::optional<uint64_t> waypointActivationGeneration;
    struct RememberedEnemy {OnlinePoint position;uint64_t assignment{};std::optional<uint16_t> identity;};
    struct AreaPopulation {
        std::optional<uint32_t> seed;
        std::map<std::tuple<int,int,int>,uint64_t> rooms;
        std::map<OnlineUnitKey,RememberedEnemy> enemies;
        Clock::time_point readyAt{};
    };
    std::map<uint16_t,AreaPopulation> populationMemory;
    uint64_t populationMemoryGame{},populationMemoryRevision{};
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
    Clock::time_point goalDeadline{},actionDeadline{},lastAttack{},retreatUntil{},retreatReady{},pausedAt{};
    bool actionWaitSubmitted{};
    void setActionDeadline(std::chrono::seconds duration,bool submitted=true) {
        actionDeadline=Clock::now()+duration;actionWaitSubmitted=submitted;
    }
    DebugExecutionClock executionClock;
    DebugDecisionCadence cadence;
    std::optional<Json> defendedTravel;
    size_t defendedStep{};
    bool defendNavigation(Context &,const Goal &);
    void resumeDefendedTravel(Context &);
    std::optional<uint16_t> decisionLife;
    bool decisionDue(const Context &,Clock::time_point);
    Json actionDiagnostics;
    std::map<DebugActionOwner,DebugSubmissionWait> submissionWaits;
    DebugSubmitResult submitInput(Context &,DebugActionOwner,std::string,unsigned,
        const std::function<bool()> &,const std::function<std::string()> &,
        std::chrono::milliseconds budget=std::chrono::seconds(12));
    bool submitItem(Context &,const OnlineItemCommand &,std::string,DebugActionOwner=DebugActionOwner::Goal);
    bool submitCombat(Context &,const OnlineCombatCommand &,DebugActionOwner,std::string,std::optional<Vec> = {});
    bool inputBlocked{};std::optional<uint32_t> closeWaypoint;
    std::optional<OnlineUnitKey> combatTarget;std::optional<OnlinePoint> retreatPoint;
    struct CombatRun {
        uint64_t assignment{};size_t skillIndex{};
        std::vector<uint16_t> skills;
        std::optional<uint8_t> bestLife,lastLife,openingLife,staticCastLife;
        Clock::time_point progressAt{},strategyAt{},approachUntil{},nextCast{},suspendedAt{};
        bool repositioned{},repeating{},healingObserved{},qualificationWait{},staticOpening{};unsigned casts{},strategyCasts{},hits{};
        bool staticTried{};unsigned staticCasts{},primaryLifeDrop{};
        uint64_t hitRevision{};std::string obstacle;
        std::optional<OnlinePoint> reposition;
        std::optional<OnlinePoint> kitePoint;
        Clock::time_point kiteUntil{},nextKite{};
        bool kiteAfterCast{};int orbitDirection=1;
        std::set<std::pair<uint16_t,uint16_t>> triedPositions;
        Json attempts=Json::array();
    } combatRun;
    struct LifeSample {Clock::time_point at;uint16_t life;};
    std::deque<LifeSample> combatLifeSamples;
    std::optional<int> combatMaxLife;
    Json combatDiagnostics;
    std::optional<OnlinePoint> hazardDodge;Clock::time_point hazardDodgeUntil{};Json hazardDiagnostics;
    struct BypassedThreat {Clock::time_point retryAt;Vec observer,target;};
    std::map<std::pair<OnlineUnitKey,uint64_t>,BypassedThreat> bypassedThreats;
    bool combatOwned{},objectIssued{},portalIssued{},portalAttempted{},portalUsed{},greetingIssued{};
    struct PickupReceipt {std::string code;uint32_t item;uint64_t sequence;Clock::time_point deadline;bool observed{};};
    std::optional<PickupReceipt> pickup;std::set<uint32_t> oldPortals;
    struct RememberedDrop {uint32_t id{};uint16_t area{};OnlinePoint position{};Clock::time_point inspectSince{};};
    std::map<std::string,RememberedDrop,std::less<>> rememberedDrops;
    void observeDrops(const Context &,const Goal &);
    enum class Continuation {None, WorldResult, KnownDrop};
    Continuation continuation{};
    bool resumeAction(Context &,const Goal &,const Goal &leaf);
    void observePickup(const Context &);
    void executeGoal(Context &,const Goal &);
    std::set<std::tuple<uint32_t,uint16_t,uint16_t,uint16_t,uint16_t>> pickupApproaches;
    unsigned castRequests{},moveGoals{},interactions{};
    unsigned npcOpenRetries{};
    bool npcOpeningRecovery{};
    bool actionRecoveryBusy{};
    std::map<std::tuple<uint16_t,OnlineUnitKey,uint64_t>,unsigned> navigationRetries;
    struct TravelSubmission {
        OnlineUnitKey unit;uint64_t assignment{},game{},world{},request{},discontinuity{};uint16_t sourceArea{};
    };
    std::optional<TravelSubmission> submittedTravel;
    std::map<std::tuple<uint16_t,OnlineUnitKey,uint64_t>,unsigned> travelRetries;
    bool mobilityHold{},mobilityTeleport{};uint64_t mobilityDiscontinuity{};
    std::optional<OnlinePoint> mobilityTarget;Vec mobilityFrom;
    std::optional<OnlinePoint> mobilityGoal;uint16_t mobilityArea{};
    std::vector<Vec> mobilityRoute;float mobilityRouteProgress{},mobilityPendingProgress{},mobilityRouteLength{};
    Clock::time_point mobilityDeadline{},nextMobility{},lastSelection{},exitRetryAt{};
    std::optional<uint16_t> townExitArea;Clock::time_point townExitObserveUntil{};
    std::map<std::tuple<uint16_t,uint16_t,uint16_t,uint16_t,uint16_t>,unsigned> mobilityTrials;
    struct PotionRequest {
        bool buying{},portalSupply{};uint32_t item{};uint64_t sequence{},tradeRevision{};std::string code;
        PotionKind kind{};unsigned beforeCount{},expectedGain=1;std::optional<uint16_t> life,mana;
        Clock::time_point deadline{};
    };
    std::optional<PotionRequest> potionRequest;
    Clock::time_point nextPotion{},shopDeadline{},potionEffectUntil{};
    unsigned shelfReopens{};
    bool shopIssued{},shelfObserved{},healingIssued{},healingObserved{};uint64_t shopGeneration{};std::optional<OnlineUnitKey> supplyNpc;
    enum class SupplyStage {None,ToTown,Shop,Return} supplyStage{};
    std::optional<uint16_t> supplyOrigin;Clock::time_point supplyStarted{},supplyDeadline{};
    bool supplyPortalUsed{};Clock::time_point supplyPortalDeadline{};
    unsigned drinks{},purchases{};
    struct PortalStock {unsigned books{},pages{},capacity{};bool known=true;};
    PortalStock portalStock(const Context &) const;
    Json lastPotion,supplyVitals;
    struct QuestPending {
        enum class Action {Object,Portal,Scroll,Topic,Take,Imbue,Place,Travel,Pickup,Use,OpenCube,StoreCube,Transmute,SubmitStaff,Introduction,CloseCube,Service,RecoverOpen,RecoverTake,RecoverPlace,RecoverClose} action{};
        std::optional<OnlineUnitKey> unit;uint64_t assignment{},sequence{},beforeRevision{};
        uint32_t item{};Clock::time_point deadline{};
        bool refreshIssued{},statusRequested{},resultObserved{};
    };
    // Passive scene observation is not a submitted operation. It may be
    // interrupted, but only relevant new facts renew its inactivity budget.
    struct WorldWait {
        uint16_t area{};Vec anchor;Clock::time_point deadline{};
        Clock::time_point inspectResidualAt{};uint64_t soundSequence{};int residualRange{},witnessSize{};
        unsigned seconds{},progress{};std::string expected,lastProgress;
        struct Enemy {std::optional<uint8_t> bestLife;bool dead{};};
        std::map<std::pair<OnlineUnitKey,uint64_t>,Enemy> enemies;
    };
    struct QuestRun {
        bool initialized{},initialInventory{};size_t step{};std::optional<QuestPending> pending;
        std::optional<uint32_t> recoveringItem;
        std::optional<WorldWait> worldWait;
        std::optional<uint16_t> denRemaining;Clock::time_point sweepReady{};
        unsigned sweeps{},materialPhase{};std::optional<uint32_t> material;
        std::optional<OnlineUnitKey> materialNpc;
        std::set<uint32_t> rewardBefore;
        std::set<uint32_t> materialBefore;std::optional<uint32_t> skillPointsBefore;
        bool rewardTracked{},retriedMissingTopic{};Clock::time_point missingTopicSince{};uint64_t clueBefore{};
        std::set<size_t> observations;
        bool selected{};
        std::optional<std::pair<OnlineUnitKey,uint64_t>> encounterWitness;
        std::optional<OnlinePoint> encounterPosition;
        std::optional<OnlinePoint> encounterStandoff;
        std::optional<uint16_t> encounterArea;
        bool encounterAnchored{};
        Clock::time_point witnessUntil{};
        std::set<std::pair<OnlineUnitKey,uint64_t>> approachedWitnesses;
    } questRun;
    Json questPlanDiagnostics;
    std::optional<uint32_t> questWeaponRestore;
    std::optional<std::pair<uint16_t,OnlinePoint>> questSpawnAnchor;

    explicit Impl(Archives &a):archives(a) {}
    bool active() const {return state=="running";}
    bool live() const {return active() || state=="paused";}
    void note(std::string event,std::string detail={});
    Json snapshot() const;
    std::vector<Goal> parse(const Json &);
    CombatPolicy parseCombat(const Json &) const;
    MobilityPolicy parseMobility(const Json &) const;
    SupplyPolicy parseSupplies(const Json &) const;
    EventPolicy parseEvents(const Json &) const;
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
    void configureActFourQuest(Goal &,QuestId) const;
    void configureActFiveQuest(Goal &,QuestId) const;
    bool questService(Context &,const QuestStep &);
    bool questRescue(Context &,const QuestStep &);
    bool questEncounter(Context &,const QuestStep &);
    bool questReceiveRewards(Context &,const QuestStep &);
    bool questSearchDrop(Context &,const QuestStep &);
    bool questEquipWeapon(Context &,const QuestStep &);
    bool questSmashOrb(Context &,const QuestStep &);
    bool questSmashForge(Context &,const QuestStep &);
    bool prepareQuest(Context &,const Goal &);
    bool recoverQuestInventory(Context &,const QuestStep * = nullptr);
    bool selectQuestRequirement(Context &,const Goal &);
    void observeQuestFacts(const Context &);
    bool questFactSatisfied(const Context &,const Goal &,size_t,const QuestFact &) const;
    enum class RouteDrop { None, Seek, Notice, Observed };
    RouteDrop routeDropState(const Context &,const Goal &) const;
    bool groundItemSeen(const Context &,std::string_view) const;
    const OnlineItem *observedDrop(const Context &,std::string_view,bool landedOnly=false) const;
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
    void waitForWorldResult(Context &,const QuestStep &,std::string_view);
    void observeWorldWait(Context &);
    bool worldResultReady(const Context &,const QuestStep &) const;
    int encounterMovementRadius() const;
    bool encounterEnemy(const Context &,const OnlineUnit &) const;
    bool tacticalPositionAllowed(const Context &,Vec) const;
    bool tacticalPathSafe(const Context &,const std::deque<Vec> &,Vec,const std::vector<VisualThreat> &) const;
    std::optional<OnlinePoint> encounterStance(Context &,OnlinePoint);
    void refreshQuestNpc(Context &);
    std::optional<uint16_t> questFlags(const Context &,QuestId) const;
    bool questFinished(const Context &,const Goal &) const;
    void observeQuestInteraction(Context &,const Json &);
    std::optional<OnlineItem> accessibleItem(const Context &,std::string_view) const;
    bool acquireItem(Context &,const Goal &);
    bool approachPickup(Context &,const OnlineItem &);
    bool settleItemInput(Context &);
    Clock::time_point nextArmorAttempt{};
    bool maintainArmor(Context &);
    bool knownSkill(const Context &,uint16_t,bool left=false) const;
    struct TeleportRooms {
        const std::vector<RemoteTown::NavigationRoom> &rooms;
        const RemoteTown::NavigationRoom *current{};
        TeleportRooms(const Context &);
        bool contains(Vec) const;
    };
    bool teleportAvailable(const Context &,bool resourceReady=true) const;
    bool selectSkill(Context &,uint16_t,OnlineSkillHand);
    std::optional<uint16_t> battleAura(const Context &) const;
    std::vector<Vec> travelLandingThreats(const Context &) const;
    bool crowdedTravelLanding(std::span<const Vec>,Vec) const;
    std::optional<Vec> teleportLanding(Context &,Vec wanted,bool exploration,std::optional<OnlinePoint> relayGoal={}) const;
    bool mobility(Context &);
    void stop(DebugAutoWalk &,RemoteControl &,RemoteCombat &);
    void finish(std::string,std::string,Context &);
    void advance(Context &,std::string,bool skipped=false);
    void resetStep();
    void cancelWalk(Context &);
    bool walk(Context &,Json);
    bool prepareNpcService(Context &, OnlineUnitKey, DebugActionOwner);
    bool walkPoint(Context &,OnlinePoint,bool exploration=false);
    void observeChild(Context &,const Goal &);
    void rememberTravel(Context &,OnlineUnitKey);
    bool recoverTravel(Context &);
    void observeActions(Context &);
    void observeQuestResult(Context &);
    bool observeMobility(Context &);
    bool questWaitRequiresInput(const Context &) const;
    std::vector<DebugActionContract> actionContracts(const Context &) const;
    bool canInput(const Context &,DebugActionOwner,unsigned) const;
    void describeActions(const Context &);
    std::string walkState(const Context &) const;
    void wait(std::string p,std::string why) {phaseReported=true;phase=std::move(p);reason=std::move(why);}
    std::vector<int> route(const Context &,int from,int to) const;
    float navigationCost(const Context &,int level,std::optional<Vec> start,std::optional<OnlinePoint> target) const;
    bool exploreLocalPad(Context &);
    void observeLocalPad(Context &);
    float routeCost(const Context &,const std::vector<int> &,std::optional<Vec> start={}) const;
    void refreshNavigationAttempts(const Context &);
    void recordFrontierAttempt(const Context &,OnlinePoint,std::string outcome,std::string reason={},std::optional<OnlinePoint> relayGoal={});
    void deferViewpoint(const Context &,OnlinePoint);
    bool frontierCoolingDown(OnlinePoint,float radius,std::optional<OnlinePoint> relayGoal={}) const;
    bool tryTeleportAcrossExit(Context &,uint16_t);
    bool travel(Context &,uint16_t,bool allowWaypoint=true);
    bool explore(Context &,std::string,bool recoveryOnly=false);
    bool approachKnown(Context &,const Selector &);
    bool approachPoint(Context &,OnlinePoint,std::string,float vicinity=8,bool exploration=false,bool enterUnitRange=false);
    bool openDoor(Context &,bool frontier=false);
    bool approachTownGate(Context &,uint16_t destination);
    bool activateWaypoint(Context &,bool required=false);
    bool waypointBeforeDeparture(Context &);
    void observeWaypointMemory(const Context &);
    void observePopulation(const Context &);
    std::optional<OnlineUnit> find(const Context &,const Selector &,bool corpses=false) const;
    bool owned(const Context &,std::string_view) const;
    bool combat(Context &,const Goal &,bool nearbyDefense=false,bool planningDefense=false);
    bool avoidHazards(Context &);
    void observeCombat(Context &);
    void releaseCombat(Context &);
    void execute(Context &,const Goal &);
    void acquire(Context &,const Goal &);
    void returnTown(Context &);
    void deliver(Context &,const Goal &);
};
}
