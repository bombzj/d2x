#pragma once
#include <cstddef>
#include <optional>
#include <span>

namespace d2x {
struct ItemInstance;
struct ResolvedItemStat;
struct Pd2ChargeLayer {int layer{},value{};std::size_t index{};bool runeword{};};
std::optional<Pd2ChargeLayer> pd2FirstChargeLayer(const ItemInstance &item);
std::optional<Pd2ChargeLayer> pd2FindChargeLayer(const ItemInstance &item,int layer);
std::optional<Pd2ChargeLayer> pd2RestoreCharge(ItemInstance &item,int layer);
void pd2WriteChargeValues(std::span<ResolvedItemStat> values,const ItemInstance &item,const Pd2ChargeLayer &selected);
}
