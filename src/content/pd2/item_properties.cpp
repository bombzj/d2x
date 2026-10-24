#include "item_properties.hpp"
#include "content/classic_data.hpp"
#include "gameplay/items/stat_evaluation.hpp"
#include "gameplay/loot/special.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <array>
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace d2x {
namespace {
// Current core 0x206F80 initializes pool 8 in this order. These are native
// algorithm identities, not every sorceress record or a filtered tree scan.
constexpr std::array pool{39,45,44,55,59,369,64,38,49,48,53,43,36,47,46,51,56,383,62,376};
}
int rollPd2SorceressProperty(uint64_t &random) {return pool[pd2UnitEventRandom(random)%pool.size()];}
std::vector<ResolvedItemStat> resolvePd2SorceressProperty(const ClassicData &data,const PropertyRange &property,int skill,int level) {
    if(std::find(pool.begin(),pool.end(),skill)==pool.end()) throw std::runtime_error("Invalid PD2 random sorceress skill identity");
    const auto &table=data.tables.at("skills");size_t row{};
    for(;row<table.rows().size();++row) if(table.number(row,"Id")==skill) break;
    if(row==table.rows().size()) throw std::runtime_error("Missing PD2 random sorceress skill record");
    int bonus{};
    const auto [end,error]=std::from_chars(property.parameter.data(),property.parameter.data()+property.parameter.size(),bonus);
    if(error!=std::errc{} || end!=property.parameter.data()+property.parameter.size()) bonus=0;
    auto chance=property.minimum.value_or(0),rank=property.maximum.value_or(0);
    if(table.number(row,"SrcDam").value_or(0)>0) chance=nativeMultiplyDivide(chance,67,100);
    rank=nativeSubtract(rank,nativeSubtract(table.number(row,"reqlevel").value_or(0),6)/6);
    std::vector<ResolvedItemStat> result;
    const auto append=[&](std::string_view name,int value,int layer) {
        if(!value) return;
        const auto found=std::find_if(data.itemStats.begin(),data.itemStats.end(),[&](const auto &stat){return stat.name==name;});
        if(found==data.itemStats.end()) throw std::runtime_error("Missing PD2 random sorceress property stat");
        result.push_back(evaluateItemStat(*found,value,layer,level));
    };
    append("item_singleskill",bonus,skill);
    append("item_skilloncast",chance,(skill<<6)|(rank&63));
    return result;
}
}
