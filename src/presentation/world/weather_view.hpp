#pragma once
#include "core/math.hpp"
#include "weather_debug.hpp"
#include "presentation/graphics/graphics.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace d2x {
struct LevelRecord;
class PaletteBlendView;
// Client environment particles only. Never advances a room/player gameplay seed.
class WeatherView {
  WeatherCycleState &cycle_;
    struct Drop {
        int x{}, y{}, bottom{}, length{}, speed{}, phase{}, size{}, rest{};
        Color color{};
        bool landed{};
    };
    struct Splash { Vec position; int kind{}, ticks{}; bool bubble{}; };
    struct SurfaceStep { int rainChance{}, age{}; bool mud{}; };
    uint64_t random_{};
    uint64_t surfaceRandom_{};
    int act_{-1}, level_{-1}, stage_{}, remaining_{}, duration_{}, peak_{}, density_{};
    int wind_{127}, targetWind_{127}, windTicks_{312};
    int thunderTicks_{1250}, thunderSoundTicks_{-1};
    float lightningDelay_{-1};
    bool lightning_{}, secondFlash_{}, flashPending_{}, flash_{}, thunderSoundEnabled_{}, thunderReady_{};
    uint32_t tick_{};
    int minWind_{42}, windRange_{170};
    uint32_t surfaceFrame_{}, lastMudFrame_{};
    float remainder_{};
    bool snow_{}, largeSnow_{}, forcedSnow_{}, enabled_{}, mud_{};
    bool randomizeCycle_{};
    WeatherDebugMode debugMode_{WeatherDebugMode::Auto};
    int debugDensity_{255};
    Vec camera_{};
    bool cameraKnown_{};
    std::vector<Drop> drops_;
    std::vector<Splash> splashes_;
    std::vector<SurfaceStep> surfaceSteps_;
    std::array<GpuAnimation,4> rainArt_, bubbleArt_;
    int artPalette_{-1};
    int random(int range, int minimum = 0);
    void wind();
    void initializeCycle();
    void configurePrecipitation(bool entering);
    void sustainPrecipitation();
    void enterLevel(const LevelRecord &);
    void applyDebugWeather();
    Drop spawn(int width, int height, int period, const PaletteBlendView &);
    void step(int width, int height, int period, const PaletteBlendView &);
  public:
    explicit WeatherView(WeatherCycleState &cycle) : cycle_(cycle) {}
    void reset(uint32_t seed);
    void lightning(float delay);
    void presentLightning();
    bool takeThunder();
    WeatherDebugState debugState() const;
    void debugWeather(WeatherDebugMode, int density, const LevelRecord &);
    void advance(float elapsed, const LevelRecord &, Vec cameraPixels, int width, int height,
                 int period, const PaletteBlendView &);
    // Called once for each currently drawn original WATER tile. Coordinates
    // are world subtiles, so camera motion does not move a ground ripple.
    void waterTile(Vec position, Vec screenPosition, Rectangle viewport);
    void finishWaterTiles();
    void loadArt(Archives &, Graphics &, int palette);
    void drawSplashes(const PaletteBlendView &, const std::function<Vec(Vec)> &screen) const;
    void draw(const PaletteBlendView &, Rectangle viewport) const;
};
} // namespace d2x
