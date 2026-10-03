#pragma once
#include <functional>

namespace d2x {
struct CombatUnit;
struct DamageRequest;
// Requires a living, fully bound unit and a positive fixed-step duration.
// Synchronous delivery: neither the callback nor the borrowed unit is retained.
using PeriodicDamageHandler = std::function<void(const DamageRequest &)>;
void advancePeriodicDamage(const CombatUnit &unit, float dt, bool poisonCannotKill,
                           const PeriodicDamageHandler &applyDamage);
} // namespace d2x
