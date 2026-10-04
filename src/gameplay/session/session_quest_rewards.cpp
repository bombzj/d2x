#include "session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/quest/npc_conversation.hpp"
#include "gameplay/quest/acts/act_three_state.hpp"
#include "gameplay/quest/acts/act_four_state.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include "content/items/item_magic_loot.hpp"
#include "core/random.hpp"

namespace d2x {
bool GameSessionImpl::deliverQuestReward(QuestReward reward, EntityId npc) {
    switch (reward) {
    case QuestReward::FinalPortal: return createQuestPortal(state().player.movement.pos + Vec{5, 0}, 565, RegionId(109));
    case QuestReward::AnyaTemplePortal: return openQuestPortal(npc, 60, RegionId(121));
    case QuestReward::DefrostPotion: return exchangeQuestItem(npc, {}, content_.prisonOfIce.potion);
    case QuestReward::ResistanceScroll: return exchangeQuestItem(npc, {}, content_.prisonOfIce.scroll);
    case QuestReward::AnyaRare: {
        const auto &character = state().player.character;
        const int tier = state().population.difficulty == 2 && character.level > 65 ? 2 : state().population.difficulty >= 1 && character.level > 45 ? 1 : 0;
        const auto &pool = content_.prisonOfIce.rewards.at(characterDefinition_.code).at(size_t(tier));
        auto seed = inventory_.state_.creationRandom;
        const auto &code = pool.at(limitedRandom(seed, unsigned(pool.size())));
        const auto cell = inventory_.findSpace(playerContainers_.backpack, code);
        if (!cell) { simulation_->emit(InteractionFailed{npc, "Make room for Anya's rare reward."}); return false; }
        const auto generated = rollAffixItem(content_, *content_.items.find(code), ItemQuality::Rare, character.level, seed, characterDefinition_.code);
        if (!generated.deferred.empty()) { simulation_->emit(InteractionFailed{npc, generated.deferred}); return false; }
        const auto previousRandom = inventory_.state_.creationRandom;
        inventory_.state_.creationRandom = generated.randomState;
        auto result = inventory_.createItem(code, 1, ContainerLocation{playerContainers_.backpack, *cell}, unsigned(character.level), generated.generation);
        if (!result) { inventory_.state_.creationRandom = previousRandom; return false; }
        publishInventory(std::move(result), {}); return true;
    }
    case QuestReward::RescueRunes: {
        const auto count = quest(QuestId::RescueOnMountArreat).flags;
        const size_t rewards = count == 15 ? 3 : count == 14 ? 2 : 1;
        auto backup = inventory_.state_;
        InventoryResult transaction;
        for (size_t i = 0; i < rewards; ++i) {
            const auto &code = content_.hellforge.runes[0][6 + i]; // Native A5Q2 r07/r08/r09, actual MPQ item definitions.
            const auto cell = inventory_.findSpace(playerContainers_.backpack, code);
            if (!cell) { inventory_.state_ = std::move(backup); simulation_->emit(InteractionFailed{npc, "Make room for Qual-Kehk's rune reward."}); return false; }
            auto created = inventory_.createItem(code, 1, ContainerLocation{playerContainers_.backpack, *cell}, 1, {});
            if (!created) { inventory_.state_ = std::move(backup); return false; }
            transaction.changes.insert(transaction.changes.end(), created.changes.begin(), created.changes.end());
        }
        publishInventory(std::move(transaction), {}); return true;
    }
    case QuestReward::ActFivePortal: return openQuestPortal(npc, 566, RegionId(109));
    case QuestReward::Soulstone: return exchangeQuestItem(npc, {}, content_.soulstoneCode);
    case QuestReward::IzualSkills:
        simulation_->state_.player.character.unspentSkills += izualSkillReward; return true;
    case QuestReward::LamTome:
        if (!exchangeQuestItem(npc, content_.lamTomeCode, {})) return false;
        simulation_->state_.player.character.unspentAttributes += lamTomeAttributeReward;
        return true;
    case QuestReward::ReturnGidbinn: return exchangeQuestItem(npc, content_.gidbinnCode, {});
    case QuestReward::GidbinnRing:
        if (claimQuestRing(std::array{21, 35, 75}.at(size_t(state().population.difficulty)), ItemQuality::Rare)) return true;
        simulation_->emit(InteractionFailed{npc, "Make room for Ormus's original ring reward."}); return false;
    case QuestReward::IronWolf: return assignQuestHireling("asheara");
    case QuestReward::ExchangeGoldenBird: return exchangeQuestItem(npc, content_.goldenBird.figurine, content_.goldenBird.bird);
    case QuestReward::DeliverGoldenBird: return exchangeQuestItem(npc, content_.goldenBird.bird, {});
    case QuestReward::LifePotion: return exchangeQuestItem(npc, {}, content_.goldenBird.potion);
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
bool GameSessionImpl::exchangeQuestItem(EntityId npc, std::string_view input, std::string_view output) {
    ItemHandle source;
    if (!input.empty())
        for (auto container : {playerContainers_.backpack, playerContainers_.cube, playerContainers_.equipment})
            for (auto id : inventory_.contents(container))
                if (const auto *item = inventory_.item(id); item && item->definition == input &&
                    item->nativeQuestDifficulty >= unsigned(state().population.difficulty)) source = item->handle();
    if (!input.empty() && !source.id) {
        simulation_->emit(InteractionFailed{npc, "Bring the original quest item in your inventory."}); return false;
    }
    if (output.empty()) {
        const auto &location = inventory_.item(source.id)->location;
        const auto *container = std::get_if<ContainerLocation>(&location);
        auto result = container && container->container == playerContainers_.equipment ?
            inventory_.consumeEquipped(source.id, playerContainers_) : inventory_.consume(source, 1, inventoryAccess());
        if (!result) return false;
        publishInventory(std::move(result), source.id); return true;
    }
    const auto cell = inventory_.findSpace(playerContainers_.backpack, output, source.id);
    if (!cell) { simulation_->emit(InteractionFailed{npc, "Make room for the quest reward."}); return false; }
    auto result = source.id ? inventory_.replaceItem(source, output,
        ContainerLocation{playerContainers_.backpack, *cell}, inventoryAccess()) :
        inventory_.createItem(output, 1, ContainerLocation{playerContainers_.backpack, *cell},
            unsigned(state().player.character.level), {});
    if (!result) return false;
    inventory_.state_.items.at(result.item).nativeQuestDifficulty = unsigned(state().population.difficulty);
    publishInventory(std::move(result), source.id); return true;
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
