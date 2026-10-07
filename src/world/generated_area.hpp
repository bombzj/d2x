#pragma once
#include "world/native_map.hpp"

namespace d2x {
struct AreaGenerationRequest {
    uint32_t seed{};
    int act{}, level{1}, difficulty{};
};
struct GeneratedArea {
    AreaGenerationRequest request;
    Vec origin;
    int palette{};
    std::vector<std::pair<int, int>> rooms; // Original tile anchors, activation order.
    std::shared_ptr<const Map> map;
};
// Host preparation uses the same generator/reveal operations as RemoteTown.
// The returned Map retains every referenced DT1 library beyond generator/cache
// destruction. Dynamic actors/objects and activation belong to the caller.
GeneratedArea generateArea(Archives &, AreaGenerationRequest);
} // namespace d2x
