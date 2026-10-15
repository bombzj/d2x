#pragma once
#include "gameplay/combat/stat_modifiers.hpp"
#include <algorithm>
#include "core/random.hpp"
namespace d2x {
enum class WeaponAvoidance { None, Dodge, Avoid, Evade };
struct NativeBlockOrDodge {bool block{},weaponBlock{};WeaponAvoidance avoidance{WeaponAvoidance::None};};
inline NativeBlockOrDodge rollNativeBlockOrDodge(int block,bool physical,bool moving,int weaponBlock,bool dualClaws,
    const CombatModifiers &mods,bool missile,uint64_t &random) {
    if(physical && block>0 && int(rollRandom(random)%100)<block) return {true};
    if(moving) {
        if(mods.evade>0 && int(rollRandom(random)%100)<mods.evade) return {false,false,WeaponAvoidance::Evade};
        return {};
    }
    if(weaponBlock>0 && dualClaws && int(rollRandom(random)%100)<weaponBlock) return {true,true};
    const auto chance=missile?mods.avoid:mods.dodge;
    if(chance>0 && int(rollRandom(random)%100)<chance) return {false,false,missile?WeaponAvoidance::Avoid:WeaponAvoidance::Dodge};
    return {};
}
// SUNITDMG_ApplyDodge: movement selects Evade exclusively.
inline WeaponAvoidance rollWeaponAvoidance(const CombatModifiers &mods,bool moving,bool missile,uint64_t &random) {
    const int chance=moving?mods.evade:missile?mods.avoid:mods.dodge;
    if(chance<=0 || limitedRandom(random,100)>=unsigned(std::clamp(chance,0,100))) return WeaponAvoidance::None;
    return moving?WeaponAvoidance::Evade:missile?WeaponAvoidance::Avoid:WeaponAvoidance::Dodge;
}
}
