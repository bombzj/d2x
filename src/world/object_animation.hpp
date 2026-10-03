#pragma once
namespace d2x {
struct ObjectAnimationRule {
    int frames = 1, start = 0;
    float fps = 0;
    bool cycle = false, enabled = false;
};
} // namespace d2x
