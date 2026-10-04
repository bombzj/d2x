#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session_impl.hpp"
#include <algorithm>

namespace d2x {
void GameSessionImpl::completeActOne(EntityId npc) {
    const auto *warriv = object(npc);
    if (!warriv || warriv->name != "Warriv" || !npcAccess(npc).townService() || region().definition.id != RegionId::Encampment)
        return;
    auto &record = simulation_->state_.player.character.quests
        .at(size_t(state().population.difficulty)).at(questIndex(QuestId::SistersToTheSlaughter));
    if (record.stage < uint32_t(SlaughterStage::PassageReady) || state().player.actions.dead) return;
    const auto destination = std::find_if(world_.regions().begin(), world_.regions().end(), [](const Region &region) {
        return int(region.definition.id) == 40 && region.definition.safe;
    });
    if (destination == world_.regions().end()) {
        simulation_->emit(InteractionFailed{npc, "Original Lut Gholein town is unavailable."});
        return;
    }
    if (slaughterAdvance(record, SlaughterStage::Completed))
        simulation_->emit(QuestAdvanced{QuestId::SistersToTheSlaughter, record.stage});
    engagedNpc_ = {};
    enter(destination->definition.id);
    for (const auto &object : region().objects)
        if (object.isWaypoint()) {
            if (simulation_->state_.waypoints.emplace(region().definition.id, state().time).second)
                simulation_->emit(WaypointActivated{object.id});
            break;
        }
}
} // namespace d2x
