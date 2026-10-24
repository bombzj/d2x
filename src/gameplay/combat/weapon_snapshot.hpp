#pragma once
#include "weapon_values.hpp"
#include "gameplay/stats/stat_list.hpp"
namespace d2x {
struct SkillFormulaCatalog;
WeaponDamage sampleNativeWeapon(const WeaponDamage &,const StatSnapshot &,const StatRuleCatalog &,
    const CombatModifiers &procedures,bool monster,int classFactor=0,const EquipmentStats *equipment=nullptr,
    const SkillFormulaCatalog *formulas=nullptr,bool dualWield=false);
}
