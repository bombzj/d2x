#pragma once
#include <array>
#include <string>
namespace d2x {
struct HydraHeadSpec { std::string code; int nativeClass{}, attackTicks{}, impactTick{}, riseTicks{}, deathTicks{}, attackSkill{}; };
struct HydraSpec { std::array<HydraHeadSpec,3> heads; };
}
