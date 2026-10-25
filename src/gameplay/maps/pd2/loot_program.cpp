#include "loot_program.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <array>

namespace d2x::pd2 {
bool visitSpecialMonsterDrops(const MapLootRules &rules,const MapMonsterLoot &unit,uint64_t &random,const SpecialDropVisitor &visit) {
    const bool map=unit.area>=137 && unit.area<=210;
    const int multiplier=map && unit.stats.get({493,0})>0 && !unit.boss?2:1;
    const auto roll=[&](int treasure) {
        const auto rule=rules.specialChances.at(treasure);
        uint32_t denominator=rule.denominator;
        if(!denominator || denominator==UINT16_MAX) return false;
        constexpr std::array scale{127u,125u,123u,121u,119u,117u,115u,113u};
        if(unit.players>1 && unit.players<=8) denominator=scale[size_t(unit.players-1)]*denominator/127;
        const uint32_t factor=rule.factor>=2 && rule.factor<UINT16_MAX?rule.factor:1;
        return int32_t(pd2UnitEventRandom(random)%(factor*denominator))<multiplier;
    };
    // Core 0x377DE0 / 0x3744F0. These are independent item creations,
    // before the minion exclusion and the map's extra TC calls.
    if(unit.undead && roll(1022+unit.difficulty) && !visit("rtmo",-1,random)) return false;
    constexpr std::array<std::pair<int,std::string_view>,8> materials{{
        {998,"lbox"},{1001,"lpp"},{1004,"rkey"},{1010,"rtp"},
        {1007,"rid"},{1013,"llmr"},{1016,"lsvl"},{1039,"imrn"}}};
    for(const auto &[treasure,code]:materials)
        if(roll(treasure+unit.difficulty) && !visit(code,-1,random)) return false;
    if(!map) return true;
    for(size_t tier=0;tier<rules.tiers.size();++tier) if(rules.tiers[tier].contains(unit.area)) {
        constexpr std::array<std::string_view,3> codes{"ubaa","ubab","ubac"};
        if(roll(1025+int(tier)) && !visit(codes[tier],-1,random)) return false;
        break;
    }
    if(roll(rules.uniqueMapBosses.contains(unit.nativeClass)?1021:1020)) {
        const auto &drop=rules.uniqueMaps[pd2UnitEventRandom(random)&7];
        if(!visit(drop.code,drop.uniqueRow,random)) return false;
    }
    return true;
}
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
