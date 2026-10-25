#include "loot_program.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <array>

namespace d2x::pd2 {
bool visitMapBonusTreasure(const MapLootRules &rules,const MapMonsterLoot &unit,uint64_t &random,const MapTreasureVisitor &visit) {
    if(unit.minion) return true; // Current 0x37819E tests MONTYPEFLAG_MINION.
    const auto stat=[&](uint16_t id){return unit.stats.get({id,0});};
    const bool skirmish=stat(493)>0;
    const int multiplier=skirmish && !unit.boss?2:1;
    const auto skirmishRoll=pd2UnitEventRandom(random)%100;
    if(skirmish) {
        if((!unit.boss || skirmishRoll<25) && !visit({},random)) return false;
        if(unit.boss) pd2UnitEventRandom(random);
    }
    switch(unit.area) {
    case 152:case 153:case 164:case 165:case 171:case 172:case 186:case 187:
        if(!visit({},random)) return false;break;
    }
    if(unit.area>=197 && unit.area<=199 && !visit({},random)) return false;
    const auto extraRoll=pd2UnitEventRandom(random)%100;
    if(stat(273)>0 && int(extraRoll)<stat(273) && !visit({},random)) return false;
    const auto roll=[&](uint16_t id,bool drawWhenAbsent) {
        const int chance=stat(id);
        if(!drawWhenAbsent && chance<=0) return true;
        const auto rolled=pd2UnitEventRandom(random)%100;
        if(chance<=0 || int(rolled)>=nativeMultiply(multiplier,chance)) return true;
        return visit(rules.treasures.at(id),random);
    };
    for(const uint16_t id:{494,495,496,497}) if(!roll(id,true)) return false;
    // The class-specific loop consumes a roll only for a positive source.
    for(const uint16_t id:{279,280,281,285,282,283,284}) if(!roll(id,false)) return false;
    for(const uint16_t id:{502,506}) if(!roll(id,true)) return false;
    return true;
}
}
