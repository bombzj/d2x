#pragma once

namespace d2x {
inline constexpr float gameFixedStep = 1.f / 25.f;
// The host chooses whether world time runs; rules receive one step at a time.
class FixedStepClock {
    float accumulated_ = 0;
  public:
    void reset() { accumulated_ = 0; }
    void add(float elapsed) { accumulated_ += elapsed; }
    bool ready() const { return accumulated_ >= gameFixedStep; }
    void consume() { accumulated_ -= gameFixedStep; }
};
} // namespace d2x
