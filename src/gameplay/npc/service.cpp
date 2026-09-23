#include "gameplay/session/session.hpp"
#include "identification.hpp"

namespace d2x {
void GameSession::identifyWithCain(EntityId npc) {
    const auto *target = object(npc);
    if (!target || target->name != "Deckard Cain" || engagedNpc_ != npc ||
        region().definition.id != RegionId::Encampment || !canReach(*target)) {
        simulation_.emit(InteractionFailed{npc, "Cain is unavailable or too far away."});
        return;
    }
    auto plan = planCainIdentification(inventory_.state(), playerContainers_);
    if (plan.items.empty()) {
        simulation_.emit(ItemsIdentified{npc, 0, 0});
        return;
    }
    auto &player = simulation_.state_.player;
    if (player.gold < plan.cost) {
        simulation_.emit(InteractionFailed{npc, "Not enough gold to identify these items."});
        return;
    }
    player.gold -= plan.cost;
    applyCainIdentification(inventory_.state_, plan);
    simulation_.emit(ItemsIdentified{npc, unsigned(plan.items.size()), plan.cost});
}
} // namespace d2x
