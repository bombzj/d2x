#include "spec.hpp"
#include "core/random.hpp"
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
SummonCastSpec resolveSummon(const SummonSkillSpec &spec, int rank, int mastery, int resist,
                            int ownerLevel, int difficulty) {
    if (rank <= 0 || rank > 255 || difficulty < 0 || difficulty > 2 || ownerLevel < 1)
        throw std::runtime_error("Invalid summon level or difficulty");
    SummonCastSpec result;
    result.monster = spec.monster; result.kind = spec.kind;
    result.limit = rank < 4 ? rank : 2 + rank / 3;
    result.shieldChance = rank > 2 ? spec.shieldChance : 0;
    result.shieldVariants = spec.shieldVariants;
    result.stats = spec.base[difficulty];
    auto &stats = result.stats;
    auto &attributes = stats.attributes;
    stats.level = std::clamp(rank + 3 * ownerLevel / 4, 1, ownerLevel);
    const auto level = std::min(size_t(stats.level), spec.levelDefense.size() - 1);
    attributes.maxLife = int((int64_t(attributes.maxLife) + int64_t(mastery) * spec.masteryLife) *
        (100 + int64_t(std::max(0, rank - 3)) * spec.lifePerRank) / 100);
    attributes.attackRating += spec.levelAttack.at(level)[difficulty] + (rank + mastery) * spec.attackPerRank;
    attributes.defense += spec.levelDefense.at(level)[difficulty] + (rank + mastery) * spec.defensePerRank;
    const int64_t damage = int64_t(mastery) * spec.masteryDamage + levelBonus(rank, spec.damageSteps);
    const int percent = 100 + std::max(0, rank - 3) * spec.damagePerRank;
    stats.minimumDamage = float((int64_t(stats.minimumDamage * 256.f) + damage * 256) * percent / 100) / 256.f;
    stats.maximumDamage = float((int64_t(stats.maximumDamage * 256.f) + damage * 256) * percent / 100) / 256.f;
    if (resist > 0) {
        const int bonus = std::min(spec.resistMaximum, spec.resistMinimum +
            (spec.resistMaximum - spec.resistMinimum) * (110 * resist / (resist + 6)) / 100);
        attributes.fireResist += bonus; attributes.coldResist += bonus;
        attributes.lightningResist += bonus; attributes.poisonResist += bonus;
    }
    return result;
}
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
    // SkillSor.cpp SKILLS_MissileInit_ChargedBolt deliberately seeds from bolt index + path X.
    uint64_t seed = initialRandom(uint32_t(index + int(target.x)));
    Vec point{float(int(origin.x)) + .5f, float(int(origin.y)) + .5f};
    std::vector<Vec> path{point};
    for (int step = 0; step < std::min(77, frames) / 2; ++step) {
        rollRandom(seed);
        const int roll = int(uint32_t(seed) & 31);
        const int offset = roll == 31 ? 1 : roll % 3 - 1;
        point = point + offsets[(mainDirection + offset + 8) % 8] * 2.f;
        path.push_back(point);
    }
    return path;
}
SkillCastSpec resolveSkill(const SkillSpec &spec, int rank,
                           const std::map<int, int> &learned, int fireMasteryPercent,
                           int lightningMasteryPercent, int coldDamagePercent) {
    if (spec.effect == SkillBehavior::None || spec.sourceId < 0 || rank < 1 || rank > 255 ||
        spec.manaShift < 0 || spec.manaShift > 15 || spec.hitShift < 0 || spec.hitShift > 15)
        throw std::runtime_error("Unsupported original skill rank or shift");
    SkillCastSpec result;
    result.effect = spec.effect;
    result.rank = rank;
    result.sourceId = spec.sourceId;
    result.delayFrames = spec.delayFrames;
    result.blizzard = spec.blizzard;
    if (spec.freezingArea) {
        const auto &program = *spec.freezingArea;
        int synergyRank = 0;
        if (auto found = learned.find(program.synergySkill); found != learned.end()) synergyRank = found->second;
        const int64_t frames = (int64_t(program.freezeFrames) + int64_t(rank - 1) * program.freezeFramesPerLevel) *
                              (100 + int64_t(synergyRank) * program.synergyPercent) / 100;
        if (frames < 0 || frames > std::numeric_limits<int>::max())
            throw std::runtime_error("Original freeze duration exceeds supported range");
        result.freezingArea = FreezingAreaCastSpec{
            program.radiusOverride > 0 ? program.radiusOverride :
                std::max(1, program.radius + (rank - 1) * program.radiusPerLevel),
            program.freezeOverride > 0 ? program.freezeOverride : int(frames)};
    }
    if (spec.effect == SkillBehavior::FrozenArmor || spec.effect == SkillBehavior::ShiverArmor) {
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
        if (spec.effect == SkillBehavior::FrozenArmor) {
            const float freeze = float((parameters[4] + (rank - 1) * parameters[5]) *
                (100 + synergyRanks * parameters[7]) / 100) / 25.f;
            armor.reactions.push_back({CombatEffectEvent::DamagedInMelee,
                FreezeAttacker{freeze, spec.hitOverlay.id, float(spec.hitOverlay.frames) / spec.hitOverlay.fps}});
        } else armor.reactions.push_back({CombatEffectEvent::AttackedInMelee, ColdMeleeRetaliation{}});
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
            spec.lightningDamage ? lightningMasteryPercent : spec.coldDamage ? coldDamagePercent : 0;
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
    const int64_t poisonFrames = int64_t(spec.poisonFrames) +
        int64_t(std::min(rank - 1, 7)) * spec.poisonFramesPerLevel[0] +
        int64_t(std::clamp(rank - 8, 0, 8)) * spec.poisonFramesPerLevel[1] +
        int64_t(std::max(rank - 16, 0)) * spec.poisonFramesPerLevel[2];
    result.poisonDuration = float(poisonFrames) / 25.f;
    result.weapon = spec.weapon;
    if (result.weapon)
        result.weapon->attackRating += (rank - 1) * result.weapon->attackRatingPerLevel;
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
    if (spec.frozenOrb) {
        const auto &orb = *spec.frozenOrb;
        FrozenOrbCastSpec cast;
        auto child = [rank](const FrozenOrbSpec::Child &source) {
            const int velocity = (source.velocity + rank * source.velocityPerLevel / 8) * 256;
            return FrozenOrbCastSpec::Child{source.missileId,
                source.lifetimeFrames + rank * source.rangePerLevel,
                float(velocity * 75 / 100) * 25.f / 4096.f};
        };
        cast.bolt = child(orb.bolt); cast.nova = child(orb.nova);
        cast.lifetimeFrames = int(result.missileLifetime * 25.f + .5f);
        cast.emissionPeriod = orb.emissionPeriod; cast.directionStep = orb.directionStep;
        cast.burstStep = orb.burstStep;
        cast.novaTurnFrames = orb.novaTurnFrames; cast.novaTurnPeriod = orb.novaTurnPeriod;
        result.frozenOrb = cast;
    }
    if (spec.effect == SkillBehavior::Inferno)
        result.missileLifetime = float(std::max(1, (spec.flameFrames + (rank - 1) * spec.flameFramesPerLevel) / 2)) / 25.f;
    result.missileImpact = spec.missileImpact;
    if (result.missileImpact && result.missileImpact->areaMissile) {
        auto &area = *result.missileImpact->areaMissile;
        area.minimum = int(result.minimumDamage * 256.f);
        area.maximum = int(result.maximumDamage * 256.f);
    }
    if (result.missileImpact && result.missileImpact->cloudBurst) {
        auto &cloud = result.missileImpact->cloudBurst->cloud;
        if (cloud.damageFromSkill) {
            if (!spec.poisonDamage || poisonFrames <= 0)
                throw std::runtime_error("Poison cloud requires resolved poison skill damage");
            cloud.minimum = int(result.minimumDamage * 256.f);
            cloud.maximum = int(result.maximumDamage * 256.f);
            cloud.poisonFrames = int(poisonFrames);
            cloud.damageFromSkill = false;
        }
    }
    return result;
}
} // namespace d2x
