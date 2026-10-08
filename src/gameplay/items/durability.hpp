#pragma once
#include "state.hpp"
#include "definitions.hpp"
#include "modifiers.hpp"
#include <algorithm>
#include <span>
namespace d2x {
// D2Game PlrModes::sub_6FC80B90: normal ammunition is destroyed at zero;
// magical throwing weapons and the native throwable stat retain their identity.
inline bool retainsEmptyStack(const ItemInstance &item,const ItemDefinition &base,std::span<const ResolvedItemStat> stats) {
    if(std::any_of(stats.begin(),stats.end(),[](const auto &s){return s.effect=="item_throwable" && s.value;})) return true;
    if(!base.equipment.isType("weap") || (!base.equipment.throwable && base.maxStack<=1)) return false;
    return item.quality==ItemQuality::Magic || item.quality==ItemQuality::Rare || item.quality==ItemQuality::Crafted ||
        item.quality==ItemQuality::Set || item.quality==ItemQuality::Unique;
}
}
