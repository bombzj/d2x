#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>

namespace d2x {
struct ShapeAnimation { int frames{},speed{},actionFrame{}; };
// Prepared from States/MonStats/MonStats2/AnimData. State order is native order.
struct ShapeProfile {
    int state{-1},nativeClass{-1},walkVelocity{},runVelocity{};
    uint16_t enabledModes{};
    bool restrictSkills{},meleeOnly{};
    std::string token,weapon;
    std::map<uint8_t,ShapeAnimation> animations;
};
struct SkillShapeRestriction { int mode{}; std::array<int,3> states{-1,-1,-1}; };
}
