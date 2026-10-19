#pragma once
#include "core/math.hpp"
#include "raylib.h"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace d2x {
struct Grid;
struct LevelRecord;
class PaletteBlendView;
struct SceneLight {
    Vec position;
    int radius = 0;
    Color color{0, 0, 0, 255};
    enum class Occlusion { None, Propagated };
    Occlusion occlusion = Occlusion::None;
};
Color sceneLightColor(uint32_t nativeColor);
struct SceneLightRegion {
    int x, y, width, height;
    const LevelRecord *level; // Borrowed only for the synchronous world draw.
};
// View-only light mask. DT1 and object-mode flags supply visibility; no lighting state is saved.
class LightingView {
    static constexpr int maskRadius = 30;
    static constexpr int maskSide = maskRadius * 2 + 1;
    Texture2D lightMap_{};
    // RGB is the native normalized color; alpha carries independent intensity.
    std::vector<Color> lightPixels_;
    std::vector<Color> cornerPixels_;
    // Transient presentation state, initialized as ENVIRONMENT_AllocDrlgEnvironment.
    struct Environment {
        int ticks = 0, cycle = 2, intensity = 128;
        Color color{255, 255, 255, 255};
    };
    std::array<Environment, 5> environments_{};
    float frameRemainder_ = 0;
    bool eclipse_ = false;
    int cloakLevel_{-1},cloakFrame_{},cloakRise_{},cloakTotal_{},cloakAngle_{};
    float cloakFactor_{1};
    Vec origin_{};
    Color ambient_{};
    struct Sources;
    std::unique_ptr<Sources> sources_;

  public:
    LightingView();
    ~LightingView();
    LightingView(const LightingView &) = delete;
    LightingView &operator=(const LightingView &) = delete;
    void advance(float dt, const LevelRecord &level);
    void resetEnvironment();
    void resetSources();
    int overlayRadius(uint64_t owner, int overlay, float startedAt, int initial, int target);
    void finishSources();
    void setEclipse(bool active);
    void beginCloak(int duration,int level);
    void prepare(const Grid &grid, const LevelRecord &level, Vec player, int radius, Color playerColor,
                 std::span<const SceneLight> lights, std::span<const SceneLightRegion> regions);
    Color sample(Vec position) const;
    void bind(PaletteBlendView &, Vec player, Vec playerScreen, float zoom) const;
};
} // namespace d2x
