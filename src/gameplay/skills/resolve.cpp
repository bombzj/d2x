#include "spec.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace d2x {
namespace {
int64_t levelBonus(int rank, const std::array<int, 5> &steps) {
    int64_t result = 0;
    for (int level = 2; level <= rank; ++level)
        result += steps[level <= 8 ? 0 : level <= 16 ? 1 : level <= 22 ? 2 : level <= 28 ? 3 : 4];
    return result;
}
} // namespace
std::vector<Vec> chargedBoltPath(Vec origin, Vec target, int index, int frames) {
    int deltaX = int(target.x) - int(origin.x), deltaY = int(target.y) - int(origin.y);
    const int absX = std::abs(deltaX), absY = std::abs(deltaY);
    int directionIndex = -1;
    if (absX < 2 * absY) {
        if (absY >= 2 * absX) {
            if (deltaX < 0) directionIndex = deltaY < -1 ? 5 : std::min(deltaY, 2) + 7;
            else deltaX &= 1;
        }
    } else deltaY = deltaY >= 0 ? deltaY & 1 : -1;
    if (directionIndex < 0) {
        deltaX = std::clamp(deltaX, -2, 2);
        directionIndex = deltaY < -1 ? 5 * deltaX + 10 : std::min(deltaY, 2) + 5 * deltaX + 12;
    }
    constexpr int directions[]{5,4,4,4,3,6,5,4,3,2,6,6,6,2,2,6,7,0,1,2,7,0,0,0,1};
    constexpr Vec offsets[]{{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
    const int mainDirection = directions[directionIndex];
    uint64_t seed = (uint64_t(666) << 32) | uint32_t(index + int(target.x));
    Vec point{float(int(origin.x)) + .5f, float(int(origin.y)) + .5f};
    std::vector<Vec> path{point};
    for (int step = 0; step < std::min(77, frames) / 2; ++step) {
        seed = uint64_t(uint32_t(seed)) * 0x6ac690c5ULL + (seed >> 32);
        const int roll = int(uint32_t(seed) & 31);
        const int offset = roll == 31 ? 1 : roll % 3 - 1;
        point = point + offsets[(mainDirection + offset + 8) % 8] * 2.f;
        path.push_back(point);
    }
    return path;
}
SkillCastSpec resolveSkill(const SkillSpec &spec, int rank,
                           const std::map<int, int> &learned, int fireMasteryPercent,
                           int lightningMasteryPercent) {
    if (spec.effect == SkillBehavior::None || spec.sourceId < 0 || rank < 1 || rank > 255 ||
        spec.manaShift < 0 || spec.manaShift > 15 || spec.hitShift < 0 || spec.hitShift > 15)
        throw std::runtime_error("Unsupported original skill rank or shift");
    SkillCastSpec result;
    result.effect = spec.effect;
    result.sourceId = spec.sourceId;
    if (spec.effect == SkillBehavior::FrozenArmor) {
        int synergyRanks = 0;
        for (int id : spec.armorSynergySkills)
            if (auto found = learned.find(id); found != learned.end()) synergyRanks += found->second;
        const auto &parameters = spec.armorParameters;
        CombatEffectSpec armor;
        armor.state = spec.state;
        armor.source = {CombatEffectSource::Skill, {}, spec.sourceId, rank};
        armor.stacking = EffectStacking::ReplaceState;
        const int64_t frames = int64_t(parameters[2]) + int64_t(rank - 1) * parameters[3] +
                               int64_t(synergyRanks) * parameters[6];
        if (frames <= 0) throw std::runtime_error("Invalid skill state duration");
        armor.duration = EffectFrame(frames);
        armor.modifiers.combat.defensePercent = parameters[0] + (rank - 1) * parameters[1];
        armor.visual.overlayId = spec.stateOverlay.id;
        const float freeze = float((parameters[4] + (rank - 1) * parameters[5]) *
            (100 + synergyRanks * parameters[7]) / 100) / 25.f;
        armor.reactions.push_back({CombatEffectEvent::DamagedInMelee,
            FreezeAttacker{freeze, spec.hitOverlay.id, float(spec.hitOverlay.frames) / spec.hitOverlay.fps}});
        result.appliedEffect = std::move(armor);
    }
    const int64_t scaledMana = std::max<int64_t>(0,
        int64_t(spec.mana) + int64_t(rank - 1) * spec.manaPerLevel) << spec.manaShift;
    const int64_t fixedMana = std::max<int64_t>(int64_t(spec.minimumMana) * 256, scaledMana);
    result.manaCost = float(fixedMana) / 256.f;
    result.startMana = float(spec.startMana);
    int64_t synergy = 0;
    for (int id : spec.synergySkills)
        if (auto found = learned.find(id); found != learned.end()) synergy += found->second;
    const int64_t bonus = std::max<int64_t>(0, 100 + synergy * spec.synergyPercent);
    auto damage = [&](int base, const std::array<int, 5> &steps) {
        const int64_t value = (int64_t(base) + levelBonus(rank, steps)) << spec.hitShift;
        const int64_t scaled = value * bonus / 100;
        const int mastery = spec.fireDamage ? fireMasteryPercent :
            spec.lightningDamage ? lightningMasteryPercent : 0;
        const int64_t mastered = scaled + scaled * mastery / 100;
        if (mastered < 0 || mastered > std::numeric_limits<int32_t>::max())
            throw std::runtime_error("Original skill damage exceeds supported range");
        return float(mastered) / 256.f;
    };
    result.minimumDamage = damage(spec.minimumDamage, spec.minimumPerLevel);
    result.maximumDamage = damage(spec.maximumDamage, spec.maximumPerLevel);
    int64_t coldFrames = int64_t(spec.coldFrames) +
        int64_t(std::min(rank - 1, 7)) * spec.coldFramesPerLevel[0] +
        int64_t(std::clamp(rank - 8, 0, 8)) * spec.coldFramesPerLevel[1] +
        int64_t(std::max(rank - 16, 0)) * spec.coldFramesPerLevel[2];
    if (auto found = learned.find(spec.coldSynergySkill); found != learned.end())
        coldFrames += coldFrames * found->second * spec.coldSynergyPercent / 100;
    result.coldDuration = float(coldFrames) / 25.f;
    result.missileId = spec.missileId;
    result.missileCount = std::min(spec.missileCountLimit,
        spec.missileCount + (rank - 1) * spec.missileCountPerLevel);
    result.missileNextDelay = float(spec.missileNextDelay) / 25.f;
    result.staticPercent = float(spec.staticPercent);
    result.staticMinDamage = float(spec.staticMinDamage) / 256.f;
    result.staticRadius = float(spec.staticRange + (rank - 1) * spec.staticRangePerLevel);
    result.castOverlayId = spec.castOverlay.id;
    result.hitOverlayId = spec.hitOverlay.id;
    if (spec.castOverlay.fps > 0)
        result.visualDuration = float(spec.castOverlay.frames) / spec.castOverlay.fps;
    if (spec.hitOverlay.fps > 0)
        result.hitOverlayDuration = float(spec.hitOverlay.frames) / spec.hitOverlay.fps;
    const int velocity = (int(spec.missileVelocity) + rank * spec.missileVelocityPerLevel / 8) * 256;
    result.missileVelocity = float(velocity * 75 / 100) * 25.f / 4096.f;
    result.missileAcceleration = float(spec.missileAcceleration) * 25.f / 4096.f;
    result.missileMaxVelocity = float(spec.missileMaxVelocity * 256) * 25.f / 4096.f;
    result.missileLifetime = spec.missileLifetime + float(rank * spec.missileRangePerLevel) / 25.f;
    if (spec.effect == SkillBehavior::Inferno)
        result.missileLifetime = float(std::max(1, (spec.flameFrames + (rank - 1) * spec.flameFramesPerLevel) / 2)) / 25.f;
    result.missileImpact = spec.missileImpact;
    return result;
}
} // namespace d2x
