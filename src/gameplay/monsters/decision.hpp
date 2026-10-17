#pragma once
#include <cstdint>
namespace d2x {
enum class MonsterDecisionAction { Idle, Approach, Attack, Wander, Circle, Retreat, Shout, Special };
struct MonsterDecisionInput {
    bool contact{}, charged{};
    int distance{}, difficulty{};
    uint64_t random{};
    bool retaliating{}, burialGrounds{};
    int lifePercent{100};
    bool commanded{}, alerted{}, leader{};
    int phase{}, loop{}, targetLifePercent{100};
    bool webActive{}, clear{};
    bool retreatBlocked{};
    bool trapAxisAligned{};
    int upgradeCooldown{};
    bool canShoot{};
    int shootDistance{INT32_MAX};
    bool hasTarget{true};
    int continuation{};
    uint8_t skillMask{};
    bool hasCommand{};
};
struct MonsterDecision {
    MonsterDecisionAction action{MonsterDecisionAction::Idle};
    int waitFrames{}, velocityPercent{75}, stopDistance{};
    bool running{}, charged{};
    uint64_t random{};
    uint8_t attackMode{4};
    bool alerted{};
    int approachRadius{};
    int phase{}, loop{}, skillSlot{-1};
    bool attackWhenMoveFails{};
    uint8_t failedMoveAttackMode{4};
    int failedMoveWanderRadius{}, targetDistance{};
    bool commandParty{};
    bool bloodRavenCloseWhenMoveFails{};
    int upgradeCooldown{};
    int pathSteps{}; // Native PATH_SetStepNum arrival bound, not route length.
    bool useShootableTarget{};
    bool wanderAroundTarget{};
    int failedEscapeContinuation{};
    int pathDistance{5};
    uint8_t pathType{13};
};
}
