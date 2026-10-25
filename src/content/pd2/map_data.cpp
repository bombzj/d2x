#include "map_data.hpp"
#include "content/classic_data.hpp"
#include "content/skills/table_values.hpp"
#include "content/monsters/monster_identity.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>
#include <stdexcept>

namespace d2x::pd2 {
std::shared_ptr<const MapCatalog> prepareMapCatalog(const ClassicData &data) {
    if(!data.pd2Host || !data.nativeStats) throw std::invalid_argument("Missing PD2 map content");
    auto result=std::make_shared<MapCatalog>();
    const auto &costs=data.tables.at("itemstatcost");
    for(size_t row=0;row<costs.rows().size();++row) {
        const auto id=costs.number(row,"ID");if(!id) continue;
        const int divide=skillTableNumber(costs,row,"Divide",SkillNumberPolicy::BlankIsZero);
        if(*id<0 || *id>=512 || divide<0) throw std::runtime_error("Invalid PD2 map stat routing");
        if(divide && divide!=1024 && divide/1000<2 && !data.nativeStats->definitions.contains(uint16_t(divide%1000)))
            throw std::runtime_error("Missing PD2 map recipient stat");
        result->properties.transfers.emplace(uint16_t(*id),MapStatTransfer{uint32_t(divide)});
    }
    const auto stat=[&](std::string_view name){return data.nativeStats->names.at(std::string(name));};
    result->properties.forceEvent=stat("map_force_event");
    result->properties.sunderResistances={stat("fireresist"),stat("coldresist"),stat("lightresist"),
        stat("poisonresist"),stat("damageresist"),stat("magicresist")};
    for(const auto &[source,low,high,length]:std::array<std::array<std::string_view,4>,5>{{
        {{"map_mon_phys_as_extra_ltng","lightmindam","lightmaxdam",""}},
        {{"map_mon_phys_as_extra_cold","coldmindam","coldmaxdam","coldlength"}},
        {{"map_mon_phys_as_extra_fire","firemindam","firemaxdam",""}},
        {{"map_mon_phys_as_extra_pois","poisonmindam","poisonmaxdam","poisonlength"}},
        {{"map_mon_phys_as_extra_mag","magicmindam","magicmaxdam",""}}}})
        result->properties.physicalElements.emplace(stat(source),std::array<uint16_t,3>{stat(low),stat(high),length.empty()?uint16_t(UINT16_MAX):stat(length)});
    for(const auto &[name,state]:data.states) if(state.definition.id==198) result->state=state.definition;
    if(result->state.id!=198) throw std::runtime_error("Missing PD2 map state");
    // 0x37D610 anchors relative TC identities at MonStats class967's first
    // normal TC. The actual names, entries and weights stay MPQ-owned.
    const auto &monsters=data.tables.at("monstats");
    const auto anchor=monsters.value(compiledMonsterRow(monsters,967),"TreasureClass1");
    const auto base=std::find_if(data.treasures.begin(),data.treasures.end(),[&](const auto &entry){return entry.name==anchor;});
    if(base==data.treasures.end()) throw std::runtime_error("Missing PD2 map treasure anchor");
    for(const auto &[id,offset]:std::array<std::pair<uint16_t,size_t>,13>{{
        {494,982},{495,983},{496,984},{497,985},{502,986},{506,988},
        {279,1114},{280,1129},{281,1108},{285,1139},{282,1100},{283,1091},{284,1095}}}) {
        if(offset>=size_t(data.treasures.end()-base)) throw std::runtime_error("Missing PD2 map bonus treasure");
        result->loot.treasures.emplace(id,(base+offset)->name);
    }
    // Current 0x3746B0's six map-boss membership containers. These native
    // program identities are distinct from MonStats.boss and unique rank.
    result->loot.bosses={861,879,870,893,746,996,750,915,1242,
        1026,800,1025,894,884,997,963,882,1149,1240,1243,
        826,998,883,809,1121,1122,755,964,747,1000,1103,743,1104,1105,
        1058,1061,1062,1063,1060,1096,1094,1148,1166,1164,1175,1174};
    result->loot.uniqueMapBosses=result->loot.bosses;
    for(const int id:{743,1104,1105,1058,1061,1062,1063,1060,1096,1094,1148,1166,1164,1175,1174})
        result->loot.uniqueMapBosses.erase(id);
    // Native initializers 0x1995D0/1996B0/1997B0 own these membership sets.
    result->loot.tiers={std::set<int>{146,148,151,155,145,169,143,158,208},
        std::set<int>{174,142,175,156,141,167,160,150,193,209,210},
        std::set<int>{154,170,139,149,190,191,144,147,201}};
    for(const int first:{998,1001,1004,1007,1010,1013,1016,1022,1039}) for(int difficulty=0;difficulty<3;++difficulty) {
        const auto offset=first+difficulty;
        if(offset>=data.treasures.end()-base) throw std::runtime_error("Missing PD2 special drop chance");
        const auto &treasure=*(base+offset);
        result->loot.specialChances.emplace(offset,MapLootRules::Chance{
            uint16_t(treasure.quality[0].value_or(0)),uint16_t(treasure.quality[1].value_or(0))});
    }
    for(const int offset:{1020,1021,1025,1026,1027}) {
        const auto &treasure=*(base+offset);
        result->loot.specialChances.emplace(offset,MapLootRules::Chance{
            uint16_t(treasure.quality[0].value_or(0)),uint16_t(treasure.quality[1].value_or(0))});
    }
    const auto &misc=data.tables.at("misc"),&levels=data.tables.at("levels"),&objects=data.tables.at("objects");
    uint32_t itemIndex=0;
    for(const auto name:{"weapons","armor"}) {
        const auto &table=data.tables.at(name);
        for(size_t row=0;row<table.rows().size();++row) if(!table.value(row,"code").empty()) ++itemIndex;
    }
    for(size_t row=0;row<misc.rows().size();++row) {
        if(misc.value(row,"code").empty()) continue;
        const auto nativeIndex=itemIndex++;
        if(misc.number(row,"pSpell")!=12) continue;
        const auto code=std::string(misc.value(row,"code"));
        const int level=skillTableNumber(misc,row,"len");
        const int portal=misc.number(row,"missiletype").value_or(0);
        bool linked=false;
        for(size_t area=0;area<levels.rows().size();++area) if(levels.number(area,"Id")==level) {linked=true;break;}
        if(code.empty() || !data.items.find(code) || !linked || level<1 || level>255 || portal<0 || size_t(portal?portal:60)>=objects.rows().size())
            throw std::runtime_error("Invalid PD2 map item destination or portal");
        if(!result->items.emplace(code,MapItemDefinition{level,portal?portal:60,nativeIndex}).second)
            throw std::runtime_error("Duplicate PD2 map item");
    }
    constexpr std::array<std::pair<int,int>,8> uniqueMaps{{
        {176,432},{181,433},{183,436},{180,453},{192,452},{195,472},{197,478},{200,482}}};
    for(size_t index=0;index<uniqueMaps.size();++index) {
        const auto [level,uniqueRow]=uniqueMaps[index];
        const auto item=std::find_if(result->items.begin(),result->items.end(),[&](const auto &entry){return entry.second.level==level;});
        const auto &uniques=data.tables.at("uniqueitems");
        if(item==result->items.end() || size_t(uniqueRow)>=uniques.rows().size() || uniques.value(size_t(uniqueRow),"code")!=item->first)
            throw std::runtime_error("PD2 unique map identity mismatch");
        result->loot.uniqueMaps[index]={item->first,uniqueRow};
    }
    return result;
}
}
