#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session.hpp"
#include "identification.hpp"

namespace d2x {
void GameSession::identifyWithCain(EntityId npc) {
    const auto *target = object(npc);
    if (!target || !npcCanIdentify(target->npcClass) || engagedNpc_ != npc ||
        !region().definition.safe || !canReach(*target)) {
        simulation_->emit(InteractionFailed{npc, "Cain is unavailable or too far away."});
        return;
    }
    auto plan = planCainIdentification(inventory_.state(), playerContainers_,
        quest(ActOneQuest::SearchForCain).stage >= uint32_t(CainStage::Rescued) &&
        !(quest(ActOneQuest::SearchForCain).flags & cainRescuedByRogues));
    if (plan.items.empty()) {
        simulation_->emit(ItemsIdentified{npc, 0, 0});
        return;
    }
    auto &player = simulation_->state_.player;
    if (uint64_t(player.gold) + player.bankGold < plan.cost) {
        simulation_->emit(InteractionFailed{npc, "Not enough gold to identify these items."});
        return;
    }
    const unsigned wallet = std::min(player.gold, plan.cost);
    player.gold -= wallet;
    player.bankGold -= plan.cost - wallet;
    applyCainIdentification(inventory_.state_, plan);
    simulation_->emit(ItemsIdentified{npc, unsigned(plan.items.size()), plan.cost});
}
} // namespace d2x
