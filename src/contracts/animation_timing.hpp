#pragma once
namespace d2x {
// Only completely observed equipment/state values; absence remains unknown.
struct KnownAttackTiming { int itemIAS{},weaponSpeed{},skillRate{}; int weaponItemSpeed{},weaponItemIAS{};bool equipped{}; };
struct KnownRecoveryTiming { int hitRecovery{},block{}; bool holyShield{}; };
}
