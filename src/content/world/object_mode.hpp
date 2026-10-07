#pragma once
#include "resources/data_table.hpp"
#include <optional>

namespace d2x {
struct ObjectPresentation {
    int mode = 0;
    float elapsed = 0;
    bool draw = true;
};
// ObjMode's ENDANIM writes mode 2 directly, without refreshing the unit for
// another 0x0E packet. Share this presentation deadline with collision binding;
// it never changes the server replica or executes an operate function.
inline std::optional<float> objectEndAnimation(const DataTable &objects, size_t row, int mode) {
    if (mode != 1 || !objects.number(row, "Mode2").value_or(0)) return {};
    switch (objects.number(row, "OperateFn").value_or(0)) {
    case 2:
        if (objects.number(row, "CycleAnim1").value_or(0)) return {};
        break;
    case 1: case 3: case 4: case 5: case 7: case 14: case 23:
    case 30: case 48: case 51: case 68:
        break;
    default:
        // In particular, spike traps (11) reset through their own server event.
        return {};
    }
    const auto frames = objects.number(row, "FrameCnt1");
    return frames && *frames > 0 ? std::optional<float>{(float(*frames) + 1.f) / 25.f}
                                : std::nullopt;
}
inline int objectDisplayMode(const DataTable &objects, size_t row, int mode, float elapsed) {
    const auto end = objectEndAnimation(objects, row, mode);
    return end && elapsed >= *end ? 2 : mode;
}
inline ObjectPresentation objectPresentation(const DataTable &objects, size_t row, int mode, float elapsed) {
    const auto end = objectEndAnimation(objects, row, mode);
    const bool opened = end && elapsed >= *end;
    return {opened ? 2 : mode, opened ? elapsed - *end : elapsed,
            objects.number(row, "Draw").value_or(0) != 0};
}
} // namespace d2x
