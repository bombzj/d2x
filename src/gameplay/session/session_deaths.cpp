#include "session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/rewards/death_settlement.hpp"
#include "content/monsters/death_loot.hpp"
#include <iostream>
#include <utility>

namespace d2x {
class GameSessionImpl::DeathSettlementAdapter final : public IDeathSettlementHost {
    GameSessionImpl &host_;
  public:
    explicit DeathSettlementAdapter(GameSessionImpl &host) : host_(host) {}
    DeathSettlementContext capture(const EnemyDied &death) const override {
        return {host_.state().player.id, host_.characterDefinition_.code, host_.deathQuestContext(death)};
    }
    void applyQuest(const EnemyDied &death, const QuestDeathPlan &plan) override { host_.applyDeathQuest(death, plan); }
    LootPlan planLoot(const EnemyDied &death, const LootRequest &request,
            DeathLootContext context, const std::set<uint32_t> &usedUniques) override {
        return host_.planDeathLoot(death, request, context, usedUniques);
    }
    void publishDeferred(EntityId source, std::string_view reason) override {
        // Simulation private access stays inside GameSessionImpl, not this adapter.
        host_.publishDeathLootDeferred(source, reason);
    }
    void spawnDrops(const EnemyDied &death, std::span<const LootDrop> drops) override {
        host_.spawnLoot(drops, death.region, death.position);
    }
    void awardExperience(const EnemyDied &death, EntityId beneficiary) override {
        host_.awardDeathExperience(death, beneficiary);
    }
};
void GameSessionImpl::settleDeaths() {
    // Appending item/quest events may reallocate GameEvent; own this input batch.
    std::vector<EnemyDied> deaths;
    for (const auto &event : events())
        if (const auto *death = std::get_if<EnemyDied>(&event)) deaths.push_back(*death);
    DeathSettlementAdapter adapter{*this};
    settleMonsterDeaths(deaths, loot_, adapter);
}
void GameSessionImpl::publishDeathLootDeferred(EntityId source, std::string_view reason) {
    simulation_->emit(LootDeferred{source, std::string(reason)});
}
LootPlan GameSessionImpl::planDeathLoot(const EnemyDied &death, const LootRequest &request,
        DeathLootContext context, const std::set<uint32_t> &usedUniques) {
    auto prepared = prepareMonsterDeathLoot(content_, monsterContent_, worldContent_, request,
        death, context, usedUniques, random_);
    const auto &entry = prepared.entry;
    const auto &plan = prepared.plan;
    std::cout << "Monster loot entry: id=" << death.victim.value << " monster=" << death.identity.monster
              << " rank=" << monsterRankName(death.identity.rank);
    if (entry.status == LootEntryStatus::Ready)
        std::cout << " TC=" << entry.treasureClass << " itemLevel=" << entry.itemLevel
                  << " upgradeLevel=" << entry.upgradeLevel << " drops=" << prepared.treasureDrops
                  << " NoDrop=" << plan.noDrops << " deferred=" << plan.deferred << '\n';
    else std::cout << (entry.status == LootEntryStatus::Empty ? " empty: " : " deferred: ")
                   << entry.reason << '\n';
    return std::move(prepared.plan);
}
} // namespace d2x
