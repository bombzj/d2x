#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session_impl.hpp"
#include "content/items/item_magic_loot.hpp"

namespace d2x {
namespace {
QuestRecord &cain(WorldState &world) {
    return world.player.character.quests.at(size_t(world.population.difficulty))
        .at(questIndex(QuestId::SearchForCain));
}
} // namespace

bool GameSessionImpl::translateCainScroll(EntityId npc) {
    auto &record = cain(simulation_->state_);
    if (record.stage != uint32_t(CainStage::BarkAcquired)) return false;
    ItemHandle bark;
    for (auto id : inventory_.contents(playerContainers_.backpack)) {
        const auto *item = inventory_.item(id);
        if (item && item->definition == "bks") { bark = item->handle(); break; }
    }
    if (!bark.id) {
        simulation_->emit(InteractionFailed{npc, "Bring the Bark Scroll to Akara."});
        return false;
    }
    auto cell = inventory_.findSpace(playerContainers_.backpack, "bkd", bark.id);
    if (!cell) {
        simulation_->emit(InteractionFailed{npc, "Make room for the deciphered scroll."});
        return false;
    }
    auto replaced = inventory_.replaceItem(bark, "bkd",
        ContainerLocation{playerContainers_.backpack, *cell}, inventoryAccess());
    if (!replaced) {
        simulation_->emit(InteractionFailed{npc, "Original deciphered scroll could not be created."});
        return false;
    }
    publishInventory(std::move(replaced), bark.id);
    return true;
}

bool GameSessionImpl::claimCainReward() {
    auto &record = cain(simulation_->state_);
    if (record.stage != uint32_t(CainStage::Rescued)) return false;
    const int difficulty = state().population.difficulty;
    return claimQuestRing(difficulty == 0 ? 7 : difficulty == 1 ? 30 : 60,
        difficulty == 0 ? ItemQuality::Magic : ItemQuality::Rare);
}
bool GameSessionImpl::claimQuestRing(int level, ItemQuality quality) {
    const auto *ring = content_.items.find("rin");
    auto cell = ring ? inventory_.findSpace(playerContainers_.backpack, ring->code) : std::nullopt;
    if (!ring || !cell) return false;
    auto generated = rollAffixItem(content_, *ring, quality, level,
                                   inventory_.state_.creationRandom, characterDefinition_.code);
    if (!generated.deferred.empty()) return false;
    const auto previousRandom = inventory_.state_.creationRandom;
    inventory_.state_.creationRandom = generated.randomState;
    auto created = inventory_.createItem(ring->code, 1,
        ContainerLocation{playerContainers_.backpack, *cell}, unsigned(level), generated.generation);
    if (!created) {
        inventory_.state_.creationRandom = previousRandom;
        return false;
    }
    publishInventory(std::move(created), {});
    return true;
}
} // namespace d2x
