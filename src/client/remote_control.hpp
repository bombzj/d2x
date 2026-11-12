#pragma once
#include "client/online_actions.hpp"
#include "client/remote_town.hpp"
#include "network/realm_session.hpp"
#include <chrono>
#include <functional>

namespace d2x {
// MPQ eligibility and command sequencing. No local movement, NPC rules or D2S authority.
class RemoteControl : public IOnlineControl {
    RemoteTown &scene_;
    net::RealmSession &session_;
    struct Approach {
        OnlineUnitKey target;
        uint64_t game{}, area{}, requestRevision{};
        std::chrono::steady_clock::time_point deadline;
        OnlineIntentContext context;
        std::optional<OnlinePoint> progressPosition;
        bool awaitServerRange{};
    };
    std::optional<Approach> approach_;
    struct Opening {
        OnlineUnitKey target;
        OnlineIntentContext context;
        uint64_t movementRevision{};
        unsigned retries{};
        std::chrono::steady_clock::time_point next, deadline;
        struct Pad {uint64_t assignment{},discontinuity{},combatSequence{};};
        std::optional<Pad> pad{};
    };
    std::optional<Opening> opening_;
    std::optional<InteractionSubmission> interactionSubmission_;
    struct Movement {
        OnlinePoint goal{};
        std::optional<Vec> requestOrigin; // UI projection hint, never a server position.
        std::optional<OnlineUnitKey> unit;
        std::optional<OnlinePoint> segment;
        bool run{}, finalUnit{};
        uint64_t game{}, area{}, requestRevision{};
        std::chrono::steady_clock::time_point submitted, deadline;
        OnlineIntentContext context;
        std::optional<OnlinePoint> progressPosition;
    };
    std::optional<Movement> movement_;
    uint64_t verificationGame_{}, verificationArea_{}, verificationRevision_{};
    std::string reason_;
    uint64_t questRefreshConnection_{}, questRefreshGame_{}, questRefreshRevision_{}, questRefreshWorldRevision_{};
    std::chrono::steady_clock::time_point questRefreshDeadline_{};
    std::function<std::optional<OnlinePoint>(OnlineUnitKey)> npcPathPosition_;
    std::function<void(OnlineUnitKey)> stopNpcPath_;
    std::function<std::optional<int>(OnlineUnitKey)> npcFollowRange_;
    std::function<std::optional<uint8_t>()> playerMovementMode_;
    bool reject(std::string reason);
    bool submitSegment(Movement &, std::optional<OnlinePoint> stopNpc = {});
    bool submitInteraction(OnlineUnitKey, const OnlineIntentContext &, std::optional<OnlinePoint> stopNpc = {});
    void observeOpening();
  public:
    RemoteControl(RemoteTown &scene, net::RealmSession &session) : scene_(scene), session_(session) {}
    void bindPlayerMovementMode(std::function<std::optional<uint8_t>()> mode) { playerMovementMode_=std::move(mode); }
    bool setRunning(bool run, const OnlineIntentContext &context) override {
        return session_.set_running_animation(run,context,playerMovementMode_?playerMovementMode_():std::nullopt);
    }
    void bindNpcPath(std::function<std::optional<OnlinePoint>(OnlineUnitKey)> position,
                     std::function<void(OnlineUnitKey)> stop,
                     std::function<std::optional<int>(OnlineUnitKey)> followRange) {
        npcPathPosition_ = std::move(position);
        stopNpcPath_ = std::move(stop);
        npcFollowRange_ = std::move(followRange);
    }
    bool move(OnlinePoint, bool run, std::optional<OnlineIntentContext> context = {}, std::optional<Vec> requestOrigin = {}) override;
    bool moveToUnit(OnlineUnitKey, bool run, std::optional<OnlineIntentContext> context = {},
                    std::optional<Vec> displayOrigin = {}, std::optional<OnlinePoint> stopNpc = {}) override;
    bool interact(OnlineUnitKey, bool run, std::optional<OnlineIntentContext> context = {},
                  std::optional<Vec> displayOrigin = {}, bool followUntilServerArrival = false) override;
    bool townPortal() override;
    const OnlineQuestState &quests() const override { return session_.read().world.quests; }
    // Reuse a private snapshot newer than the caller's observed revision;
    // otherwise join/request a refresh. Accepted is not completion.
    bool refreshQuests(std::optional<uint64_t> newerThan = {}) override;
    bool closeNpc(std::optional<OnlineIntentContext> context = {}) override;
    bool acknowledgeNpc(uint16_t topic, std::optional<OnlineIntentContext> context = {},
                        bool dismissAfterSpeech = false) override;
    bool acknowledgeNpcGreeting(uint16_t identity,std::optional<OnlineIntentContext> context = {}) override;
    bool npcTravel(uint32_t destination,std::optional<OnlineIntentContext> context = {}) override;
    bool useWaypoint(uint16_t destination,uint8_t number = 0,std::optional<OnlineIntentContext> context = {}) override;
    void cancelApproach() override {
        if (approach_) movement_.reset();
        approach_.reset();
    }
    void cancelMovement() override { movement_.reset(); approach_.reset(); }
    void tick(std::optional<Vec> displayOrigin = {}, std::optional<Vec> pathPosition = {}, bool actionBusy = false);
    const std::string &reason() const override { return reason_; }
    const std::optional<InteractionSubmission> &interactionSubmission() const override {return interactionSubmission_;}
    std::optional<OnlineIntentContext> intentContext() const override {
        if (approach_) return approach_->context;
        return movement_ ? std::optional{movement_->context} : std::nullopt;
    }
    std::optional<OnlineUnitKey> approaching() const override {
        return approach_ ? std::optional{approach_->target} : std::nullopt;
    }
    bool awaitingServerArrival() const override {return approach_ && approach_->awaitServerRange;}
    std::optional<OnlinePoint> movementGoal() const override {
        return movement_ ? std::optional{movement_->goal} : std::nullopt;
    }
    std::optional<OnlinePoint> movementSegment() const override {
        return movement_ ? movement_->segment : std::nullopt;
    }
    std::optional<OnlineUnitKey> movementTarget() const override {
        return movement_ ? movement_->unit : std::nullopt;
    }
};
} // namespace d2x
