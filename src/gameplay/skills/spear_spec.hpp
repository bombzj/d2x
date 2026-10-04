#pragma once
#include "gameplay/combat/missile_effects.hpp"
#include <memory>
#include <vector>

namespace d2x {
struct SpearSequenceFrame { int frame = 0; bool secondAttack = false, hit = false; };
struct SpearSequence { std::vector<SpearSequenceFrame> frames; };
struct SpearSkillSpec {
    enum class Kind { Jab, Power, Poison, Impale, Bolt, Charged, Fend, Strike, Fury };
    Kind kind = Kind::Jab;
    std::optional<PoisonCloudSpec> poisonTrail;
    int sourceDamage = 128, conversionPercent = 0, conversionPerLevel = 0;
    bool automaticHit = false, attackWithoutMana = false;
    int countBase = 0, countDivisor = 1, activateFrames = 0;
    int countPerLevel = 0, targetRadius = 0;
    int childId = -1, childFrames = 0, childRangePerLevel = 0, childVelocity = 0, childVelocityPerLevel = 0;
    float childSpeed = 0, childLifetime = 0;
    bool childKillOnHit = false;
    int wearChance = 0, wearMinimum = 0, wearMaximum = 0, wearAmount = 0;
    std::shared_ptr<const SpearSequence> oneHand, twoHand;
};
struct SpearMissileState {
    std::shared_ptr<const SpearSkillSpec> program;
};
} // namespace d2x
