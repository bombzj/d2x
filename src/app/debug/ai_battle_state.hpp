#pragma once
#include "contracts/online_world.hpp"
#include "core/math.hpp"
#include <chrono>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>
#include <nlohmann/json.hpp>

namespace d2x::debug_ai {
struct CombatState {
    using Clock=std::chrono::steady_clock;
    using Json=nlohmann::json;
    std::optional<OnlineUnitKey> combatTarget;std::optional<OnlinePoint> retreatPoint;
    std::optional<uint16_t> excursionSkill;
    bool excursionSkillPrepared{};
    struct CombatRun {
        bool entryAssessed{};
        uint64_t assignment{};size_t skillIndex{};
        std::vector<uint16_t> skills;
        std::optional<uint8_t> bestLife,lastLife;
        Clock::time_point progressAt{},strategyAt{},approachUntil{},nextCast{},suspendedAt{};
        Clock::time_point delayObservedAt{};bool waitingForDelay{};
        bool repositioned{},repeating{},healingObserved{},groupExhausted{};unsigned casts{},strategyCasts{},staticCasts{},hits{};
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
    bool combatOwned{};
    Clock::time_point lastAttack{},retreatUntil{},retreatReady{};
    Clock::time_point nextArmorAttempt{};
    void resetStep();
    void suspend(Clock::duration);
};
} // namespace d2x::debug_ai
