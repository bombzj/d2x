#include "loot.hpp"

namespace d2x {
std::vector<LootDrop> LootSystem::settle(LootRequest request) {
    // Record death once, but do not consume randomness or invent missing rules.
    // The MPQ adapter exposes the original TC slots; the 1.04 selection algorithm
    // and item-quality generation still need a verified implementation.
    if (request.source)
        settled_.insert(request.source);
    return {};
}
} // namespace d2x
