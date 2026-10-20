#pragma once
#include "core/id.hpp"
#include <cstdint>
#include <optional>
namespace d2x {
int64_t poisonMasteryRate(int64_t rate, int percent);
int weaponPoisonLength(int64_t itemFrames, int itemSources, int64_t skillFrames = 0,int32_t overrideFrames = 0);
// Rate has already passed damage resistance; frames still await target PLR.
struct PoisonApplication { int64_t rate{}; uint64_t frames{}; int state{-1}; EntityId actualSource{}; int lengthPierce{}; };
struct PoisonStatus { PoisonApplication damage; EntityId source; uint64_t until{}, next{};std::optional<int32_t> nativeExpireFrame{};std::optional<uint64_t> removalDue{}; };
// SUNITDMG_ApplyPoisonDamage keeps a stronger existing rate, including its deadline.
inline bool replacesPoison(int64_t existing,int64_t incoming) {return incoming>0 && incoming>=existing;}
}
