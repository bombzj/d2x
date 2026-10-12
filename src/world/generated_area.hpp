#pragma once
#include "world/native_map.hpp"
#include <chrono>

namespace d2x {
struct AreaGenerationRequest {
    uint32_t seed{};
    int act{}, level{1}, difficulty{};
};
struct GeneratedArea {
    AreaGenerationRequest request;
    Vec origin;
    MapRecipe recipe;
    int palette{};
    std::optional<int> staffTomb;
    std::vector<std::pair<int, int>> rooms; // Original tile anchors, activation order.
    std::shared_ptr<const Map> map;
    double preparationMilliseconds{};
};
// Incremental host preparation; every operation stays on the archive owner's
// thread. No partial terrain is published and reveal/RNG order is unchanged.
class AreaGenerator {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    AreaGenerator(Archives &, AreaGenerationRequest);
    ~AreaGenerator();
    bool advance(std::chrono::steady_clock::time_point deadline);
    GeneratedArea take();
};
// Host preparation uses the same generator/reveal operations as RemoteTown.
// The returned Map retains every referenced DT1 library beyond generator/cache
// destruction. Dynamic actors/objects and activation belong to the caller.
GeneratedArea generateArea(Archives &, AreaGenerationRequest);
} // namespace d2x
