#pragma once
#include "gameplay/combat/attack_timing.hpp"
#include <array>
#include <string>
#include <vector>
#include <cstdint>
namespace d2x {
struct HydraHeadSpec {
    std::string code;
    int nativeClass{},attackTicks{},impactTick{},riseTicks{},deathTicks{},attackSkill{};
    AnimationTiming attackAnimation;
    int actionFrame{};
    std::vector<uint8_t> animationFlags;
};
struct HydraSpec { std::array<HydraHeadSpec,3> heads; };
}
