#pragma once
#include "client/remote_town.hpp"
#include "network/realm_session.hpp"
#include <chrono>

namespace d2x {
// MPQ eligibility and command sequencing. No local movement, NPC rules or D2S authority.
class RemoteControl {
  public:
    struct InteractionSubmission {
        OnlineUnitKey target;
        OnlineIntentContext context;
        uint64_t revision{},discontinuity{};
        OnlinePoint position{};
    };
  private:
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
    bool reject(std::string reason);
    bool submitSegment(Movement &);
    bool submitInteraction(OnlineUnitKey, const OnlineIntentContext &);
    void observeOpening();
  public:
    RemoteControl(RemoteTown &scene, net::RealmSession &session) : scene_(scene), session_(session) {}
    bool move(OnlinePoint, bool run, std::optional<OnlineIntentContext> context = {}, std::optional<Vec> requestOrigin = {});
    bool moveToUnit(OnlineUnitKey, bool run, std::optional<OnlineIntentContext> context = {},
                    std::optional<Vec> displayOrigin = {});
    bool interact(OnlineUnitKey, bool run, std::optional<OnlineIntentContext> context = {},
                  std::optional<Vec> displayOrigin = {}, bool followUntilServerArrival = false);
    bool townPortal();
    void cancelApproach() {
        if (approach_) movement_.reset();
        approach_.reset();
    }
    void cancelMovement() { movement_.reset(); approach_.reset(); }
    void tick(std::optional<Vec> displayOrigin = {}, std::optional<Vec> pathPosition = {}, bool actionBusy = false);
    const std::string &reason() const { return reason_; }
    const std::optional<InteractionSubmission> &interactionSubmission() const {return interactionSubmission_;}
    std::optional<OnlineIntentContext> intentContext() const {
        if (approach_) return approach_->context;
        return movement_ ? std::optional{movement_->context} : std::nullopt;
    }
    std::optional<OnlineUnitKey> approaching() const {
        return approach_ ? std::optional{approach_->target} : std::nullopt;
    }
    bool awaitingServerArrival() const {return approach_ && approach_->awaitServerRange;}
    std::optional<OnlinePoint> movementGoal() const {
        return movement_ ? std::optional{movement_->goal} : std::nullopt;
    }
    std::optional<OnlinePoint> movementSegment() const {
        return movement_ ? movement_->segment : std::nullopt;
    }
    std::optional<OnlineUnitKey> movementTarget() const {
        return movement_ ? movement_->unit : std::nullopt;
    }
};
} // namespace d2x
