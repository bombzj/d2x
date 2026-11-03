#pragma once
#include "core/math.hpp"
#include "core/entity_id.hpp"
#include <span>
#include <vector>
namespace d2x {
// MISSILES_CreateMissileFromParams: seed from the owner's base pierce_idx;
// stat absent means zero. This stream does not consume combat/action RNG.
int missilePierceCount(int chance,uint32_t ownerIndex);
struct MissileTargetBurst { int count{},countPerLevel{},radius{}; };
struct MissileBurstTarget { EntityId id; Vec position; };
// Native aura callbacks keep the caller's unit traversal order. Visibility,
// allegiance and collision remain separate client/server adapter decisions.
std::vector<MissileBurstTarget> missileBurstTargets(Vec origin,int radius,int count,std::span<const MissileBurstTarget> candidates);
struct PoisonCloudDirection { Vec direction; bool secondary{}; };
// MissMode::CreatePoisonCloudHitSubmissiles / retail client equivalent.
std::vector<PoisonCloudDirection> poisonCloudDirections(int mainStep,int subStep);
int poisonCloudVelocity(int parameter);
// D2Game CreateImmolationArrowHitSubmissiles and retail D2Client B9740.
std::vector<Vec> missileDiskOffsets(int radius);
} // namespace d2x
