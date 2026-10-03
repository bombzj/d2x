#pragma once
#include <cstdint>

namespace d2x {
struct AttackElements;
struct WeaponDamage;
struct SkillCastSpec;
void applyWeaponSkillElements(AttackElements &elements, const WeaponDamage &weapon,
                              const SkillCastSpec *skill, uint64_t &random, unsigned &vengeanceHit);
} // namespace d2x
