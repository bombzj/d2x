#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include <deque>
#include <functional>
#include <optional>

namespace d2x {
struct HirelingControlView {
    EntityId id;
    Vec &pos, &look;
    std::deque<Vec> &route;
    bool &moving;
    float &thinkTimer, &animationTime, &animationRate;
    int &attackBias;
    uint64_t &combatRandom;
    int level;
    const float &webSlowRemaining;
    const int &webSlowPercent;
};
struct HirelingOwnerView { Vec pos; bool moving = false, runningNow = false; };
struct HirelingMovementRules {
    int walkVelocity = 0, walkAnimationRate = 0, fasterMoveVelocity = 0, velocityPercent = 0;
    bool melee = false;
    int meleeReach = 1;
};
struct HirelingControlTarget { EntityId id; Vec pos; int distance = 0; };
// Synchronous authority capabilities; none are retained after control returns.
struct HirelingControlWorld {
    std::function<bool(Vec)> open;
    std::function<bool(Vec, Vec)> segment;
    std::function<std::deque<Vec>(Vec, Vec)> path;
    std::function<int(int)> movementRate;
    std::function<std::optional<float>()> idleAnimationRate;
    std::function<std::optional<HirelingControlTarget>()> target;
    std::function<void(EntityId, Vec)> beginAttack;
};
// Follow/retreat/idle/attack decisions use only existing borrowed capabilities
// and resolved inputs. Content lookup and attack execution stay in the adapter.
void advanceHirelingControl(HirelingControlView actor, HirelingOwnerView owner,
                           HirelingMovementRules rules, float dt, const HirelingControlWorld &world);
} // namespace d2x
