#pragma once
#include <cstddef>

namespace d2x {
struct WeatherCycleState {
    int stage{}, remaining{}, duration{}, peak{};
    bool windInitialized{};
    bool initialized{};
};
enum class WeatherDebugMode { Auto, Clear, Rain, Snow };
struct WeatherDebugState {
    WeatherDebugMode mode{WeatherDebugMode::Auto};
    int level{-1}, stage{}, density{}, remainingFrames{};
    int requestedDensity{255};
    bool precipitationAllowed{}, rainAllowed{}, snowAllowed{}, snow{};
    bool lightning{};
    int thunderSoundFrames{-1};
    std::size_t particles{}, splashes{};
};
} // namespace d2x
