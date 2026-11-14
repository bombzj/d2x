#pragma once
#include "contracts/online_world.hpp"
#include "core/math.hpp"
#include "client/inventory_workflow.hpp"
#include <chrono>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace d2x::debug_ai {
struct QuestState {
    using Clock=std::chrono::steady_clock;
    using Json=nlohmann::json;
    struct QuestPending {
        enum class Action {Object,Portal,Scroll,Topic,Take,Imbue,Place,Travel,Pickup,Use,OpenCube,StoreCube,Transmute,SubmitStaff,CloseCube,Service,RecoverOpen,RecoverTake,RecoverPlace,RecoverClose} action{};
        std::optional<OnlineUnitKey> unit;uint64_t assignment{},sequence{},beforeRevision{};
        uint32_t item{};Clock::time_point deadline{};
        bool refreshIssued{},statusRequested{},resultObserved{};
        // A sent object 13 may still own a host-side approach. Its first
        // observed effect releases motion before any longer quest/world wait.
        bool operationPending{};std::optional<uint8_t> operationMode;
    };
    // Passive scene observation is not a submitted operation. It may be
    // interrupted, but only relevant new facts renew its inactivity budget.
    struct WorldWait {
        uint16_t area{};Vec anchor;float vicinity=8;Clock::time_point deadline{};
        uint64_t soundSequence{},clearanceRevision{1};int residualRange{},witnessSize{};
        unsigned seconds{},progress{};std::string expected,lastProgress;
        struct Enemy {std::optional<uint8_t> bestLife;bool dead{};};
        std::map<std::pair<OnlineUnitKey,uint64_t>,Enemy> enemies;
    };
    struct QuestRun {
        bool initialized{},initialInventory{};size_t step{};std::optional<QuestPending> pending;
        Clock::time_point portalDeadline{};
        std::optional<uint32_t> recoveringItem;
        std::optional<WorldWait> worldWait;
        std::optional<uint16_t> denRemaining;Clock::time_point sweepReady{};
        unsigned sweeps{},materialPhase{};std::optional<uint32_t> material;
        uint64_t forgeSubmissionRevision{};bool forgeStrikingStopped{};
        std::optional<OnlineUnitKey> materialNpc;
        std::set<uint32_t> rewardBefore;
        std::set<uint32_t> materialBefore;std::optional<uint32_t> skillPointsBefore;
        bool rewardTracked{},awaitingTopicAlert{},journalVisit{};Clock::time_point missingTopicSince{};uint64_t clueBefore{};
        struct JournalReceipt {
            std::set<std::pair<uint16_t,uint16_t>> heard; // private quest word and offered line
            std::optional<uint16_t> unproductiveFlags;
        };
        std::map<size_t,JournalReceipt> journalReceipts;
        std::optional<uint64_t> missingTopicRefresh;
        std::set<size_t> observations;
        bool selected{};
        std::optional<std::pair<OnlineUnitKey,uint64_t>> encounterWitness;
        std::optional<OnlinePoint> encounterPosition;
        std::optional<OnlinePoint> encounterStandoff;
        InventoryWorkflow inventoryWork;
        Json encounterStanceDiagnostics;
        std::optional<uint16_t> encounterArea;
        bool encounterAnchored{};
        Clock::time_point witnessUntil{};
        std::set<std::pair<OnlineUnitKey,uint64_t>> approachedWitnesses;
    } questRun;
    Json questPlanDiagnostics;
    bool questLogPending{};
    uint64_t questLogGame{},questLogConnection{};
    Clock::time_point questLogCloseAt{};
    std::optional<uint16_t> tombSymbol; // Actual journal-qualified server payload, including valid initialization.
    // Present only after the summit door refused this player during this visit.
    std::optional<std::set<uint16_t>> summitTrialKills;
    std::optional<uint64_t> journalReadGame; // Confirmed reading in this process/game, never a saved portal/unlock fact.
    std::array<std::optional<uint32_t>,2> questHandsRestore;
    std::optional<std::pair<uint16_t,OnlinePoint>> questSpawnAnchor;
    void resetPlan();
    void suspend(Clock::duration,bool pendingRequiresInput);
};
} // namespace d2x::debug_ai
