#pragma once
#include "unique_modifiers.hpp"
#include "damage.hpp"
namespace d2x {
// Prepared native stats -> fixed-point source damage. No archive, actor or GPU.
struct MonsterEnchantmentDamageState {
    std::array<std::optional<AttackDamageRange>,6> spectral;
    int coldFrames{}, poisonFrames{};
};
// The owner stages these native stat overrides with its RNG before committing.
void addMonsterEnchantmentDamage(MonsterHit &,const MonsterEnchantment &,int sourceDamage,uint64_t &,
    MonsterEnchantmentDamageState &);
} // namespace d2x
