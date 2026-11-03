#include "replenishment.hpp"
#include <algorithm>
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/items/pd2/charges.hpp"
#include "gameplay/items/modifiers.hpp"
#include "gameplay/items/definitions.hpp"
#include "gameplay/items/state.hpp"

namespace d2x {
std::optional<ItemRestoration> itemRestoration(const ItemInstance &item,const ItemDefinition &base,
    std::span<const ResolvedItemStat> stats,unsigned maximumDurability) {
    int repair=0,quantity=0,extra=0,charges=0;
    for(const auto &stat:stats) {
        if(stat.effect=="item_replenish_durability") repair=nativeAdd(repair,stat.value);
        if(stat.effect=="item_replenish_quantity") quantity=nativeAdd(quantity,stat.value);
        if(stat.effect=="item_extra_stack") extra=nativeAdd(extra,stat.value);
        if(stat.name=="item_replenish_charges") charges=nativeAdd(charges,stat.rawValue);
    }
    if(maximumDurability && !(item.nativeFlags&0x100u) && repair>0) return ItemRestoration{false,repair,maximumDurability};
    if(base.maxStack>1 && quantity>0) return ItemRestoration{true,quantity,unsigned(std::clamp(nativeAdd(int(base.maxStack),extra),1,511))};
    if(charges>0) if(const auto first=pd2FirstChargeLayer(item)) {
        const bool fullStatsHasCharges=std::any_of(stats.begin(),stats.end(),[](const auto &stat){return stat.name=="item_charged_skill";});
        if(fullStatsHasCharges && first->value>=0 && first->value<=65535)
            return ItemRestoration{false,charges,(unsigned(first->value)>>8)&255,true,first->layer};
    }
    return {};
}

unsigned itemRestorationValue(const ItemInstance &item,const ItemRestoration &rule) {
    if(rule.charges) {const auto first=pd2FirstChargeLayer(item);return first?unsigned(first->value)&255:rule.maximum;}
    return rule.quantity?item.quantity:item.durability;
}

bool itemRestorationActive(const ItemInstance &item,const ItemRestoration &rule) {
    return rule.charges || itemRestorationValue(item,rule)<rule.maximum;
}

uint64_t itemRestorationDue(uint64_t tick,const ItemRestoration &rule,bool initial) {
    const auto interval=2500/rule.rate;
    const uint64_t duration=uint64_t(rule.charges?(rule.halfInterval?interval/2:interval)+1:
        initial?interval+1:std::max(125,interval+1));
    return tick>UINT64_MAX-duration?UINT64_MAX:tick+duration;
}
} // namespace d2x
