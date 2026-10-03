#pragma once
#include "core/id.hpp"
#include "gameplay/units/restoration.hpp"
#include <deque>

namespace d2x {
struct PlayerResources {
    float hp = 0, mana = 0, stamina = 0;
    float chill = 0;
    float poisonRemaining = 0, poisonPerSecond = 0;
    EntityId poisonSource, openWoundsSource;
    float openWoundsRemaining = 0, openWoundsPerSecond = 0;
    float webSlowRemaining = 0;
    int webSlowPercent = 0;
    EntityId webSource;
    std::deque<ResourceRestoration> healing, manaRestoration;
};
} // namespace d2x
