#include "experience.hpp"
#include <algorithm>
#include <array>

namespace d2x {
uint64_t monsterExperienceGain(uint64_t base, int rankFactor, int monsterLevel,
        int recipientLevel, int ratio, int shift) {
    uint64_t award = base * rankFactor;
    const int delta = recipientLevel - monsterLevel;
    // Original single-player level difference factors from D2MOO SUNITDMG_ComputeExperienceGain.
    constexpr std::array lower{256, 256, 256, 256, 256, 256, 207, 159, 110, 61, 13};
    constexpr std::array higher{256, 256, 256, 256, 256, 256, 225, 174, 92, 38, 5};
    if (delta >= 0)
        award = award * lower[std::min(delta, 10)] / 256;
    else if (recipientLevel < 25)
        award = award * higher[std::min(-delta, 10)] / 256;
    else
        award = award * recipientLevel / monsterLevel;

    return award * uint64_t(ratio) >> shift;
}
uint64_t playerExperienceGain(uint64_t base, int experiencePercent) {
    return base + base * uint64_t(std::max(0, experiencePercent)) / 100;
}
bool hirelingCanGainExperience(bool alive, int level, int ownerLevel) {
    return alive && level < ownerLevel && level < 99;
}
HirelingExperiencePlan planHirelingExperience(uint64_t base, const HirelingExperienceFacts &facts) {
    HirelingExperiencePlan plan{0, facts.experience, facts.level, false};
    if (!hirelingCanGainExperience(facts.alive, facts.level, facts.ownerLevel)) return plan;
    const auto bonus = std::max(0, 100 + facts.experiencePercent);
    plan.amount = std::min(base * unsigned(bonus) / 100,
                           (facts.nextExperience - facts.baseExperience) >> 6);
    if (!facts.directKill) plan.amount = plan.amount * 86 / 256;
    plan.experience += plan.amount;
    if (plan.experience >= facts.nextExperience) { ++plan.level; plan.leveled = true; }
    return plan;
}
} // namespace d2x
