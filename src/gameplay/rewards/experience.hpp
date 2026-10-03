#pragma once
#include <cstdint>

namespace d2x {
// Parsed original table values are supplied by the content adapter.
uint64_t monsterExperienceGain(uint64_t base, int rankFactor, int monsterLevel,
    int recipientLevel, int ratio, int shift);
uint64_t playerExperienceGain(uint64_t base, int experiencePercent);
bool hirelingCanGainExperience(bool alive, int level, int ownerLevel);
struct HirelingExperienceFacts {
    uint64_t experience = 0, baseExperience = 0, nextExperience = 0;
    int level = 0, ownerLevel = 0, experiencePercent = 0;
    bool alive = false, directKill = false;
};
struct HirelingExperiencePlan {
    uint64_t amount = 0, experience = 0;
    int level = 0;
    bool leveled = false;
};
HirelingExperiencePlan planHirelingExperience(uint64_t base, const HirelingExperienceFacts &facts);
} // namespace d2x
