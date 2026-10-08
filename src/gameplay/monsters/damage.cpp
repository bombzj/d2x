#include "damage.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x {
void monsterCritical(MonsterHit &hit,int chance,uint64_t &random) {
    if(chance && limitedRandom(random,100)<unsigned(chance)) for(auto &channel:hit.channels) channel*=2;
}
bool monsterHitRecovery(int64_t damage,int64_t maximumLife,uint8_t hitClass,
    bool frozen,bool poisonOnly,bool hasMode,uint64_t &random) {
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
MonsterHit rollMonsterHit(int minimum, int maximum, int critical,
    std::span<const MonsterElementAttack> elements, int sourceDamage, uint64_t &random) {
    MonsterHit hit;
    auto roll = [&](int low, int high, int shift) {
        const int64_t base = int64_t(low) << shift, spread = int64_t(std::max(0, high-low)) << shift;
        return base + (spread ? limitedRandom(random, uint32_t(spread)) : 0);
    };
    hit.channels[0] = roll(minimum, maximum, 8) * sourceDamage / 128;
    for (const auto &element : elements) {
        if (element.chance < 100 && limitedRandom(random,100) >= unsigned(element.chance)) continue;
        // MonStats poison is converted to fixed rate before the damage roll.
        const auto value = element.type=="pois"?roll(10*element.minimum,10*element.maximum,0):roll(element.minimum,element.maximum,8);
        if (element.type == "fire") hit.channels[2] += value * sourceDamage / 128;
        else if (element.type == "ltng") hit.channels[3] += value * sourceDamage / 128;
        else if (element.type == "mag") hit.channels[1] += value * sourceDamage / 128;
        else if (element.type == "cold") {
            hit.channels[4] += value * sourceDamage / 128;
            hit.coldFrames += uint64_t(element.durationFrames) * unsigned(sourceDamage) / 128;
        } else if (element.type == "pois") {
            hit.channels[5] += value * sourceDamage / 128;
            hit.poisonFrames += uint64_t(element.durationFrames) * 2;
        } else if (element.type == "mana") hit.mana += value;
        else if (element.type == "stam") hit.stamina += value;
        else if (element.type == "stun") hit.stunFrames += unsigned(element.durationFrames);
    }
    // MONSTER_ApplyCriticalDamage doubles all six damage channels.
    monsterCritical(hit,critical,random);
    return hit;
}
}
