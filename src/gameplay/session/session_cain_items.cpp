#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session.hpp"
#include "content/items/item_magic_loot.hpp"

namespace d2x {
namespace {
QuestRecord &cain(WorldState &world) {
    return world.player.actOneQuests.at(size_t(world.population.difficulty))
        .at(questIndex(ActOneQuest::SearchForCain));
}
} // namespace

void GameSession::updateCainQuestItems() {
    auto &staff = simulation_->state_.player.actOneQuests.at(size_t(state().population.difficulty)).at(questIndex(QuestId::HoradricStaff));
    if (staff.stage < 6) {
        uint32_t next = staff.stage;
        for (const auto &[id, item] : inventory_.state().items) {
            const auto *location = std::get_if<ContainerLocation>(&item.location);
            if (!location || (location->container != playerContainers_.backpack && location->container != playerContainers_.cube &&
                location->container != playerContainers_.equipment)) continue;
            if (item.definition != content_.cubeCode && item.nativeQuestDifficulty < unsigned(state().population.difficulty)) continue;
            if (item.definition == "tr1" || item.definition == content_.cubeCode || item.definition == "msf" || item.definition == "vip") next = std::max(next, 1u);
            if (item.definition == "hst") next = 5;
        }
        if (staff.stage != next) { staff.stage = next; simulation_->emit(QuestAdvanced{QuestId::HoradricStaff, next}); }
    }
    bool acquiredBark = false;
    for (const auto &event : events())
        if (const auto *picked = std::get_if<ItemPickedUp>(&event);
            picked && picked->definition == "bks")
            acquiredBark = true;
    if (acquiredBark) {
        auto &record = cain(simulation_->state_);
        if (cainAdvance(record, CainStage::BarkAcquired))
            simulation_->emit(QuestAdvanced{ActOneQuest::SearchForCain, record.stage});
    }
}

void GameSession::translateCainScroll(EntityId npc) {
    auto &record = cain(simulation_->state_);
    if (record.stage != uint32_t(CainStage::BarkAcquired)) return;
    ItemHandle bark;
    for (auto id : inventory_.contents(playerContainers_.backpack)) {
        const auto *item = inventory_.item(id);
        if (item && item->definition == "bks") { bark = item->handle(); break; }
    }
    if (!bark.id) {
        simulation_->emit(InteractionFailed{npc, "Bring the Bark Scroll to Akara."});
        return;
    }
    auto cell = inventory_.findSpace(playerContainers_.backpack, "bkd", bark.id);
    if (!cell) {
        simulation_->emit(InteractionFailed{npc, "Make room for the deciphered scroll."});
        return;
    }
    auto removed = inventory_.consume(bark, 1, inventoryAccess());
    if (!removed) {
        simulation_->emit(InteractionFailed{npc, "The Bark Scroll is unavailable."});
        return;
    }
    publishInventory(std::move(removed), bark.id);
    auto created = inventory_.createItem("bkd", 1,
        ContainerLocation{playerContainers_.backpack, *cell});
    if (!created) {
        simulation_->emit(InteractionFailed{npc, "Original deciphered scroll could not be created."});
        return;
    }
    publishInventory(std::move(created), {});
    if (cainAdvance(record, CainStage::ScrollTranslated))
        simulation_->emit(QuestAdvanced{ActOneQuest::SearchForCain, record.stage});
}

bool GameSession::claimCainReward() {
    auto &record = cain(simulation_->state_);
    if (record.stage != uint32_t(CainStage::Rescued)) return false;
    const auto *ring = content_.items.find("rin");
    auto cell = ring ? inventory_.findSpace(playerContainers_.backpack, ring->code) : std::nullopt;
    if (!ring || !cell) return false;
    const int difficulty = state().population.difficulty;
    const int level = difficulty == 0 ? 7 : difficulty == 1 ? 30 : 60;
    const auto quality = difficulty == 0 ? ItemQuality::Magic : ItemQuality::Rare;
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
    if (cainAdvance(record, CainStage::Rewarded))
        simulation_->emit(QuestAdvanced{ActOneQuest::SearchForCain, record.stage});
    return true;
}
} // namespace d2x
