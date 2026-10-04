#include "session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include "core/random.hpp"
namespace d2x {
void GameSessionImpl::personalizeWithAnya(const PersonalizeQuestItem &command) {
    const auto *npc = object(command.npc);
    auto &record = simulation_->state_.player.character.quests.at(size_t(state().population.difficulty))[questIndex(QuestId::BetrayalOfHarrogath)];
    const auto *item = inventory_.item(command.item.id);
    const auto *where = item ? std::get_if<ContainerLocation>(&item->location) : nullptr;
    const auto *base = item ? content_.items.find(item->definition) : nullptr;
    if (!npc || npc->npcClass != "drehya" || !npcAccess(command.npc).townService() || record.stage != 4 ||
        !item || item->revision != command.item.revision || !where || where->container != playerContainers_.backpack ||
        !base || !base->personalizable || !item->personalizedName.empty() || (item->nativeFlags & 0x1100u) || (inventory_.maximumDurability(*item) && !item->durability)) {
        simulation_->emit(InteractionFailed{command.npc, "Select an intact, nameable item for Anya."}); return;
    }
    auto &updated = inventory_.state_.items.at(item->id);
    updated.personalizedName = state().player.character.name; updated.durability = inventory_.maximumDurability(updated); ++updated.revision;
    InventoryResult result; result.item = updated.id;
    result.changes.push_back({updated.id, updated.revision, ItemChangeKind::PropertiesChanged, updated.location, updated.location, updated.quantity});
    record.stage = 5; publishInventory(std::move(result), updated.id); simulation_->emit(QuestAdvanced{QuestId::BetrayalOfHarrogath, 5});
}
void GameSessionImpl::socketWithLarzuk(const SocketQuestItem &command) {
    const auto *npc = object(command.npc);
    auto &record = simulation_->state_.player.character.quests.at(size_t(state().population.difficulty))[questIndex(QuestId::SiegeOnHarrogath)];
    const auto *item = inventory_.item(command.item.id);
    const auto *where = item ? std::get_if<ContainerLocation>(&item->location) : nullptr;
    const auto *base = item ? content_.items.find(item->definition) : nullptr;
    if (!npc || npc->npcClass != "larzuk" || !npcAccess(command.npc).townService() || record.stage != 4 ||
        !item || item->revision != command.item.revision || !where || where->container != playerContainers_.backpack ||
        !base || item->sockets || (item->nativeFlags & 0x1900u) || item->quantity != 1 ||
        (inventory_.maximumDurability(*item) && !item->durability) ||
        (content_.tables.at(base->base.sourceTable).number(base->base.sourceRow, "quest").value_or(0) && item->definition != "leg")) {
        simulation_->emit(InteractionFailed{command.npc, "Select an unsocketed weapon or armor for Larzuk."}); return;
    }
    int sockets = base->base.socketsByLevel[item->level > 40 ? 2 : item->level > 25 ? 1 : 0];
    if (sockets <= 0) { simulation_->emit(InteractionFailed{command.npc, "This original item type cannot receive sockets."}); return; }
    auto seed = inventory_.state_.creationRandom;
    if (item->quality == ItemQuality::Magic) sockets = 1 + int(limitedRandom(seed, unsigned(std::min(2, sockets))));
    else if (item->quality == ItemQuality::Rare || item->quality == ItemQuality::Set || item->quality == ItemQuality::Unique) sockets = 1;
    auto &updated = inventory_.state_.items.at(item->id); updated.sockets = unsigned(sockets);
    updated.durability = inventory_.maximumDurability(updated); ++updated.revision;
    inventory_.state_.creationRandom = seed;
    InventoryResult result; result.item = updated.id;
    result.changes.push_back({updated.id, updated.revision, ItemChangeKind::PropertiesChanged, updated.location, updated.location, updated.quantity});
    record.stage = 5; publishInventory(std::move(result), updated.id);
    simulation_->emit(QuestAdvanced{QuestId::SiegeOnHarrogath, 5});
}
} // namespace d2x
