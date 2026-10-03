#pragma once
#include "gameplay/rewards/death.hpp"
#include "gameplay/quest/death.hpp"
#include "gameplay/loot/request.hpp"
#include "gameplay/loot/plan.hpp"
#include "gameplay/loot/death_context.hpp"
#include <set>
#include <span>
#include <string>
#include <string_view>

namespace d2x {
class LootSystem;
struct DeathSettlementContext {
    EntityId beneficiary;
    std::string characterClass;
    QuestDeathContext quests;
};
// Authority adapter binds the recipient and services; the coordinator never
// queries a current PlayerState, enemy record, MPQ, device or GPU.
class IDeathSettlementHost {
  public:
    virtual ~IDeathSettlementHost() = default;
    virtual DeathSettlementContext capture(const EnemyDied &death) const = 0;
    virtual void applyQuest(const EnemyDied &death, const QuestDeathPlan &plan) = 0;
    virtual LootPlan planLoot(const EnemyDied &death, const LootRequest &request,
        DeathLootContext context, const std::set<uint32_t> &usedUniques) = 0;
    virtual void publishDeferred(EntityId source, std::string_view reason) = 0;
    virtual void spawnDrops(const EnemyDied &death, std::span<const LootDrop> drops) = 0;
    virtual void awardExperience(const EnemyDied &death, EntityId beneficiary) = 0;
};
// The input is an independent death batch, safe when publishing grows GameEvent.
void settleMonsterDeaths(std::span<const EnemyDied> deaths, LootSystem &loot,
                         IDeathSettlementHost &host);
} // namespace d2x
