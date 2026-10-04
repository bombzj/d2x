#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session_impl.hpp"
#include "identification.hpp"

namespace d2x {
void GameSessionImpl::identifyWithCain(EntityId npc) {
    const auto *target = object(npc);
    if (!target || !npcCanIdentify(target->npcClass) || !npcAccess(npc).townService()) {
        simulation_->emit(InteractionFailed{npc, "Cain is unavailable or too far away."});
        return;
    }
    auto plan = planCainIdentification(inventory_.state(), playerContainers_,
        quest(QuestId::SearchForCain).stage >= uint32_t(CainStage::Rescued) &&
        !(quest(QuestId::SearchForCain).flags & cainRescuedByRogues));
    if (plan.items.empty()) {
        simulation_->emit(ItemsIdentified{npc, 0, 0});
        return;
    }
    auto &player = simulation_->state_.player;
    if (uint64_t(player.character.gold) + player.character.bankGold < plan.cost) {
        simulation_->emit(InteractionFailed{npc, "Not enough gold to identify these items."});
        return;
    }
    const unsigned wallet = std::min(player.character.gold, plan.cost);
    player.character.gold -= wallet;
    player.character.bankGold -= plan.cost - wallet;
    applyCainIdentification(inventory_.state_, plan);
    simulation_->emit(ItemsIdentified{npc, unsigned(plan.items.size()), plan.cost});
}
} // namespace d2x
