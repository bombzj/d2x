#pragma once
#include "gameplay/combat/pd2/item_modifiers.hpp"
#include <array>
#include <string_view>
namespace d2x {
struct StatSnapshot;struct StatRuleCatalog;struct StatList;
struct WeaponDamage;struct EquipmentStats;struct SkillFormulaCatalog;
Pd2ItemModifiers pd2ItemModifiers(const StatSnapshot &,const StatRuleCatalog &);
void appendPd2ItemModifiers(StatList &,const StatRuleCatalog &,const Pd2ItemModifiers &);
void applyPd2WeaponParameters(Pd2ItemModifiers &,const StatSnapshot &,const StatRuleCatalog &,
    const WeaponDamage &,const EquipmentStats *,const SkillFormulaCatalog &,bool dualWield);
std::array<int,3> pd2WeaponMastery(const StatSnapshot &,const StatRuleCatalog &,const WeaponDamage &,
    const EquipmentStats *,const SkillFormulaCatalog &,bool dualWield,std::string_view group);
}
