#pragma once

namespace d2x {
struct MovementTiming {
    int velocity{}; // Native 8.8 path velocity.
    int animationRate{}; // Native 8.8 frames per game tick.
    int percentage{};
};
MovementTiming nativeMovementTiming(int baseVelocity,int baseAnimationRate,int percentage);
// D2Common UNITS_UpdateRunWalkAnimRateAndVelocity. Inputs are prepared MPQ
// velocities and observed modifiers; the temporary run bonus is applied here.
MovementTiming playerMovementTiming(int walkVelocity,int runVelocity,
    int fasterMoveVelocity,int velocityPercent,bool running);
} // namespace d2x
