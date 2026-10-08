#pragma once
#include "content/classic_data.hpp"
#include "gameplay/loot/plan.hpp"
namespace d2x {
// Materializes original prepared rolls, without allocating world identities or
// mutating authority. The caller commits the returned value and random state.
ItemInstance prepareItem(const ClassicData &, const LootDrop &, uint64_t &random, int difficulty);
}
