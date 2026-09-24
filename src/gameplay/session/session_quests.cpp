#include "gameplay/session/session.hpp"
#include <algorithm>

namespace d2x {
namespace {
QuestRecord &denRecord(WorldState &world) {
    return world.player.actOneQuests.at(size_t(world.population.difficulty))
        .at(questIndex(ActOneQuest::DenOfEvil));
}
} // namespace

void GameSession::onQuestRegionEntered(RegionId id) {
    auto &book = simulation_.state_.player.actOneQuests
        .at(size_t(state().population.difficulty));
    auto advance = [&](ActOneQuest quest, auto transition) {
        auto &record = book.at(questIndex(quest));
        if (transition(record)) simulation_.emit(QuestAdvanced{quest, record.stage});
    };
    if (denRegion_ && id == *denRegion_)
        advance(ActOneQuest::DenOfEvil, denAdvanceOnEntry);
    if (burialRegion_ && id == *burialRegion_)
        advance(ActOneQuest::SistersBurialGrounds, burialAdvanceOnEntry);
    if (tristramRegion_ && id == *tristramRegion_)
        advance(ActOneQuest::SearchForCain, [](QuestRecord &record) {
            return record.stage >= uint32_t(CainStage::PortalOpened) &&
                   cainAdvance(record, CainStage::TristramEntered);
        });
    if (towerRegion_ && id == *towerRegion_)
        advance(ActOneQuest::ForgottenTower, [](QuestRecord &record) {
            return towerAdvance(record, TowerStage::TowerEntered);
        });
    if (towerCellarRegion_ && id == *towerCellarRegion_)
        advance(ActOneQuest::ForgottenTower, [](QuestRecord &record) {
            return towerAdvance(record, TowerStage::CellarEntered);
        });
    if (barracksRegion_ && id == *barracksRegion_)
        advance(ActOneQuest::ToolsOfTheTrade, [](QuestRecord &record) {
            return toolsAdvance(record, ToolsStage::BarracksEntered);
        });
    if (catacombsFourRegion_ && id == *catacombsFourRegion_)
        advance(ActOneQuest::SistersToTheSlaughter, [](QuestRecord &record) {
            return slaughterAdvance(record, SlaughterStage::CatacombsEntered);
        });
}

void GameSession::updateDenQuest() {
    if (!denRegion_ || state().area.region != *denRegion_) return;
    const auto &area = state().area;
    bool alive = std::any_of(area.enemies.begin(), area.enemies.end(),
                             [](const Enemy &enemy) { return enemy.hp > 0; });
    auto &record = denRecord(simulation_.state_);
    if (denAdvanceOnClear(record, area.kills > 0,
                          alive || !area.pendingSpawns.empty()))
        simulation_.emit(QuestAdvanced{ActOneQuest::DenOfEvil, record.stage});
}
void GameSession::updateBurialQuest(const EnemyDied &death) {
    if (!burialRegion_ || death.region != *burialRegion_ ||
        death.identity.monster != "bloodraven") return;
    auto &record = simulation_.state_.player.actOneQuests
        .at(size_t(state().population.difficulty)).at(questIndex(ActOneQuest::SistersBurialGrounds));
    if (burialAdvanceOnBloodRaven(record))
        simulation_.emit(QuestAdvanced{ActOneQuest::SistersBurialGrounds, record.stage});
}
void GameSession::updateTowerQuest(const EnemyDied &death) {
    const auto *countess = monsterContent_.superUnique("The Countess");
    if (!countess || !towerCellarRegion_ || death.region != *towerCellarRegion_ ||
        death.identity.superUnique != countess->id ||
        death.identity.monster != countess->monster) return;
    auto &record = simulation_.state_.player.actOneQuests
        .at(size_t(state().population.difficulty)).at(questIndex(ActOneQuest::ForgottenTower));
    if (towerAdvance(record, TowerStage::CountessSlain))
        simulation_.emit(QuestAdvanced{ActOneQuest::ForgottenTower, record.stage});
}

void GameSession::talkToNpc(EntityId npc) {
    const auto *target = object(npc);
    if (!target || engagedNpc_ != npc ||
        region().definition.id != RegionId::Encampment || !canReach(*target))
        return;
    if (target->name == "Warriv") {
        auto &record = simulation_.state_.player.actOneQuests
            .at(size_t(state().population.difficulty)).at(questIndex(ActOneQuest::SistersToTheSlaughter));
        if (record.stage == uint32_t(SlaughterStage::AndarielSlain) &&
            slaughterAdvance(record, SlaughterStage::PassageReady))
            simulation_.emit(QuestAdvanced{ActOneQuest::SistersToTheSlaughter, record.stage});
        return;
    }
    if (target->name == "Deckard Cain") {
        auto &record = simulation_.state_.player.actOneQuests
            .at(size_t(state().population.difficulty)).at(questIndex(ActOneQuest::SistersToTheSlaughter));
        if (record.stage == uint32_t(SlaughterStage::Unstarted) &&
            quest(ActOneQuest::SearchForCain).stage >= uint32_t(CainStage::Rescued) &&
            slaughterAdvance(record, SlaughterStage::Assigned))
            simulation_.emit(QuestAdvanced{ActOneQuest::SistersToTheSlaughter, record.stage});
        return;
    }
    if (target->name == "Charsi") {
        auto &record = simulation_.state_.player.actOneQuests
            .at(size_t(state().population.difficulty)).at(questIndex(ActOneQuest::ToolsOfTheTrade));
        if (record.stage == uint32_t(ToolsStage::MalusAcquired)) {
            ItemHandle malus;
            for (auto container : {playerContainers_.backpack, playerContainers_.equipment})
                for (auto id : inventory_.contents(container)) {
                    const auto *item = inventory_.item(id);
                    if (item && item->definition == "hdm") malus = item->handle();
                }
            if (!malus.id) return;
            const auto *held = inventory_.item(malus.id);
            const auto *location = held ? std::get_if<ContainerLocation>(&held->location) : nullptr;
            auto removed = location && location->container == playerContainers_.equipment
                ? inventory_.consumeEquipped(malus.id, playerContainers_)
                : inventory_.consume(malus, 1, inventoryAccess());
            if (!removed) return;
            publishInventory(std::move(removed), malus.id);
            if (toolsAdvance(record, ToolsStage::RewardReady))
                simulation_.emit(QuestAdvanced{ActOneQuest::ToolsOfTheTrade, record.stage});
        } else if (record.stage == uint32_t(ToolsStage::Unstarted) &&
                   state().player.level >= 8) {
            if (toolsAdvance(record, ToolsStage::Assigned))
                simulation_.emit(QuestAdvanced{ActOneQuest::ToolsOfTheTrade, record.stage});
        }
        return;
    }
    if (target->name == "Kashya") {
        auto &record = simulation_.state_.player.actOneQuests
            .at(size_t(state().population.difficulty)).at(questIndex(ActOneQuest::SistersBurialGrounds));
        if (record.stage == uint32_t(BurialStage::BloodRavenSlain)) {
            if (!assignKashyaHireling()) {
                simulation_.emit(InteractionFailed{npc, "Original Rogue hireling data is unavailable."});
                return;
            }
            if (burialClaimReward(record))
                simulation_.emit(QuestAdvanced{ActOneQuest::SistersBurialGrounds, record.stage});
        } else if (burialAdvanceOnTalk(record,
                    denRecord(simulation_.state_).stage >= uint32_t(DenStage::Rewarded)))
            simulation_.emit(QuestAdvanced{ActOneQuest::SistersBurialGrounds, record.stage});
        return;
    }
    if (target->name != "Akara") return;
    auto &cainQuest = simulation_.state_.player.actOneQuests
        .at(size_t(state().population.difficulty)).at(questIndex(ActOneQuest::SearchForCain));
    if (cainQuest.stage == uint32_t(CainStage::BarkAcquired)) {
        translateCainScroll(npc);
        return;
    }
    if (cainQuest.stage == uint32_t(CainStage::Rescued)) {
        if (!claimCainReward())
            simulation_.emit(InteractionFailed{npc, "Make room for Akara's original ring reward."});
        return;
    }
    if (cainQuest.stage == uint32_t(CainStage::Unstarted) &&
        quest(ActOneQuest::SistersBurialGrounds).stage >= uint32_t(BurialStage::BloodRavenSlain)) {
        if (cainAdvance(cainQuest, CainStage::Assigned))
            simulation_.emit(QuestAdvanced{ActOneQuest::SearchForCain, cainQuest.stage});
        return;
    }
    auto &record = denRecord(simulation_.state_);
    if (denClaimReward(record)) {
        ++simulation_.state_.player.unspentSkills;
        simulation_.emit(QuestAdvanced{ActOneQuest::DenOfEvil, record.stage});
    } else if (denAdvanceOnTalk(record))
        simulation_.emit(QuestAdvanced{ActOneQuest::DenOfEvil, record.stage});
}

void GameSession::claimAkaraRespec(EntityId npc) {
    const auto *target = object(npc);
    if (!target || target->name != "Akara" || engagedNpc_ != npc ||
        region().definition.id != RegionId::Encampment || !canReach(*target)) {
        simulation_.emit(InteractionFailed{npc, "Akara is unavailable or too far away."});
        return;
    }
    auto &player = simulation_.state_.player;
    auto &record = denRecord(simulation_.state_);
    if (!denClaimRespec(record)) {
        simulation_.emit(InteractionFailed{npc, "No free respec remains for this difficulty."});
        return;
    }
    player.unspentAttributes += allocatedPoints(player.allocated);
    player.allocated = {};
    for (const auto &[skill, rank] : player.skillRanks)
        player.unspentSkills += rank;
    player.skillRanks.clear();
    player.skillHotkeys = {};
    refreshCharacter(true);
    simulation_.emit(QuestAdvanced{ActOneQuest::DenOfEvil, record.stage});
}
} // namespace d2x
