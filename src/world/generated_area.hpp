#pragma once
#include "world/native_map.hpp"
#include <chrono>

namespace d2x {
struct AreaGenerationRequest {
    uint32_t seed{};
    int act{}, level{1}, difficulty{};
    bool ladderMonsterStats{true}; // Native nGameType || dwGameType, prepared by hosting.
};
struct GeneratedArea {
    AreaGenerationRequest request;
    Vec origin;
    MapRecipe recipe;
    int palette{};
    std::optional<int> staffTomb;
    struct RoomLink { int level{}, x{}, y{}; };
    std::map<std::pair<int, int>, std::vector<RoomLink>> nearRooms;
    // Offline projection for authored object/exit/arrival descriptors only.
    // Its selected collision and room random must never seed the authority.
    std::shared_ptr<const Map> map;
    std::shared_ptr<const PreparedRoomTerrain> roomTerrain;
    double preparationMilliseconds{};
    std::map<std::pair<int, int>, RoomPopulationLayout> populationRooms;
    std::vector<Vec> populationWarpCenters;
    uint64_t levelRandom{};
    std::vector<NativeTileInfo> tileInfo;
    NativeLevelPlacement placement;
};
// Resource/descriptor preparation on the archive owner's thread. The authority
// receives pristine decoded room inputs and materializes its own DRLG history.
class AreaGenerator {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    AreaGenerator(Archives &, AreaGenerationRequest);
    ~AreaGenerator();
    bool advance(std::chrono::steady_clock::time_point deadline);
    GeneratedArea take();
};
// The Map is an offline authored-content projection and retains its libraries.
// Only roomTerrain may supply authority terrain and active-room initial seeds.
GeneratedArea generateArea(Archives &, AreaGenerationRequest);
} // namespace d2x
