#pragma once
#include "core/id.hpp"
#include <cstdint>
namespace d2x {
struct PoisonApplication { int64_t rate{}; uint64_t frames{}; int state{-1}; EntityId actualSource{}; };
struct PoisonStatus { PoisonApplication damage; EntityId source; uint64_t until{}, next{}; };
// SUNITDMG_ApplyPoisonDamage keeps a stronger existing rate, including its deadline.
inline bool replacesPoison(int64_t existing,int64_t incoming) {return incoming>0 && incoming>=existing;}
}
