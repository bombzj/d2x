#pragma once
#include "client/remote_town.hpp"
#include "network/realm_session.hpp"
#include <chrono>

namespace d2x {
// MPQ eligibility and command sequencing. No local movement, NPC rules or D2S authority.
class RemoteControl {
    RemoteTown &scene_;
    net::RealmSession &session_;
    struct Approach {
        OnlineUnitKey target;
        uint64_t game{}, area{}, requestRevision{};
        std::chrono::steady_clock::time_point deadline;
        OnlineIntentContext context;
    };
    std::optional<Approach> approach_;
    struct Movement {
        OnlinePoint goal{};
        std::optional<OnlineUnitKey> unit;
        std::optional<OnlinePoint> segment;
        bool run{}, finalUnit{};
        uint64_t game{}, area{}, requestRevision{};
        std::chrono::steady_clock::time_point submitted, deadline;
        OnlineIntentContext context;
    };
    std::optional<Movement> movement_;
    std::string reason_;
    bool reject(std::string reason);
    bool submitSegment(Movement &);
  public:
    RemoteControl(RemoteTown &scene, net::RealmSession &session) : scene_(scene), session_(session) {}
    bool move(OnlinePoint, bool run, std::optional<OnlineIntentContext> context = {});
    bool moveToUnit(OnlineUnitKey, bool run, std::optional<OnlineIntentContext> context = {});
    bool interact(OnlineUnitKey, bool run, std::optional<OnlineIntentContext> context = {});
    bool townPortal();
    void cancelApproach() {
        if (approach_ && movement_ && movement_->unit == approach_->target) movement_.reset();
        approach_.reset();
    }
    void cancelMovement() { movement_.reset(); approach_.reset(); }
    void tick();
    const std::string &reason() const { return reason_; }
    std::optional<OnlineIntentContext> intentContext() const {
        if (approach_) return approach_->context;
        return movement_ ? std::optional{movement_->context} : std::nullopt;
    }
    std::optional<OnlineUnitKey> approaching() const {
        return approach_ ? std::optional{approach_->target} : std::nullopt;
    }
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
