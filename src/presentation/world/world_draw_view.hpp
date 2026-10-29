#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "world/identity.hpp"
#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>

namespace d2x {
struct Sprite;
struct InventoryItemView;
struct Map;
// Borrowed for one synchronous draw only. Positions are in the terrain's local
// subtile coordinates; packet IDs and authoritative simulation are not inputs.
struct WorldDrawItem {
    const Sprite *image = nullptr;
    Vec position, pixelOffset;
    EntityId unit;
    bool shadow = false, highlighted = false;
    int orderFlag = 0;
    const InventoryItemView *ground = nullptr;
    int missile = -1, overlay = -1;
    Vec heading;
    float age = 0, remaining = 0;
    EntityId lightOwner;
    float lightStartedAt = 0;
    bool loop = false;
    int height = 1;
    std::optional<int> missileFrame;
    std::optional<int> overlayFrame;
    bool suppressMissileLight{};
};
struct WorldDrawView {
    struct Alert { EntityId unit; Vec position; };
    struct Object { int identity = -1, mode = 0; Vec position; };
    struct Monster { int identity = -1; Vec position; };
    struct PlayerLight { Vec position; int radius; uint32_t color; };
    const Map *map = nullptr;
    RegionId region{};
    int level = 0, palette = 0;
    uint64_t gameGeneration = 0, areaGeneration = 0;
    Vec observer, roomObserver, terrainOrigin;
    uint32_t observerLightColor{}; // Native item_lightcolor, 0 selects white.
    float time = 0, elapsed = 0;
    std::optional<bool> eclipse;
    std::optional<int> denIntensity; // Retail level-8 ambient override; absence uses Levels/environment.
    std::optional<int> sanctumIntensity; // Retail 107/108 ambient override after FX12.
    std::optional<int> cloakDuration;
    std::optional<size_t> selectedExit;
    EntityId groundHighlight;
    std::vector<WorldDrawItem> items;
    std::vector<Alert> alerts;
    std::vector<Object> objects;
    std::vector<Monster> monsters;
    std::vector<PlayerLight> playerLights;
};
} // namespace d2x
