#include "session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/quest/acts/act_three_state.hpp"
#include "content/items/item_quality.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
ItemGeneration GameSessionImpl::questItemGeneration(std::string_view code, uint64_t &random) const {
    ItemGeneration generation;
    if (!content_.staffRecipe.isComponent(code) && !content_.khalimRecipe.isWeapon(code) && code != content_.hellforge.hammer) return generation;
    const auto record = std::find_if(content_.uniqueItems.begin(), content_.uniqueItems.end(),
        [&](const auto &value) { return value.code == code; });
    if (record == content_.uniqueItems.end() || !record->artAvailable)
        throw std::runtime_error("Original quest unique item is unavailable: " + std::string(code));
    auto properties = rollSpecialProperties(*record, random);
    random = properties.randomState;
    generation.quality = ItemQuality::Unique;
    generation.specialRow = int32_t(record->row);
    generation.requiredLevel = record->requiredLevel;
    generation.propertyRolls = std::move(properties.values);
    return generation;
}
bool GameSessionImpl::carriesQuestItem(std::string_view code) const {
    for (const auto &[id, item] : inventory_.state().items) {
        const auto *location = std::get_if<ContainerLocation>(&item.location);
        if (!location || (location->container != playerContainers_.backpack &&
            location->container != playerContainers_.cube && location->container != playerContainers_.equipment)) continue;
        if (item.definition == code && (code == content_.cubeCode ||
            item.nativeQuestDifficulty >= unsigned(state().population.difficulty))) return true;
    }
    return false;
}
bool GameSessionImpl::questItemOnGround(std::string_view code) const {
    for (const auto &[id, item] : inventory_.state().items)
        if (item.definition == code && item.nativeQuestDifficulty >= unsigned(state().population.difficulty) &&
            std::holds_alternative<GroundLocation>(item.location)) return true;
    return false;
}
void GameSessionImpl::updateQuestItems() {
    auto &bird = simulation_->state_.player.character.quests.at(size_t(state().population.difficulty)).at(questIndex(QuestId::GoldenBird));
    const auto previousBird = bird.stage;
    if (bird.stage < uint32_t(GoldenBirdStage::Brewing)) {
        if (carriesQuestItem(content_.goldenBird.bird)) bird.stage = std::max(bird.stage, 3u);
        else if (carriesQuestItem(content_.goldenBird.figurine)) bird.stage = std::max(bird.stage, 1u);
    }
    if (bird.stage != previousBird) simulation_->emit(QuestAdvanced{QuestId::GoldenBird, bird.stage});
    auto &blade = simulation_->state_.player.character.quests.at(size_t(state().population.difficulty)).at(questIndex(QuestId::BladeOfTheOldReligion));
    if (blade.stage < 3 && carriesQuestItem(content_.gidbinnCode)) {
        blade.stage = 3; simulation_->emit(QuestAdvanced{QuestId::BladeOfTheOldReligion, 3});
    }
    auto &khalim = simulation_->state_.player.character.quests.at(size_t(state().population.difficulty))[questIndex(QuestId::KhalimsWill)];
    const auto previousKhalim = khalim.stage;
    if (khalim.stage < 4) {
        if (carriesQuestItem(content_.khalimRecipe.output)) khalim.stage = 3;
        else for (const auto &input : content_.khalimRecipe.inputs)
            if (carriesQuestItem(input)) khalim.stage = std::max(khalim.stage, 2u);
    }
    if (khalim.stage != previousKhalim) simulation_->emit(QuestAdvanced{QuestId::KhalimsWill, khalim.stage});
    auto &tome = simulation_->state_.player.character.quests.at(size_t(state().population.difficulty))[questIndex(QuestId::LamEsensTome)];
    if (tome.stage < 3 && carriesQuestItem(content_.lamTomeCode)) {
        tome.stage = 3; simulation_->emit(QuestAdvanced{QuestId::LamEsensTome, 3});
    }
    auto &staff = simulation_->state_.player.character.quests.at(size_t(state().population.difficulty)).at(questIndex(QuestId::HoradricStaff));
    if (staff.stage < 6) {
        uint32_t next = staff.stage;
        for (const auto &[id, item] : inventory_.state().items) {
            const auto *location = std::get_if<ContainerLocation>(&item.location);
            if (!location || (location->container != playerContainers_.backpack && location->container != playerContainers_.cube &&
                location->container != playerContainers_.equipment)) continue;
            if (item.definition != content_.cubeCode && item.nativeQuestDifficulty < unsigned(state().population.difficulty)) continue;
            if (item.definition == content_.staffRecipe.scroll || item.definition == content_.cubeCode ||
                content_.staffRecipe.isComponent(item.definition)) next = std::max(next, 1u);
            if (item.definition == content_.staffRecipe.output) next = 5;
        }
        if (staff.stage != next) { staff.stage = next; simulation_->emit(QuestAdvanced{QuestId::HoradricStaff, next}); }
    }
    bool acquiredBark = false;
    for (const auto &event : events())
        if (const auto *picked = std::get_if<ItemPickedUp>(&event);
            picked && picked->definition == "bks")
            acquiredBark = true;
    if (acquiredBark) {
        auto &record = simulation_->state_.player.character.quests.at(size_t(state().population.difficulty)).at(questIndex(QuestId::SearchForCain));
        if (cainAdvance(record, CainStage::BarkAcquired))
            simulation_->emit(QuestAdvanced{QuestId::SearchForCain, record.stage});
    }
}

} // namespace d2x
