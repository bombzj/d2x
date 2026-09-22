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
#include <functional>

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
using TreasureVisitor = std::function<bool(const TreasureSelection &, uint64_t &)>;
TreasureRoll selectTreasure(std::span<const TreasureClass> classes, std::string_view root, uint64_t seed,
                           int level = 0, const TreasureVisitor &visitor = {});
struct LootRequest {
    EntityId source;
    MonsterIdentity identity;
    RegionId region = RegionId::Encampment;
    int difficulty = 0;
};
struct LootDrop {
    std::string code;
    unsigned quantity;
    Vec offset;
    unsigned level = 1;
};
struct LootPlan {
    uint64_t randomState = 0;
    unsigned noDrops = 0;
    std::string deferred;
    std::vector<LootDrop> drops;
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
    bool settled(EntityId source) const { return settled_.contains(source); }
    uint64_t randomState() const { return randomState_; }
    LootState snapshot() const { return {randomState_, settled_}; }
    void restore(LootState state) noexcept {
        randomState_ = state.randomState;
        settled_.swap(state.settled);
    }
    static constexpr std::string_view unavailableReason =
        "Monster loot partial: normal consumables, quivers and gold; unsupported batches are deferred.";
    std::vector<LootDrop> settle(LootRequest request, LootPlan plan);
};
} // namespace d2x
