#include "gameplay/loot/loot.hpp"

namespace d2x {
std::vector<LootDrop> LootSystem::settle(LootRequest request, LootPlan plan) {
    if (!request.source || !settled_.insert(request.source).second)
        return {};
    randomState_ = plan.randomState;
    if (!plan.deferred.empty())
        return {};
    return std::move(plan.drops);
}
} // namespace d2x
