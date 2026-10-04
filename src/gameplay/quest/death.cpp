#include "death.hpp"
#include "module.hpp"

namespace d2x {
QuestDeathPlan planQuestDeath(const EnemyDied &death, const QuestDeathContext &context) {
    QuestDeathPlan plan;
    const auto modules = questModules();
    // The original host processes Act I death effects before Act II effects.
    for (size_t i = modules.size(); i > 0; --i) modules[i - 1].death(death, context, plan);
    return plan;
}
} // namespace d2x
