#include "death_settlement.hpp"
#include "gameplay/loot/loot.hpp"
#include <utility>

namespace d2x {
void settleMonsterDeaths(std::span<const EnemyDied> deaths, LootSystem &loot,
                         IDeathSettlementHost &host) {
    for (const auto &death : deaths) {
        if (loot.settled(death.victim)) continue;
        // Capture per death, not for the whole batch: preceding kills may advance
        // quests, level recipients or reserve a limited unique before the next kill.
        const auto context = host.capture(death);
        const auto quest = planQuestDeath(death, context.quests);
        host.applyQuest(death, quest);
        LootRequest request{death.victim, death.identity, death.region, death.difficulty,
            quest.questFirstKill, true, death.rewardModifiers};
        if (death.identity.origin == SpawnOrigin::Summoned) {
            LootPlan empty;
            empty.randomState = death.lootRandom;
            loot.settle(request, std::move(empty));
            continue;
        }
        const bool earnsExperience = context.beneficiary && death.killer == context.beneficiary;
        auto plan = host.planLoot(death, request,
            {context.characterClass, quest.andarielFirstKill && earnsExperience}, loot.usedUniques());
        if (!plan.deferred.empty()) host.publishDeferred(death.victim, plan.deferred);
        auto drops = loot.settle(request, std::move(plan));
        host.spawnDrops(death, drops);
        if (earnsExperience) host.awardExperience(death, context.beneficiary);
    }
}
} // namespace d2x
