#include "cube.hpp"
#include "content/classic_data.hpp"
#include "content/items/item_properties.hpp"
#include "content/items/cube_data.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "gameplay/items/cube_records.hpp"
#include "gameplay/items/native_sources.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
bool pd2SacrificeEligible(const ClassicData &data,const ItemInstance &item) {
    const auto *base=data.items.find(item.definition);
    if(!item.identified || !base || !base->equipment.isType("amul")) return false;
    bool corrupted=false;
    for(const auto &stat:resolveItemStats(data,item,int(item.level))) {
        if((stat.name=="desecrated" || stat.name=="mirrored") && stat.rawValue) return false;
        if(stat.name=="corrupted" && stat.rawValue) corrupted=true;
    }
    return corrupted;
}
bool applyPd2AutomaticCorruption(const ClassicData &data,ItemInstance &item,uint64_t &itemRandom,uint32_t &programRandom,bool desecrate) {
    auto visible=item;visible.identified=true;
    int corruption=0;
    for(const auto &stat:resolveItemStats(data,visible,int(item.level))) {
        if(stat.name=="corrupted" && stat.rawValue) {if(!desecrate) return false;corruption=stat.rawValue;}
        if(stat.name=="desecrated" && stat.rawValue) return false;
    }
    const auto *base=data.items.find(item.definition);
    if(!base) return false;
    int threshold=0;uint32_t roll=0;bool drawn=false;
    // Core 0x3632D0 scans enabled corruption rows against the current item.
    // Automatic map corruption and a bricked rare both use this attribute
    // program; it never destroys/replaces the item as a manual WSS input can.
    for(size_t index=0;index<data.cubeRecipes.size();++index) {
        const auto &recipe=data.cubeRecipes[index];
        if((recipe.operation!=16 && recipe.operation!=18) || recipe.operationParameter!=361 ||
            !recipe.operationValue || recipe.inputs.empty() || recipe.outputs.empty()) continue;
        if(recipe.inputs.size()>1 && recipe.inputs[1].code!="wss" && data.items.find(recipe.inputs[1].code)) continue;
        if(!matchesCubeInput(recipe.inputs.front(),item,*base,data.cubeBases.at(item.definition))) continue;
        const auto &output=recipe.outputs.front();
        if(output.kind==CubeOutputKind::UseType && output.quality==ItemQuality::Rare && output.properties.empty()) {
            threshold=recipe.operationValue;
            if(threshold<0 || threshold>=1000) throw std::runtime_error("Invalid PD2 corruption threshold");
            roll=uint32_t(threshold)+pd2CrtRandom(programRandom)%uint32_t(1000-threshold);drawn=true;
            if(desecrate) {threshold=851;roll=851+pd2CrtRandom(programRandom)%149;}
        }
        if(threshold && roll && roll<uint32_t(recipe.operationValue)) {
            auto properties=output;
            const auto first=properties.properties.empty()?data.properties.end():std::find_if(data.properties.begin(),data.properties.end(),
                [&](const auto &property){return property.code==properties.properties.front().property.code;});
            if(desecrate && first!=data.properties.end() && int(first->row)==corruption) {
                roll=uint32_t(threshold)+pd2CrtRandom(programRandom)%uint32_t(1000-threshold);
                index=0;while(index<data.cubeRecipes.size() && data.cubeRecipes[index].row<351) ++index;
                if(index==data.cubeRecipes.size()) break;
                --index;continue;
            }
            if(desecrate) for(auto &property:properties.properties) for(const auto &source:data.properties) {
                if(source.code!=property.property.code || (source.row!=270 && source.row!=271)) continue;
                const auto replacement=std::find_if(data.properties.begin(),data.properties.end(),[&](const auto &entry){return entry.row==source.row+155;});
                if(replacement==data.properties.end()) throw std::runtime_error("Missing native desecration property link");
                property.property.code=replacement->code;break;
            }
            for(auto &property:properties.properties) property.chance=0;
            applyCubeProperties(data,properties,item,itemRandom);item.nativePropertyRandom=itemRandom;break;
        }
    }
    return drawn;
}
bool pd2CubeCorruptionAvailable(const ClassicData &data,std::span<const ItemInstance *const> items,int level) {
    bool material=false;
    for(const auto *item:items) if(item->definition=="wss" || item->definition=="cwss") {
        material=true;auto visible=*item;visible.identified=true;
        for(const auto &stat:resolveItemStats(data,visible,level)) if(stat.name=="corrupted" && stat.rawValue) return false;
    }
    return material;
}
bool matchesPd2CubeCondition(const ClassicData &data,const CubeRecipe &recipe,const ItemInstance &item,int level) {
    if(recipe.operation!=16 && recipe.operation!=18) return true;
    const auto rule=data.nativeStats->definitions.find(uint16_t(recipe.operationParameter));
    if(recipe.operationParameter<0 || recipe.operationParameter>UINT16_MAX || rule==data.nativeStats->definitions.end()) return false;
    // The native input matcher applies operations 15..26 only to input slot 0.
    // Sample the item's complete native list, including its socket children;
    // identification controls display, not CubeMain's predicate.
    auto visible=item;visible.identified=true;
    EquipmentContribution contribution;contribution.item=item.id;
    contribution.stats=resolveItemStats(data,visible,level);
    const auto sources=prepareNativeEquipmentSources(data.items,*data.nativeStats,std::span(&contribution,1),{},
        [&](EntityId)->const ItemInstance &{return visible;});
    const auto &root=*sources.items.at(item.id);
    const auto values=evaluateStatList(*data.nativeStats,root);
    const auto value=values.get({uint16_t(recipe.operationParameter),0});
    const auto expected=int32_t(uint32_t(recipe.operationValue)>>(rule->second.valueShift&31));
    return recipe.operation==16?value<=expected:value==expected;
}
}
