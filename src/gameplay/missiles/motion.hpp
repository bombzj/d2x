#pragma once
#include "core/math.hpp"
#include "gameplay/units/direction.hpp"
#include <array>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <span>

namespace d2x {
struct NativeMissileMotionResult {
    Vec position;
    bool blocked{},reached{},movedCell{},pathFinished{};
};
// One missile owns its FP16 path state. Vec is a projection; room translation
// preserves the fractional coordinates and does not recalculate direction.
class NativeMissileMotion {
    int64_t x_{},y_{};
    Vec published_;
    bool initialized_{},directionReady_{};
    NativeDirection direction_;
    Vec target_;
    uint64_t revision_{};
    std::array<Vec,10> steps_{};
    size_t stepCount_{};
  public:
    enum class Path { Classic,Waypoint };
    using Collision=std::function<uint16_t(int32_t,int32_t)>;
    void reset() noexcept;
    Vec translate(Vec position,Vec offset);
    std::span<const Vec> steps() const {return {steps_.data(),stepCount_};}
    NativeMissileMotionResult advance(Vec position,Vec target,int32_t velocity,Path,uint64_t revision,const Collision &);
};
}
