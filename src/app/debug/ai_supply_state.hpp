#pragma once
#include "contracts/online_world.hpp"
#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace d2x::debug_ai {
enum class PotionKind {Life,Mana,Rejuvenation};
struct SupplyState {
    using Clock=std::chrono::steady_clock;
    using Json=nlohmann::json;
    struct PotionRequest {
        bool buying{},portalSupply{},quiverSupply{};uint32_t item{};uint64_t sequence{},tradeRevision{};std::string code;
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
    struct QuiverTransfer {
        uint32_t item{};
        uint8_t body{};
        std::optional<uint64_t> sequence;
        bool equipping{};
        Clock::time_point deadline{};
    };
    std::optional<QuiverTransfer> quiverTransfer;
    bool quiverTopUpDeferred{};
    Json lastPotion,supplyVitals;
    void resetPlan();
    void suspend(Clock::duration);
};
} // namespace d2x::debug_ai
