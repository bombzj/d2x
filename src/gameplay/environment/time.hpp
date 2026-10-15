#pragma once
#include "gameplay/stats/stat_list.hpp"
namespace d2x {
// The server's act environment, not a wall-clock or a character save field.
struct NativeActEnvironment {
    int cycle{2},period{},ticks{},rate{128},previousDegrees{};
    bool eclipse{};
};
// Original 1.13c ENVIRONMENT_UpdateTicks/UpdateCycleIndex. True denotes the
// native item-refresh/environment-notification boundary, not every frame.
bool advanceActEnvironment(NativeActEnvironment &,int act);
StatTimeContext actStatTime(const NativeActEnvironment &);
void setActSunEclipse(NativeActEnvironment &,bool);
}
