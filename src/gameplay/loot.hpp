#pragma once
#include "core/id.hpp"
#include "definitions.hpp"
#include "monster_spawn.hpp"
#include <set>
#include <span>
#include <string_view>

namespace d2x {
struct LootRequest {
    EntityId source;
    MonsterIdentity identity;
    RegionId region = RegionId::Encampment;
    int difficulty = 0;
};
struct LootDrop {
    std::string_view code;
    unsigned quantity;
    Vec offset;
};
struct LootState {
    uint64_t randomState = 0;
    std::set<EntityId> settled;
};
class LootSystem {
    uint64_t randomState_;
    std::set<EntityId> settled_;

  public:
    static constexpr uint64_t defaultSeed = 0xd2;
    explicit LootSystem(uint64_t seed = defaultSeed) : randomState_(seed) {}
    LootState snapshot() const { return {randomState_, settled_}; }
    void restore(LootState state) noexcept {
        randomState_ = state.randomState;
        settled_.swap(state.settled);
    }
    static constexpr std::string_view unavailableReason =
        "Monster loot unavailable: original drop execution and item quality rules are not implemented.";
    std::vector<LootDrop> settle(LootRequest request);
};
} // namespace d2x
