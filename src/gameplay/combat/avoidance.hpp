#pragma once
#include "gameplay/combat/stat_modifiers.hpp"
#include <algorithm>
#include "core/random.hpp"
namespace d2x {
enum class WeaponAvoidance { None, Dodge, Avoid, Evade };
// SUNITDMG_ApplyDodge: movement selects Evade exclusively.
inline WeaponAvoidance rollWeaponAvoidance(const CombatModifiers &mods,bool moving,bool missile,uint64_t &random) {
    const int chance=moving?mods.evade:missile?mods.avoid:mods.dodge;
    if(chance<=0 || limitedRandom(random,100)>=unsigned(std::clamp(chance,0,100))) return WeaponAvoidance::None;
    return moving?WeaponAvoidance::Evade:missile?WeaponAvoidance::Avoid:WeaponAvoidance::Dodge;
}
}
