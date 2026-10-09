#pragma once
#include "gameplay/items/state.hpp"
#include "gameplay/items/definitions.hpp"
#include "gameplay/items/modifiers.hpp"
#include <algorithm>
#include <span>
#include <optional>
#include <cstdint>
namespace d2x {
struct ItemRestoration {bool quantity{};int rate{};unsigned maximum{};};
struct ItemRestorationClock {uint64_t due{};int rate{};bool quantity{};};
// ItemMode::sub_6FC4A2E0: durability takes priority even when full.
// Identification is unrelated to the item's regeneration.
inline std::optional<ItemRestoration> itemRestoration(const ItemInstance &item,const ItemDefinition &base,
    std::span<const ResolvedItemStat> stats,unsigned maximumDurability) {
    int repair=0,quantity=0,extra=0;
    for(const auto &stat:stats) {
        if(stat.effect=="item_replenish_durability") repair+=stat.value;
        if(stat.effect=="item_replenish_quantity") quantity+=stat.value;
        if(stat.effect=="item_extra_stack") extra+=stat.value;
    }
    if(maximumDurability && !(item.nativeFlags&0x100u) && repair>0) return ItemRestoration{false,repair,maximumDurability};
    if(base.maxStack>1 && quantity>0) return ItemRestoration{true,quantity,unsigned(std::clamp(int(base.maxStack)+extra,1,511))};
    return {};
}
inline uint64_t itemRestorationDue(uint64_t tick,int rate,bool initial) {
    const uint64_t duration=uint64_t(initial?2500/rate+1:std::max(125,2500/rate+1));
    return tick>UINT64_MAX-duration?UINT64_MAX:tick+duration;
}
}
