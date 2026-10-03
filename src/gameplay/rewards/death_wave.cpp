#include "death_wave.hpp"
#include "core/random.hpp"

namespace d2x {
DeathWavePlan planDeathWave(Vec origin, uint64_t frame, DeathWaveDelay delay,
        std::span<const DeathWaveTarget> targets, uint64_t random) {
    DeathWavePlan plan{random, {}};
    for (const auto &target : targets) {
        const int horizontal = int(target.position.x) - int(origin.x);
        const int vertical = int(target.position.y) - int(origin.y);
        if (target.eligible && horizontal * horizontal + vertical * vertical <= 35 * 35)
            plan.deaths.push_back({target.id, frame + delay.base + limitedRandom(plan.random, delay.randomBound)});
    }
    return plan;
}
} // namespace d2x
