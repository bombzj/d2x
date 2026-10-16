#include "damage.hpp"
#include "core/random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/stats/stat_list.hpp"
#include "gameplay/combat/poison.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x {
// SUnitDmg::ApplyDamageBonuses / FillDamageValues, unarmed unit path.
// D2MOO 5596f5c, MIT; docs/licenses/D2MOO.txt. The caller supplies the
// actual posted unit+mode+state snapshot, never a pet-only damage cache.
MonsterHit rollMonsterUnitHit(const StatSnapshot &values,const StatRuleCatalog &rules,int damagePercent,int sourceDamage,uint64_t &random) {
    const auto stat=[&](std::string_view name) {
        const auto id=rules.names.find(name);if(id==rules.names.end()) throw std::runtime_error("Missing native unit damage stat");
        return values.get({id->second,0});
    };
    const auto roll=[&](int32_t low,int32_t high) {
        return nativeAdd(low,high>low?int32_t(limitedRandom(random,uint32_t(nativeSubtract(high,low)))):0);
    };
    if(!sourceDamage) sourceDamage=128;
    MonsterHit hit;
    const auto normal=nativeShiftLeft(stat("item_normaldamage"),8);
    auto minimum=nativeAdd(nativeShiftLeft(std::max(stat("mindamage"),1),8),normal);
    auto maximum=nativeAdd(nativeShiftLeft(std::max(stat("maxdamage"),2),8),normal);
    if(minimum<1) minimum=256;
    if(maximum<=minimum) maximum=nativeAdd(minimum,256);
    const auto percent=std::max(-90,nativeAdd(nativeAdd(damagePercent,stat("damagepercent")),stat("strength")));
    minimum=nativeAdd(minimum,nativePercentage(minimum,nativeAdd(percent,stat("item_mindamage_percent")),100));
    maximum=nativeAdd(maximum,nativePercentage(maximum,nativeAdd(percent,stat("item_maxdamage_percent")),100));
    const auto physical=std::max(0,roll(minimum,maximum));
    hit.channels[0]=sourceDamage==128?physical:nativePercentage(physical,sourceDamage,128);
    const auto critical=stat("passive_critical_strike"),deadly=stat("item_deadlystrike");
    if((critical>0 && int32_t(rollRandom(random)%100)<critical) || (deadly>0 && int32_t(rollRandom(random)%100)<deadly)) {
        hit.critical=true;hit.channels[0]=nativeMultiply(nativeChecked(hit.channels[0]),2);
    }
    struct Element {size_t channel;std::string_view minimum,maximum,mastery;};
    constexpr std::array<Element,5> elements{{
        {2,"firemindam","firemaxdam","passive_fire_mastery"},
        {3,"lightmindam","lightmaxdam","passive_ltng_mastery"},
        {4,"coldmindam","coldmaxdam","passive_cold_mastery"},
        {1,"magicmindam","magicmaxdam","passive_mag_mastery"},
        {5,"poisonmindam","poisonmaxdam","passive_pois_mastery"}}};
    for(const auto &element:elements) {
        const auto shift=element.channel==5?0u:8u;
        auto high=nativeShiftLeft(stat(element.maximum),shift);
        if(high<(element.channel==5?1:8)) continue;
        auto low=nativeShiftLeft(stat(element.minimum),shift);const auto mastery=stat(element.mastery);
        low=nativeAdd(low,nativePercentage(low,mastery,100));high=nativeAdd(high,nativePercentage(high,mastery,100));
        hit.channels[element.channel]=nativeMultiplyDivide(std::max(0,roll(low,high)),sourceDamage,128);
    }
    if(hit.channels[4]>0) hit.coldFrames=uint32_t(nativeMultiplyDivide(stat("coldlength"),sourceDamage,128));
    if(hit.channels[5]>0) hit.poisonFrames=uint32_t(weaponPoisonLength(stat("poisonlength"),stat("poison_count"),0,stat("skill_poison_override_length")));
    hit.stunFrames=uint32_t(nativeMultiplyDivide(stat("stunlength"),sourceDamage,128));
    const auto drain=[&](std::string_view low,std::string_view high) {
        const auto minimum=stat(low),maximum=stat(high);
        return minimum || maximum?roll(nativeShiftLeft(minimum,8),nativeShiftLeft(maximum,8)):0;
    };
    hit.lifeDrain=drain("lifedrainmindam","lifedrainmaxdam");hit.mana=drain("manadrainmindam","manadrainmaxdam");
    hit.stamina=drain("stamdrainmindam","stamdrainmaxdam");
    hit.burn=drain("burningmin","burningmax");hit.burnFrames=uint32_t(stat("firelength"));return hit;
}
void monsterCritical(MonsterHit &hit,int chance,uint64_t &random) {
    if(chance && limitedRandom(random,100)<unsigned(chance)) {hit.critical=true;for(auto &channel:hit.channels) channel=nativeMultiply(nativeChecked(channel),2);}
}
uint8_t monsterDamageHitClass(const MonsterHit &hit,uint8_t base,uint32_t &cursor) {
    if(base&0xf0) return base;
    // MONSTER_ApplyCriticalDamage sets the critical high nibble before
    // ExecuteMissileDamage; an existing high nibble skips elemental rotation.
    if(hit.critical) return uint8_t(base|0x10);
    constexpr std::array<size_t,4> channels{4,2,3,5};
    constexpr std::array<uint8_t,4> tags{0x30,0x20,0x40,0x50};
    const auto start=cursor++%4;
    uint8_t tag=0;
    for(unsigned offset=0;offset<4;++offset) {
        const auto index=(start+offset)%4;
        if(hit.channels[channels[index]]>0) {tag=tags[index];break;}
    }
    return uint8_t((base?base:0x0d)|tag);
}
bool monsterHitRecovery(int64_t damage,int64_t maximumLife,uint8_t hitClass,
    bool frozen,bool poisonOnly,bool hasMode,uint64_t &random,bool stunned) {
    // SUNITDMG checks STATE_STUNNED before sub_6FCC1870, without rolling.
    if(stunned) return hasMode;
    if(frozen || poisonOnly || damage<256) return false;
    int divisor=16;
    switch(hitClass) {
    case 2:case 6:case 10:case 11:divisor=8;break;
    case 5:divisor=64;break;
    case 4:case 8:divisor=32;break;
    default:break;
    }
    if(damage<maximumLife/divisor) return false;
    if(damage<maximumLife/(divisor/2) && !(rollRandom(random)&1)) return false;
    if(damage<maximumLife/(divisor/4) && !(rollRandom(random)&3)) return false;
    return hasMode;
}
}
