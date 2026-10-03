#pragma once

namespace d2x {
struct FirewallSpec {
    int makerId = -1, fireId = -1, makerFrames = 0, fireFrames = 0;
    float velocity = 0;
    int minimumDamage = 0, maximumDamage = 0, hitShift = 0, size = 1;
    int softHitChance = 0;
};
// Compatibility name; both players and monsters already use this program.
using MonsterFirewall = FirewallSpec;
} // namespace d2x
