#pragma once
#include "unique_modifiers.hpp"
#include "damage.hpp"
namespace d2x {
// Prepared native stats -> fixed-point source damage. No archive, actor or GPU.
struct MonsterEnchantmentDamageState {
    std::array<std::optional<AttackDamageRange>,6> spectral;
    int coldFrames{}, poisonFrames{};
};
// Retail 1.13c D2Game RVA 267BF-26804, after MonsterCEDamagePercent.
// The retail difficulty reductions are absent from D2MOO's 1.10f transcription.
int32_t fireEnchantedDeathMaximum(int32_t maximumLife,int32_t percentage,int difficulty);
// The owner stages these native stat overrides with its RNG before committing.
void addMonsterEnchantmentDamage(MonsterHit &,const MonsterEnchantment &,int sourceDamage,uint64_t &,
    MonsterEnchantmentDamageState &);
} // namespace d2x
