#include "hireling_hit.hpp"
#include "server/systems/monsters/system.hpp"
#include <algorithm>
namespace d2x::server::combat {
int hirelingDamagePercent(const monsters::Actor &source,const monsters::Actor &target) {
    return source.hireling?(target.hireling?25:target.rule.boss?source.rule.hirelingBossDamagePercent:100):100;
}
HirelingHitEffects hirelingHitEffects(const WeaponSkillDamage &attack,const monsters::Actor &target,
    EntityId source,EntityId credit,int physicalResistance,int64_t physical,int64_t total,uint64_t tick) {
    HirelingHitEffects result;
    if(attack.crushing) {
        const bool boss=target.rule.boss || target.identity.rank==MonsterRank::Unique || target.identity.rank==MonsterRank::SuperUnique;
        int divisor=(target.hireling?10:boss?8:4)*(attack.projectile?2:1);
        divisor+=divisor*int(std::max(1u,target.admittedPlayerCount)-1)/2;
        result.crushing=target.life/divisor*(100-std::clamp(physicalResistance,0,100))/100;
    }
    const int64_t dealt=std::min(std::max<int64_t>(0,target.life-result.crushing),std::max<int64_t>(0,physical));
    // Native hirelings are exempt from DifficultyLevels' player leech divisor.
    result.healing=dealt*int64_t(attack.lifeLeech)*64/100*std::max(0,target.rule.drain)/100/64;
    if(attack.openWounds && total+result.crushing>0 && target.rule.openWoundsState>=0) {
        int rate=40,remaining=std::max(0,attack.level-1);constexpr int increments[]{9,18,27,36,45};
        for(int tier=0;tier<5 && remaining;++tier) {const int levels=std::min(remaining,tier==0?14:tier==4?99:15);rate+=levels*increments[tier];remaining-=levels;}
        if(target.identity.rank==MonsterRank::Champion || target.identity.rank==MonsterRank::Unique || target.identity.rank==MonsterRank::SuperUnique) rate/=2;
        result.wound=OpenWoundsApplication{rate,source,credit,tick+200,tick+1};
    }
    return result;
}
}
