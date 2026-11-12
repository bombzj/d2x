#pragma once
#include "contracts/online.hpp"
#include "core/math.hpp"
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace d2x {
// Read-only access to the current client snapshot, including request receipts
// after a functional call. The transport/session itself is never exposed.
class OnlineObservation {
    std::function<const OnlineView &()> read_;
  public:
    explicit OnlineObservation(std::function<const OnlineView &()> read) : read_(std::move(read)) {}
    const OnlineView &read() const { return read_(); }
};
struct Grid;
struct EquipmentDefinition;
struct OnlineCombatSkillView {
    uint16_t id{}, base{}, bonus{}, level{};
    std::string name;
    bool left{}, passive{}, inTown{}, classSkill{}, innate{};
};
struct OnlineCombatStateView {
    uint8_t id{};
    std::string name;
    std::vector<OnlineItemStat> stats;
};
struct OnlineCombatUnitStates {
    uint64_t sequence{}, generation{};
    bool decoded{};
    std::string reason;
    std::map<uint8_t, OnlineCombatStateView> states;
    struct Death {uint64_t revision{};bool shatter{},frozen{},uberMinion{};};
    std::optional<Death> death;
};

// Input capabilities shared by manual controls and automation. Implementations
// own eligibility and continuation; acceptance never means server completion.
// Replica updates, transport, animation clocks and resolver binding are not
// input capabilities and must remain with the frontend/client adapters.
class IOnlineControl {
  public:
    struct InteractionSubmission {
        OnlineUnitKey target;
        OnlineIntentContext context;
        uint64_t revision{},discontinuity{},combatSequence{};
        OnlinePoint position{};
        uint64_t targetAssignment{};std::optional<uint8_t> targetMode;
    };
    virtual ~IOnlineControl() = default;
    virtual bool setRunning(bool run, const OnlineIntentContext &context) = 0;
    virtual bool move(OnlinePoint, bool run, std::optional<OnlineIntentContext> context = {},
                      std::optional<Vec> requestOrigin = {}) = 0;
    virtual bool moveToUnit(OnlineUnitKey, bool run, std::optional<OnlineIntentContext> context = {},
                            std::optional<Vec> displayOrigin = {}, std::optional<OnlinePoint> stopNpc = {}) = 0;
    virtual bool interact(OnlineUnitKey, bool run, std::optional<OnlineIntentContext> context = {},
                          std::optional<Vec> displayOrigin = {}, bool followUntilServerArrival = false) = 0;
    virtual bool townPortal() = 0;
    virtual const OnlineQuestState &quests() const = 0;
    virtual bool refreshQuests(std::optional<uint64_t> newerThan = {}) = 0;
    virtual bool closeNpc(std::optional<OnlineIntentContext> context = {}) = 0;
    virtual bool acknowledgeNpc(uint16_t topic, std::optional<OnlineIntentContext> context = {},
                                bool dismissAfterSpeech = false) = 0;
    virtual bool acknowledgeNpcGreeting(uint16_t identity, std::optional<OnlineIntentContext> context = {}) = 0;
    virtual bool npcTravel(uint32_t destination, std::optional<OnlineIntentContext> context = {}) = 0;
    virtual bool useWaypoint(uint16_t destination, uint8_t number = 0, std::optional<OnlineIntentContext> context = {}) = 0;
    virtual void cancelApproach() = 0;
    virtual void cancelMovement() = 0;
    virtual const std::string &reason() const = 0;
    virtual const std::optional<InteractionSubmission> &interactionSubmission() const = 0;
    virtual std::optional<OnlineIntentContext> intentContext() const = 0;
    virtual std::optional<OnlineUnitKey> approaching() const = 0;
    virtual bool awaitingServerArrival() const = 0;
    virtual std::optional<OnlinePoint> movementGoal() const = 0;
    virtual std::optional<OnlinePoint> movementSegment() const = 0;
    virtual std::optional<OnlineUnitKey> movementTarget() const = 0;
};

class IOnlineCombat {
  public:
    virtual ~IOnlineCombat() = default;
    virtual bool submit(OnlineCombatCommand, std::optional<Vec> inputOrigin = {},
                        std::optional<OnlineSkillSelection> expectedSelection = {}) = 0;
    virtual bool teleportPathAllowed(uint16_t level, const Grid &, Vec from, Vec to) const = 0;
    virtual bool hostile(const OnlineUnit &) const = 0;
    virtual bool hostileSource(const OnlineUnit &) const = 0;
    virtual std::optional<bool> knownHostileSource(const OnlineUnit &) const = 0;
    virtual std::optional<uint16_t> monsterClass(const OnlineUnit &) const = 0;
    virtual std::optional<uint16_t> superUnique(const OnlineUnit &) const = 0;
    virtual bool eliteMonster(const OnlineUnit &) const = 0;
    virtual bool jadeFigurineCandidate(const OnlineUnit &) const = 0;
    virtual bool monsterTargetEligible(const OnlineUnit &, bool targetCorpse) const = 0;
    virtual bool skillTargetEligible(const OnlineUnit &, uint16_t skill) const = 0;
    virtual bool skillTargetEligible(const OnlineItem &, uint16_t skill) const = 0;
    virtual std::optional<int> skillDelayFrames(uint16_t skill, uint32_t owner = UINT32_MAX) const = 0;
    virtual bool skillDelayActive(uint16_t skill, uint32_t owner = UINT32_MAX) const = 0;
    virtual const std::vector<OnlineCombatSkillView> &skills() const = 0;
    virtual const std::map<OnlineUnitKey, OnlineCombatUnitStates> &states() const = 0;
    virtual const std::string &reason() const = 0;
};

class IOnlineInventory {
  public:
    virtual ~IOnlineInventory() = default;
    virtual const OnlineInventoryView &read() const = 0;
    virtual EquipmentDefinition equipmentDefinition(const OnlineItem &) const = 0;
    virtual const std::string &reason() const = 0;
    virtual bool submit(OnlineItemCommand) = 0;
};
} // namespace d2x
