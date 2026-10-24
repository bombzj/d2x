#include "cube_inputs.hpp"
#include "cube.hpp"
#include "content/classic_data.hpp"
#include "gameplay/items/cube_records.hpp"
#include <algorithm>
#include <functional>

namespace d2x {
std::optional<Pd2CubeInputs> matchPd2CubeInputs(const ClassicData &data,const CubeRecipe &recipe,
    const std::vector<const ItemInstance *> &items,int level,int difficulty) {
    std::vector<unsigned> used(items.size());Pd2CubeInputs result;
    const auto match=[&](auto &&self,size_t slot,size_t index,unsigned remaining)->bool {
        if(slot==recipe.inputs.size()) return std::all_of(used.begin(),used.end(),[](auto count){return count>0;});
        if(!remaining) return self(self,slot+1,0,slot+1<recipe.inputs.size()?recipe.inputs[slot+1].quantity:0);
        if(index==items.size()) return false;
        const auto &wanted=recipe.inputs[slot];const auto &item=*items[index];const auto *base=data.items.find(item.definition);
        if(base && matchesCubeInput(wanted,item,*base,data.cubeBases.at(item.definition)) &&
            (slot || matchesPd2CubeCondition(data,recipe,item,level)) &&
            (recipe.operation!=28 || !base->questTag || item.nativeQuestDifficulty>=unsigned(difficulty))) {
            const unsigned quantity=base->maxStack>1 && item.quality==ItemQuality::Normal?item.quantity:1;
            const auto available=quantity>used[index]?quantity-used[index]:0;
            for(auto take=std::min(available,remaining);take;--take) {
                const bool first=!used[index];used[index]+=take;
                if(first) result.ordered.push_back(&item);
                if(self(self,slot,index+1,remaining-take)) return true;
                used[index]-=take;if(first) result.ordered.pop_back();
            }
        }
        return self(self,slot,index+1,remaining);
    };
    if(recipe.inputs.empty() || !match(match,0,0,recipe.inputs.front().quantity)) return {};
    for(size_t index=0;index<items.size();++index) if(used[index]) result.consumed.emplace_back(items[index],used[index]);
    return result;
}
}
