#pragma once

namespace d2x {
struct AnimationTiming {
    int frames{}, speed{};
    int durationTicks(int startFrame = 0) const;
};

int animationFrameTicks(int frame, int speed);
int monsterAnimationTicks(AnimationTiming timing, int startFrame = 0);
int animationSecondsTicks(float seconds);
int effectiveFasterRate(int itemBonus);
int effectiveCastSpeed(int animationSpeed, int fasterCast);
int effectiveRecoverySpeed(int animationSpeed, int itemBonus, bool holyShield = false);
int effectiveOtherAnimationSpeed(int animationSpeed, int otherRate);
int effectiveAttackSpeed(int animationSpeed, int itemIAS, int baseWeaponSpeed, int skillRate);
}