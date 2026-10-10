#pragma once
#include <array>
namespace d2x {
struct GrimWardSpec {
    struct Form {int start{}, startFrames{}, ward{}, stop{}, stopFrames{}, period{};};
    std::array<Form,3> forms;
    int radius{}, radiusPerLevel{}, duration{}, durationPerLevel{}, noSelectState{}, noDrawState{};
};
}
