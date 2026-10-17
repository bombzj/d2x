#pragma once
#include "content/classic_data.hpp"
#include "gameplay/loot/plan.hpp"
#include "gameplay/loot/quality.hpp"
#include <cstddef>
#include <optional>
#include <set>
#include <string_view>
namespace d2x {
// Materializes original prepared rolls, without allocating world identities or
// mutating authority. The caller commits the returned value and random state.
ItemInstance prepareItem(const ClassicData &, const LootDrop &, uint64_t &random, int difficulty,int staffmodBias=0);
enum class NativeItemUse { Equipment, Vendor };
DropQuality nativeDropQuality(ItemQuality);
ItemInstance prepareNativeItem(const ClassicData &,std::string_view code,int level,int difficulty,
    uint64_t &gameRandom,std::optional<DropQuality>,const std::set<size_t> &uniques,NativeItemUse);
}
