#pragma once
#include "combat_values.hpp"
#include "gameplay/effects/definition.hpp"
#include <span>
#include <cstdint>
namespace d2x {
struct MonsterHit {
    std::array<int64_t,6> channels{}; // Poison is damage per native frame.
    int64_t mana{}, stamina{};
    uint64_t coldFrames{}, poisonFrames{}, stunFrames{};
    uint64_t slowFrames{};
    int slowPercent{};
    uint8_t hitClass{};
    bool critical{};
    bool knockback{};
};
struct MonsterHitStates { CombatStateDefinition cold, poison, slow; };
void monsterCritical(MonsterHit &,int chance,uint64_t &random);
// SUNITDMG_GetHitClass: base weapon class plus rotating elemental/critical tag.
uint8_t monsterDamageHitClass(const MonsterHit &,uint8_t base,uint32_t &cursor);
// MonsterMode::sub_6FC62470 and SUNITDMG_FillDamageValues; no unit mutation.
MonsterHit rollMonsterHit(int minimum, int maximum, int critical,
    std::span<const MonsterElementAttack> elements, int sourceDamage, uint64_t &random);
// SUNITDMG::sub_6FCC1870 returns whether GH should be suppressed.
bool monsterHitRecovery(int64_t damage,int64_t maximumLife,uint8_t hitClass,
    bool frozen,bool poisonOnly,bool hasMode,uint64_t &random);
}
