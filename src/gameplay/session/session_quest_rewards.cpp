#include "session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/quest/npc_conversation.hpp"

namespace d2x {
bool GameSessionImpl::deliverQuestReward(QuestReward reward, EntityId npc) {
    switch (reward) {
    case QuestReward::None: return true;
    case QuestReward::SkillPoint:
        ++simulation_->state_.player.character.unspentSkills;
        return true;
    case QuestReward::Rogue:
        if (assignKashyaHireling()) return true;
        simulation_->emit(InteractionFailed{npc, "Original Rogue hireling data is unavailable."});
        return false;
    case QuestReward::TranslateScroll: return translateCainScroll(npc);
    case QuestReward::CainRing:
        if (claimCainReward()) return true;
        simulation_->emit(InteractionFailed{npc, "Make room for Akara's original ring reward."});
        return false;
    case QuestReward::ReturnMalus: return returnMalus(npc);
    case QuestReward::TyraelPortal:
        ensureRegion(RegionId(40));
        if (!portalResources_ || !townPortalArrivals_.contains(RegionId(40))) return false;
        simulation_->state_.publicPortals.push_back({true, ++simulation_->state_.nextPortalRevision,
            RegionId(73), map().grid.nearest(state().player.movement.pos), townPortalArrivals_.at(RegionId(40)), state().time});
        return true;
    }
    return false;
}
bool GameSessionImpl::returnMalus(EntityId npc) {
    ItemHandle malus;
    for (auto container : {playerContainers_.backpack, playerContainers_.equipment})
        for (auto itemId : inventory_.contents(container)) {
            const auto *item = inventory_.item(itemId);
            if (item && item->definition == "hdm") malus = item->handle();
        }
    if (!malus.id) {
        simulation_->emit(InteractionFailed{npc, "Bring the Horadric Malus to Charsi."});
        return false;
    }
    const auto *held = inventory_.item(malus.id);
    const auto *location = held ? std::get_if<ContainerLocation>(&held->location) : nullptr;
    auto removed = location && location->container == playerContainers_.equipment
        ? inventory_.consumeEquipped(malus.id, playerContainers_)
        : inventory_.consume(malus, 1, inventoryAccess());
    if (!removed) return false;
    publishInventory(std::move(removed), malus.id);
    return true;
}
} // namespace d2x
