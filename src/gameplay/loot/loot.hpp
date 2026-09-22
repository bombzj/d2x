#pragma once
#include "core/id.hpp"
#include "gameplay/model/definitions.hpp"
#include "gameplay/monsters/monster_spawn.hpp"
#include <set>
#include <span>
#include <string_view>
#include <optional>
#include <string>
#include <vector>

namespace d2x {
struct TreasureClass {
    std::string name;
    std::vector<std::string> codes;
    std::vector<int> weights;
    std::optional<int> picks, noDrop, group, level;
    std::array<std::optional<int>, 4> quality{};
};
struct TreasureSelection {
    std::string code;
    std::array<int, 4> quality{};
    std::vector<std::string> path;
};
struct TreasureRoll {
    std::string root;
    uint64_t randomState = 0;
    unsigned noDrops = 0;
    std::vector<TreasureSelection> selections;
};
TreasureRoll selectTreasure(std::span<const TreasureClass> classes, std::string_view root, uint64_t seed,
                           int level = 0);
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
