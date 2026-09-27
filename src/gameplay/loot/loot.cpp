#include "gameplay/loot/loot.hpp"

namespace d2x {
std::vector<LootDrop> LootSystem::settle(LootRequest request, LootPlan plan) {
    if (!request.source || !settled_.insert(request.source).second)
        return {};
    if (!request.sourceSeed) randomState_ = plan.randomState;
    // A deferred TC entry must not discard other independently generated drops.
    for (const auto &drop : plan.drops)
        if (drop.generation.quality == ItemQuality::Unique && drop.generation.specialRow >= 0)
            usedUniques_.insert(uint32_t(drop.generation.specialRow));
    return std::move(plan.drops);
}
} // namespace d2x
