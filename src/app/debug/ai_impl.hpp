#pragma once
#include "ai.hpp"
#include "client/online_actions.hpp"
#include "client/remote_town.hpp"
#include "ai_navigation_state.hpp"
#include "ai_battle_state.hpp"
#include "ai_combat_strategy.hpp"
#include "ai_supply_state.hpp"
#include "ai_quest_state.hpp"
#include "ai_behavior_log.hpp"
#include "auto_walk.hpp"
#include "action_state.hpp"
#include "content/monsters/monster_identity.hpp"
#include "resources/data_table.hpp"
#include "gameplay/quest/catalog.hpp"
#include "gameplay/rules/formula.hpp"
#include <chrono>
#include <algorithm>
#include <array>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#include <nlohmann/json.hpp>

namespace d2x {
struct DebugAi::Impl {
    using Json=nlohmann::json;
    using Clock=std::chrono::steady_clock;
    using ExplorationReturn=debug_ai::NavigationState::ExplorationReturn;
    using FrontierAttempt=debug_ai::NavigationState::FrontierAttempt;
    using ExplorationFocus=debug_ai::NavigationState::ExplorationFocus;
    using ExperienceLandmark=debug_ai::NavigationState::ExperienceLandmark;
    using AreaExperience=debug_ai::NavigationState::AreaExperience;
    using NavigationExperience=debug_ai::NavigationState::NavigationExperience;
    using FrontierRoute=debug_ai::NavigationState::FrontierRoute;
    using ApproachObservation=debug_ai::NavigationState::ApproachObservation;
    using VaughtClues=debug_ai::NavigationState::VaughtClues;
    using VaughtSearch=debug_ai::NavigationState::VaughtSearch;
    using ColdPlainsSearch=debug_ai::NavigationState::ColdPlainsSearch;
    using Passage=debug_ai::NavigationState::Passage;
    using PassageWait=debug_ai::NavigationState::PassageWait;
    using WaypointActivation=debug_ai::NavigationState::WaypointActivation;
    using WaypointDeferral=debug_ai::NavigationState::WaypointDeferral;
    using RememberedEnemy=debug_ai::NavigationState::RememberedEnemy;
    using AreaPopulation=debug_ai::NavigationState::AreaPopulation;
    using LocalPad=debug_ai::NavigationState::LocalPad;
    using PadObservation=debug_ai::NavigationState::PadObservation;
    using TravelPhase=debug_ai::NavigationState::TravelPhase;
    using TravelSubmission=debug_ai::NavigationState::TravelSubmission;
    using TeleportTimingObservation=debug_ai::NavigationState::TeleportTimingObservation;
    using RouteCombatTask=debug_ai::NavigationState::RouteCombatTask;
    using RouteExecution=debug_ai::NavigationState::RouteExecution;
    using CombatRun=debug_ai::CombatState::CombatRun;
    using LifeSample=debug_ai::CombatState::LifeSample;
    using BypassedThreat=debug_ai::CombatState::BypassedThreat;
    using PotionRequest=debug_ai::SupplyState::PotionRequest;
    using SupplyStage=debug_ai::SupplyState::SupplyStage;
    using QuiverTransfer=debug_ai::SupplyState::QuiverTransfer;
    using QuestPending=debug_ai::QuestState::QuestPending;
    using WorldWait=debug_ai::QuestState::WorldWait;
    using QuestRun=debug_ai::QuestState::QuestRun;
    enum class Kind {Travel,Find,Hunt,Acquire,ReturnTown,Deliver,Restock,Quest};
    using PotionKind=debug_ai::PotionKind;
    struct Potion {PotionKind kind{};int strength{};bool bulkBelt{};std::optional<uint8_t> state;};
    struct Selector {uint8_t type{};uint16_t identity{};std::string name;std::optional<uint16_t> superUnique{};bool eliteOnly{},npc{};};
    // Authored search knowledge, never a claim that a unit exists or floor is passable.
    struct TargetSearchKnowledge {
        std::optional<Selector> anchor;
        std::vector<uint16_t> openedObjects;
        std::optional<float> radius;
        Vec offset;
        std::optional<Vec> direction;
        std::vector<DebugExplorationMemory::Cell> candidateRooms;
        bool returnViaWaypoint{};
        unsigned waitMs{},timeoutSeconds{};
    };
    enum class QuestAction {Talk,Clear,Hunt,Acquire,ReadScroll,Stones,Operate,Portal,ReturnTown,Imbue,ActTravel,Travel,UseItem,Combine,SubmitStaff,SearchDrop,EquipWeapon,SmashOrb,SmashForge,Service,Rescue,Encounter};
    struct QuestStep {
        QuestAction action{};std::optional<uint16_t> area;std::optional<Selector> target,witness,spawnSource;
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
        std::optional<uint8_t> status,dropNoticeStatus,journalTalkStatus;
        std::optional<uint8_t> cinematic;
        unsigned resultWaitSeconds{},initialWaitMs{};int residualRange{};std::optional<uint8_t> stageSound;
        std::optional<Vec> witnessOffset;
        struct ProximityTrigger {Selector target;int distance{};};
        std::optional<ProximityTrigger> proximityTrigger;
        std::optional<TargetSearchKnowledge> search;
    };
    struct QuestFact {
        enum class Kind {Item,BackpackItem,Flags,Area,TrueTombArea,TombClue,Waypoint,Corpse,BookUsed,PortalObserved,JournalRead,ActChange,Equipped,EquipmentRestored,ObjectOpened,GroundItem,MonsterObserved,Status,Cinematic,InitiallyRewarded,CairnOrder,SummitPassage,RequirementObserved} kind{};
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
        std::optional<TargetSearchKnowledge> search;
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
        std::optional<Selector> quiverSupplier(unsigned act,std::string_view type) const;
    };
    struct CombatPolicy {
        bool enabled=true,staticField=true,avoidHazards=true;std::string scope="defensive";std::optional<uint16_t> skill,groupSkill,aura;int range=16;
        int radius=6,crowd=4,retreat=8,engage=18,waitDefenseRadius=10;
        std::vector<uint16_t> fallbackSkills;
        std::map<uint16_t,unsigned> minimumSkillLevels;
        std::map<uint16_t,int> skillRanges;
        int noProgressSeconds=6,stallTimeoutSeconds=90,retreatLifePercent=45,lifeDropPercent=10;
        bool rangedKite=true;
        int kiteIntervalMs=900,kiteDistance=6,kiteMinDistance=8,hazardLookaheadMs=350,hazardPadding=3;
    } policy;
    struct MobilityPolicy {std::string mode="auto";bool vigor=true;int distance=24;} mobilityPolicy;
    struct EventPolicy : DebugWalkTiming {
        int waitSeconds=90,initialWaitMs=1000,doorInitialWaitMs=250,scrollInitialWaitMs=1000;
        int questLogDurationMs=2000,waypointCloseDelayMs=300,npcDialogueCloseDelayMs=500;
        int encounterRadius=32,standoffMin=8,standoffMax=24;
    } eventPolicy;
    struct SupplyPolicy {
        bool enabled=true,buy=true,returnWhenEmpty=true,fillPortalBook=true;
        int lifePercent=60,manaPercent=35,criticalLifePercent=25,healthCount=4,manaCount=4,quiverCount=3,intervalMs=1500;
    } supplyPolicy;
    struct Context {
        // Direct reads of client map/collision/automap data are intentional;
        // automation need not recover that information from rendered pixels.
        const OnlineObservation &observation;const RemoteTown &town;DebugAutoWalk &walk;IOnlineControl &control;
        IOnlineCombat &combat;IOnlineInventory &inventory;Vec position;bool moving{};const SuppliesView *supplies{};
    };
    struct VisualThreat {int id{};Vec from,to;float radius{};bool unitSource{}, dangerous{};};
    std::vector<VisualThreat> visibleThreats(const Context &) const;
    static float threatClearance(std::span<const VisualThreat>,Vec);
    Archives &archives;std::unique_ptr<Definitions> definitions;
    std::vector<Goal> goals;size_t cursor{};std::string state="idle",phase,reason,configurationFile,configurationClass;
    std::deque<Json> history;uint64_t decisions{},sequence{},game{},areaGeneration{};
    std::filesystem::path logDirectory;
    AiBehaviorLog behaviorLog;
    struct NavigationEfficiency {
        double activeMs{},navigationWaitMs{},walkingDistance{};
        std::array<double,3> modeMs{};
        uint64_t relocations{},backtracks{};
        size_t observedTiles{};
    } navigationEfficiency,goalEfficiencyStart;
    Clock::time_point efficiencySampleAt{};
    bool efficiencyExecuting{},efficiencyWaiting{};
    size_t efficiencyMode{};
    std::optional<Vec> efficiencyPosition;
    std::optional<uint16_t> efficiencyArea;
    uint64_t efficiencyDiscontinuity{};
    size_t efficiencyObserved{};
    uint64_t observedWalkFailures{};
    bool phaseReported{};
    struct StallWatch {std::optional<OnlinePoint> host;std::optional<uint16_t> area;Clock::time_point since{};unsigned reports{};} stallWatch;
    std::set<OnlineUnitKey> loggedQuestAlerts;
    void observeDiagnostics(const Context &);
    void goalStarted();
    void cancel(DebugAutoWalk &,IOnlineControl &,IOnlineCombat &,std::string);
    std::optional<uint16_t> area;std::optional<OnlineUnitKey> target;
    uint64_t targetAssignment{};
    bool resumeExplorationReturn(Context &);
    bool beginExplorationReturn(Context &,OnlinePoint destination);
    NavigationExperience parseExperience(const Json &) const;
    const AreaExperience *areaExperience(const Context &) const;
    std::optional<OnlinePoint> experienceHint(const Context &,const Selector &) const;
    std::optional<OnlinePoint> experienceWaypointHint(const Context &) const;
    std::optional<DebugExplorationMemory::Cell> experienceDirection(const Context &,ExplorationFocus) const;
    using TerrainMemory=DebugExplorationMemory::Area;
    TerrainMemory *observeTerrain(const Context &);
    struct TerrainLeg {std::optional<OnlinePoint> point;size_t nearest{};bool teleport{};};
    TerrainLeg terrainLeg(const Context &,const std::vector<OnlinePoint> &path) const;
    bool frontierWalking(const Context &) const;
    bool exploreFrontier(Context &,const std::string &purpose,bool recoveryOnly,ExplorationFocus);
    struct TravelLeg {float cost{};std::optional<Vec> arrival;std::optional<OnlineUnitKey> unit;};
    TravelLeg travelLeg(const Context &,int from,int to,std::optional<Vec> start) const;
    static constexpr auto passageRetryInterval=std::chrono::seconds(1);
    static std::string_view passageName(Passage);
    PassageWait &beginPassage(Passage,std::chrono::milliseconds,Clock::time_point);
    bool retryPassage(Passage,std::string_view);
    bool servicePassage(Context &,const Goal &,bool prepared);
    Clock::time_point goalDeadline{},actionDeadline{},pausedAt{};
    bool actionWaitSubmitted{};
    void setActionDeadline(std::chrono::seconds duration,bool submitted=true) {
        actionDeadline=Clock::now()+duration;actionWaitSubmitted=submitted;
    }
    DebugExecutionClock executionClock;
    DebugDecisionCadence cadence;
    std::optional<uint16_t> decisionLife;
    struct NpcDialogueDisplay {
        OnlineIntentContext context;
        std::optional<uint16_t> speech;
        Clock::time_point openedAt;
    };
    std::optional<NpcDialogueDisplay> npcDialogueDisplay;
    void observeNpcDialogue(const OnlineView &,Clock::time_point);
    Clock::time_point npcDialogueReadyAt() const {
        return npcDialogueDisplay?npcDialogueDisplay->openedAt+std::chrono::milliseconds(eventPolicy.npcDialogueCloseDelayMs):Clock::time_point{};
    }
    bool decisionDue(const Context &,Clock::time_point);
    Json actionDiagnostics;
    std::map<DebugActionOwner,DebugSubmissionWait> submissionWaits;
    DebugSubmitResult submitInput(Context &,DebugActionOwner,std::string,unsigned,
        const std::function<bool()> &,const std::function<std::string()> &,
        std::chrono::milliseconds budget=std::chrono::seconds(12));
    bool submitItem(Context &,const OnlineItemCommand &,std::string,DebugActionOwner=DebugActionOwner::Goal);
    bool submitCombat(Context &,const OnlineCombatCommand &,DebugActionOwner,std::string,std::optional<Vec> = {});
    bool inputBlocked{};
    using CombatSight=debug_ai::CombatSight;
    using CombatSelection=debug_ai::CombatSelection;
    using AttackStrategy=debug_ai::AttackStrategy;
    using StaticFieldOpportunity=debug_ai::StaticFieldOpportunity;
    uint16_t combatAttackMask(const Context &,uint16_t) const;
    bool selectCombatTarget(Context &,const Goal &,bool nearbyDefense,bool planningDefense,
        const RouteCombatTask *,Clock::time_point,const CombatSight &,CombatSelection &);
    bool beginEngagement(Context &,const OnlineUnit &,bool planningDefense,const RouteCombatTask *,Clock::time_point);
    std::string_view attackQualification(const Context &,const OnlineUnit &,uint16_t,bool aura) const;
    void changeAttack(Context &,size_t,Clock::time_point);
    AttackStrategy selectAttackStrategy(Context &,const CombatSelection &,const RouteCombatTask *,bool boss,Clock::time_point,CombatSight &);
    StaticFieldOpportunity staticFieldOpportunity(const Context &,bool boss) const;
    bool cooldownAttack(Context &,const OnlineUnit &,const AttackStrategy &,const StaticFieldOpportunity &,
        bool ray,bool planningDefense,const RouteCombatTask *,Clock::time_point);
    bool selectFallbackAttack(Context &,const OnlineUnit &,const AttackStrategy &,Clock::time_point);
    bool submitDirectAttack(Context &,const OnlineUnit &,const AttackStrategy &,bool nearbyDefense,bool planningDefense,bool rangedKite,OnlineSkillHand,Clock::time_point);
    std::optional<OnlinePoint> selectCombatPosition(Context &,const CombatSelection &,const CombatSight &,std::span<const Vec>,bool retreat,int range);
    std::optional<OnlinePoint> selectKitePosition(Context &,const CombatSelection &,const CombatSight &,std::span<const Vec>,int range);
    void prepareCombatSkill(const Context &);
    bool meleeAttack(const Context &,uint16_t) const;
    int combatRange(const Context &,uint16_t) const;
    bool attackRankQualified(const Context &,uint16_t,bool left) const;
    bool objectIssued{},portalIssued{},portalAttempted{},portalUsed{};
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
    bool actionRecoveryBusy{};
    struct PortalStock {unsigned books{},pages{},capacity{};bool known=true;};
    PortalStock portalStock(const Context &) const;
    struct QuiverStock {
        std::string type;
        uint8_t body{};
        const OnlineItem *equipped{},*reserve{};
        std::optional<unsigned> equippedQuantity;
        unsigned stacks{},rounds{};
        bool empty() const {return body && equippedQuantity==0;}
    };
    QuiverStock quiverStock(const Context &) const;
    void observeQuiver(Context &);
    bool reloadQuiver(Context &);
    void tickQuestLog(const OnlineView &,const SuppliesView &,bool blocked);
    bool questEquipmentRestored(const Context &) const;

