#pragma once
#include <span>
#include <optional>
#include <cstdint>
namespace d2x {
struct ItemInstance;
struct ResolvedItemStat;
struct ItemDefinition;
struct ItemRestoration {bool quantity{};int rate{};unsigned maximum{};bool charges{};int layer{};bool halfInterval{};};
struct ItemRestorationClock {uint64_t due{};int rate{};bool quantity{},charges{},halfInterval{};};
// ItemMode::sub_6FC4A2E0: durability takes priority even when full.
// Identification is unrelated to the item's regeneration.
std::optional<ItemRestoration> itemRestoration(const ItemInstance &item,const ItemDefinition &base,
    std::span<const ResolvedItemStat> stats,unsigned maximumDurability);
unsigned itemRestorationValue(const ItemInstance &item,const ItemRestoration &rule);
bool itemRestorationActive(const ItemInstance &item,const ItemRestoration &rule);
uint64_t itemRestorationDue(uint64_t tick,const ItemRestoration &rule,bool initial);
}
