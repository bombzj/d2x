#include "weapon_hit.hpp"
#include "gameplay/skills/weapon_damage.hpp"
#include "server/systems/monsters/system.hpp"

namespace d2x::server::combat {
int hirelingDamagePercent(const monsters::Actor &source, const monsters::Actor &target) {
    return source.hireling ? (target.hireling ? 25 : target.rule.boss ? source.rule.hirelingBossDamagePercent : 100) : 100;
}
WeaponHitEffects weaponHitEffects(const WeaponSkillDamage &attack, const monsters::Actor &target, EntityId source,
    EntityId credit, int physicalResistance, int64_t physical, LeechDivisors divisors, uint64_t tick, uint64_t &random) {
    const auto rank = target.identity.rank;
    const WeaponHitTarget input{target.life, physicalResistance, target.rule.drain, target.admittedPlayerCount,
        target.hireling, target.rule.boss || rank == MonsterRank::SuperUnique,
        rank == MonsterRank::Champion || rank == MonsterRank::Unique || rank == MonsterRank::SuperUnique};
    const auto hit = calculateWeaponHitEffects(attack, input, physical, divisors, random);
    WeaponHitEffects result{hit.crushing, hit.life, hit.mana, {}};
    if (hit.woundRate > 0 && target.rule.openWoundsState >= 0)
        result.wound = OpenWoundsApplication{hit.woundRate, source, credit, tick + 200, tick + 1};
    return result;
}
}