    // Each domain owns its pending work, clocks and diagnostics. The coordinator
    // retains goals and shared input arbitration, never mirrored domain state.
    debug_ai::NavigationState navigation;
    debug_ai::CombatState battle;
    debug_ai::SupplyState supply;
    debug_ai::QuestState questState;

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
    static std::optional<QuestId> questIdentity(std::string_view);
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
    bool finishAutomaticNpcSpeech(Context &,const Goal &);
    void trackNpcQuestReward(const OnlineWorldView &,const Goal &,const QuestStep &);
    bool questTalk(Context &,const Goal &,const QuestStep &);
    bool questJournalTalk(Context &,const QuestStep &);
    bool quietQuestNpc(Context &,const OnlineUnit &);
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
    bool serviceInventoryWork(Context &);
    void checkQuestPending(const Context &) const;
    void waitForWorldResult(Context &,const QuestStep &,std::string_view);
    void waitAtWorldEvent(Context &,const QuestStep &,OnlinePoint,std::string_view,float vicinity=8);
    bool approachWorldTrigger(Context &,const QuestStep &);
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
    void observeQuestInteraction(Context &,const DebugWalkIntent &);
    std::optional<OnlineItem> accessibleItem(const Context &,std::string_view) const;
    bool acquireItem(Context &,const Goal &);
    bool approachPickup(Context &,const OnlineItem &);
    bool settleItemInput(Context &);
    bool maintainArmor(Context &);
    bool knownSkill(const Context &,uint16_t,bool left=false) const;
    // Acquired terrain/objects and native room eligibility, without actor occupancy.
    struct TeleportTargets {
        const std::vector<RemoteTown::NavigationRoom> &rooms;
        const RemoteTown::NavigationRoom *current{};
        std::shared_ptr<const Grid> terrain;
        Vec offset;
        TeleportTargets(const Context &);
        bool contains(Vec) const;
        bool permits(const Context &,Vec,int range) const;
    };
    bool teleportAvailable(const Context &,bool resourceReady=true) const;
    std::pair<int,bool> travelMobility(const Context &,int level,bool resourceReady=true) const;
    std::optional<float> kiteWalkSeconds(const Context &,Vec destination) const;
    bool selectSkill(Context &,uint16_t,OnlineSkillHand);
    std::optional<uint16_t> battleAura(const Context &) const;
    std::vector<Vec> travelLandingThreats(const Context &) const;
    bool crowdedTravelLanding(std::span<const Vec>,Vec) const;
    Json routeExecutionSnapshot() const;
    bool beginTeleportTransit(Context &,OnlinePoint);
    bool beginWalkingPassage(Context &);
    bool beginCombatEntry(Context &,const OnlineUnit &);
    bool resumeRouteExecution(Context &,const Goal &);
    int routeAttackRange(const Context &) const;
    std::optional<OnlinePoint> selectCombatLanding(const Context &,OnlinePoint,const RouteCombatTask &,bool transit) const;
    struct TeleportApproach {Vec landing;bool completeRoute{};};
    std::optional<TeleportApproach> teleportLanding(Context &,Vec wanted,bool exploration,bool directOnly=false) const;
    bool recoverExplorationLanding(Context &,const DebugExplorationMemory::Area &);
    bool submitMobilityCast(Context &,Vec wanted,Vec landing,bool teleport,float progress);
    bool mobility(Context &);
    void stop(DebugAutoWalk &,IOnlineControl &,IOnlineCombat &);
    void finish(std::string,std::string,Context &);
    void advance(Context &,std::string,bool skipped=false);
    void resetStep();
    void suspendExecution(Context &,Clock::duration);
    void observeArea(Context &,Clock::time_point);
    void prepareDecision(Context &);
    bool recoverSessionInput(Context &,Clock::time_point);
    void dispatchActions(Context &,const Goal &,Clock::time_point);
    void cancelWalk(Context &);
    bool walk(Context &,DebugWalkIntent);
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
    void wait(std::string p,std::string why) {phaseReported=true;phase=std::move(p);reason=std::move(why);}
    std::vector<int> route(const Context &,int from,int to,std::optional<Vec> start={}) const;
    float navigationCost(const Context &,int level,std::optional<Vec> start,std::optional<OnlinePoint> target) const;
    bool exploreLocalPad(Context &);
    bool followLocalPadRoute(Context &,const DebugExplorationMemory::Plan &);
    void observeLocalPad(Context &);
    float routeCost(const Context &,const std::vector<int> &,std::optional<Vec> start={}) const;
    void refreshNavigationAttempts(const Context &);
    void recordFrontierAttempt(const Context &,OnlinePoint,std::string outcome,std::string reason={},std::optional<OnlinePoint> relayGoal={});
    bool frontierCoolingDown(OnlinePoint,float radius,std::optional<OnlinePoint> relayGoal={}) const;
    bool tryTeleportAcrossExit(Context &,uint16_t);
    bool approachWalkingConnection(Context &,uint16_t);
    bool travel(Context &,uint16_t,bool allowWaypoint=true);
    bool explore(Context &,std::string,bool recoveryOnly=false,ExplorationFocus=ExplorationFocus::Coverage);
    bool approachKnown(Context &,const Selector &);
    TargetSearchKnowledge parseSearchKnowledge(const Json &,TargetSearchKnowledge) const;
    void configureSearchKnowledge(std::vector<Goal> &,const Json &) const;
    void observeSearchEvents(const Context &);
    bool searchTrigger(Context &,const TargetSearchKnowledge &,Clock::time_point &,uint64_t &);
    void searchTarget(Context &,const Selector &,const TargetSearchKnowledge *knowledge=nullptr);
    void searchArea(Context &,std::string key,std::string purpose,const TargetSearchKnowledge &,
                    std::optional<Vec> center={},Clock::time_point readyAt={},uint64_t event=0,bool awaitingAppearance=false,bool coverageOnly=false);
    void searchWorldTarget(Context &,const QuestStep &,OnlinePoint);
    void targetSearchObserved(Context &);
    void prepareVaughtClues();
    bool approachTerrainClue(Context &,const Selector &);
    bool observeColdPlainsWaypoint(Context &);
    bool approachPoint(Context &,OnlinePoint,std::string,float vicinity=8,bool exploration=false,std::optional<OnlineUnitKey> interaction={},
        std::optional<std::vector<DebugExplorationMemory::Cell>> stances={});
    bool approachEnemy(Context &,const OnlineUnit &,std::string,int attackRange=0);
    bool openDoor(Context &,bool frontier=false);
    bool approachTownGate(Context &,uint16_t destination);
    bool activateWaypoint(Context &,bool required=false);
    bool waypointBeforeDeparture(Context &);
    void observeWaypointMemory(const Context &);
    void observePopulation(const Context &);
    std::optional<OnlineUnit> find(const Context &,const Selector &,bool corpses=false) const;
    bool owned(const Context &,std::string_view) const;
    bool defendWhileWaiting(Context &,const Goal &);
    bool combat(Context &,const Goal &,bool nearbyDefense=false,bool planningDefense=false,
        const RouteCombatTask *task=nullptr);
    bool avoidHazards(Context &);
    void observeCombat(Context &);
    void releaseCombat(Context &);
    void execute(Context &,const Goal &);
    void acquire(Context &,const Goal &);
    void returnTown(Context &);
    void deliver(Context &,const Goal &);
};
}
