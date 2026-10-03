#pragma once

namespace d2x {
// Prepared from imported skill data by SceneAssets; no damage or cast rules.
struct BlizzardVisual {
    int fallDistance = 0, fallRate = 0;
    int impactId = -1, impactFrames = 0;
};
struct MeteorVisual {
    int fireFrames = 0;
    int explodeId = -1, explodeDensity = 1;
    int lightId = -1, mediumId = -1, smallId = -1;
    int mediumDensity = 1, smallDensity = 1;
};
} // namespace d2x
