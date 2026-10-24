#pragma once
#include "gameplay/items/charges.hpp"
#include "gameplay/items/modifiers.hpp"
#include <optional>
#include <span>
#include <algorithm>

namespace d2x {
struct Pd2ChargeLayer {int layer{},value{};};
inline std::optional<Pd2ChargeLayer> pd2FirstChargeLayer(const ItemInstance &item) {
    std::optional<Pd2ChargeLayer> result;
    visitItemChargeStats(item,[&](const auto &stat) {
        if(!result || stat.parameter<result->layer) result=Pd2ChargeLayer{stat.parameter,stat.value};
    });
    return result;
}
inline void pd2RestoreCharge(ItemInstance &item,int layer) {
    bool restored=false;
    visitItemChargeStats(item,[&](auto &stat) {
        if(!restored && stat.parameter==layer && (stat.value&255)<((unsigned(stat.value)>>8)&255)) {++stat.value;restored=true;}
    });
}
inline void pd2RestoreChargeValues(std::span<ResolvedItemStat> values,int layer) {
    for(auto &stat:values) if(stat.name=="item_charged_skill" && stat.layer==layer) {
        ++stat.rawValue;++stat.value;if(stat.nativeValue) ++*stat.nativeValue;
    }
}
}
