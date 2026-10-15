#pragma once
#include <cstdint>
#include <compare>

namespace d2x {
// Events.txt identities used by D2Game's unit-event ABI. Skill bindings and
// callback numbers are prepared from the current MPQ, never chosen by clients.
enum class NativeUnitEvent : uint8_t {
    HitByMissile, DamagedInMelee, DamagedByMissile, AttackedInMelee,
    Active, DealtMeleeDamage, DealtMissileDamage, MeleeAttack, MissileAttack,
    Kill, Killed, AbsorbDamage, LevelUp
};
struct SkillUnitEventBinding {
    int16_t event{-1},function{};
    auto operator<=>(const SkillUnitEventBinding &) const = default;
};
}
