#pragma once
#include <cstdint>
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/combat/stats.hpp"
#include <array>
#include <map>
#include <memory>
namespace d2x {
// Mechanism-specific values stay behind the immutable program slot.
struct BoneSkillSpec {
    bool corpse = false, barrier = false, prison = false, spirit = false;
    int retargetPeriod = 0, searchRadius = 0;
    float spiritLifetime = 0;
    int lifePerLevel = 0, lifePercent = 0, barrierFrames = 0, sideCount = 0;
    std::map<int, int> lifeSynergies;
    std::array<UnitCombatStats, 3> barrierStats;
    int radius = 0, radiusPerLevel = 0;
    int minimumPercent = 0, maximumPercent = 0, elementalPercent = 0;
    int trailId = -1, trailCount = 0;
    float trailDuration = 0;
    int corpseVisual = -1;
    float corpseVisualDuration = 0;
};
struct BoneMissileState {
    std::shared_ptr<const BoneSkillSpec> program;
    EntityId root;
    int remaining = 0;
    EntityId target;
    Vec offset;
    bool coordinateTarget = false, searched = false;
};
struct BoneBarrierState {
    EntityId caster, root;
    uint64_t expiresAt = 0;
    Vec facing{1,0};
};
}
