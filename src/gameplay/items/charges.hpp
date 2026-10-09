#pragma once
#include "gameplay/items/state.hpp"

namespace d2x {
// All original saved property lists retain their own packed 204 layer. The
// caller decides whether a vendor, recipe or quest may recharge the item.
template<class Item,class Visitor>
void visitItemChargeStats(Item &item,Visitor visitor) {
    const auto visit=[&](auto &list) {for(auto &stat:list) if(stat.id==204) visitor(stat);};
    visit(item.savedStats);
    visit(item.runewordStats);
    for(auto &list:item.savedSetStats) visit(list);
}
inline bool itemHasMissingSkillCharges(const ItemInstance &item) {
    bool missing=false;
    visitItemChargeStats(item,[&](const auto &stat){missing|=(stat.value&255)<((unsigned(stat.value)>>8)&255);});
    return missing;
}
inline void rechargeItemSkills(ItemInstance &item) {
    visitItemChargeStats(item,[](auto &stat){stat.value=(stat.value&~255)|((unsigned(stat.value)>>8)&255);});
}
}
