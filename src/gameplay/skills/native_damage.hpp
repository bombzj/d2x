#pragma once
#include "gameplay/missiles/damage_data.hpp"
#include <cstdint>

namespace d2x {
class SkillFormulaValues;
// Skills::RollPhysicalDamage, RollElementalDamage and sub_6FD11E40.
// These functions use the actual casting unit's seed, without a missile seed.
void addNativeSkillElement(MissileHitDamage &,MissileDamageElement,int32_t damage,int32_t length,uint64_t &random);
int32_t rollNativeSkillElement(MissileHitDamage &,SkillFormulaValues &,int skill,int rank,uint64_t &random);
MissileHitDamage rollNativeSkillDamage(SkillFormulaValues &,int skill,int rank,uint64_t &random);
// FillDamageValues' elemental part. Input is the selected native stat view
// (unmerged weapon damage stats for kicks). Ordinary monsters roll raw drains.
void fillNativeWeaponElements(MissileHitDamage &,const StatSnapshot &,const StatRuleCatalog &,int sourceDamage,uint64_t &random,bool nonHirelingMonster=false);
}
