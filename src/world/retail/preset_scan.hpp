#pragma once
#include "rooms.hpp"
#include "resources/formats.hpp"
#include <tuple>

namespace d2x {
struct RetailPresetUnit {
    MapObject unit; // Resolved identity; global subtile coordinates and MapAI.
    int mode{};
    bool identityResolved{true};
    bool clientOnly{};
};
using RetailPresetKey = std::tuple<int, int, int, int, int>; // level, preset, file, mapX, mapY
// Current-MPQ linker identities only, without gameplay population or actors.
class RetailPresetScan {
    std::map<int, uint32_t> objectSubclasses_;
    std::array<std::vector<int>, 5> monsterPresets_;
    int monsterCount_{}, uniqueCount_{};
    int navi_{-1};
  public:
    explicit RetailPresetScan(Archives &);
    std::optional<RetailPresetUnit> resolveUnit(const MapObject &, int version, int act,
        int mapX, int mapY) const;
    std::vector<RetailPresetUnit> buildUnits(const MapData &, int act, int mapX, int mapY, Seed &) const;
    std::vector<RetailPresetUnit> buildClientUnits(const MapData &, int level, int preset,
        int file, int mapX, int mapY, int width, int height) const;
    // Scanned presets consume the level stream before rooms exist. Other
    // presets consume the first activating room's stream when their DS1 opens.
    void consumeUnitRandom(const MapData &, int act, Seed &) const;
    std::vector<uint32_t> scan(const PresetRecord &, const MapData &, int act,
        int width, int height, uint32_t flags, Seed &,
        std::vector<RetailPresetUnit> *units = nullptr, int mapX = 0, int mapY = 0) const;
};
} // namespace d2x
