#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x {
namespace {
QuestRecord &denRecord(WorldState &world) {
    return world.player.actOneQuests.at(size_t(world.population.difficulty))
        .at(questIndex(ActOneQuest::DenOfEvil));
}
} // namespace

void GameSession::onQuestRegionEntered(RegionId id) {
    auto &book = simulation_->state_.player.actOneQuests
        .at(size_t(state().population.difficulty));
    auto advance = [&](ActOneQuest quest, auto transition) {
        auto &record = book.at(questIndex(quest));
        if (transition(record)) simulation_->emit(QuestAdvanced{quest, record.stage});
    };
    auto &radament = book.at(questIndex(QuestId::RadamentsLair));
    if (int(id) == 74 && book.at(questIndex(QuestId::ArcaneSanctuary)).stage < 3) {
        book.at(questIndex(QuestId::ArcaneSanctuary)).stage = 3;
        simulation_->emit(QuestAdvanced{QuestId::ArcaneSanctuary, 3});
    }
    if ((int(id) == 44 || int(id) == 45) && !book.at(questIndex(QuestId::TaintedSun)).stage && !sunDarkeningFrame_)
        sunDarkeningFrame_ = state().frame + 15 + limitedRandom(random_, 2);
    if (int(id) != 40 && quest(QuestId::RadamentsLair).stage == uint32_t(RadamentStage::Assigned))
        if (const auto level = worldContent_.levels().find(int(id));
            level != worldContent_.levels().end() && level->second.act == 1)
            if (radamentAdvance(radament, RadamentStage::LeftTown))
                simulation_->emit(QuestAdvanced{QuestId::RadamentsLair, radament.stage});
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
    auto &record = denRecord(simulation_->state_);
    if (denAdvanceOnClear(record, area.kills > 0,
                          alive || !area.pendingSpawns.empty()))
        simulation_->emit(QuestAdvanced{ActOneQuest::DenOfEvil, record.stage});
}
void GameSession::updateBurialQuest(const EnemyDied &death) {
    if (!burialRegion_ || death.region != *burialRegion_ ||
        death.identity.monster != "bloodraven") return;
    auto &record = simulation_->state_.player.actOneQuests
        .at(size_t(state().population.difficulty)).at(questIndex(ActOneQuest::SistersBurialGrounds));
    if (burialAdvanceOnBloodRaven(record))
        simulation_->emit(QuestAdvanced{ActOneQuest::SistersBurialGrounds, record.stage});
    auto *source = simulation_->findEnemy(death.victim);
    if (source)
        for (auto &enemy : simulation_->state_.area.enemies) {
            const auto *monster = monsterContent_.find(enemy.identity.monster);
            const int horizontal = int(enemy.pos.x) - int(death.position.x);
            const int vertical = int(enemy.pos.y) - int(death.position.y);
            if (enemy.hp > 0 && monster && monster->undead &&
                simulation_->relation(source->id, enemy.id) == Relation::Allied &&
                region().map.activation.nearby(source->pos, enemy.pos) &&
                horizontal * horizontal + vertical * vertical <= 35 * 35)
                enemy.questDeathFrame = state().frame + 25 + limitedRandom(source->combatRandom, 100);
        }
}
void GameSession::updateTowerQuest(const EnemyDied &death) {
    const auto *countess = monsterContent_.superUnique("The Countess");
    if (!countess || !towerCellarRegion_ || death.region != *towerCellarRegion_ ||
        death.identity.superUnique != countess->id ||
        death.identity.monster != countess->monster) return;
    auto &record = simulation_->state_.player.actOneQuests
        .at(size_t(state().population.difficulty)).at(questIndex(ActOneQuest::ForgottenTower));
    if (towerAdvance(record, TowerStage::CountessSlain)) {
        simulation_->emit(QuestAdvanced{ActOneQuest::ForgottenTower, record.stage});
        for (auto &object : regions_.at(current_).objects)
            if (object.objectClass == 371) object.towerRewardStart = state().frame;
    }
}

void GameSession::talkToNpc(EntityId npc) {
    const auto *target = object(npc);
    if (!target || engagedNpc_ != npc ||
        (!region().definition.safe && !(int(region().definition.id) == 73 && target->npcClass == "tyrael1")) || !canReach(*target))
        return;
    const auto dialogue = npcQuestDialogue(target->name);
    if (!dialogue.readKey.empty())
        pendingNpcQuestMessages_.erase(dialogue.readKey);
    if (!dialogue.advancesQuest) return;
    const auto id = *dialogue.advancesQuest;
    auto &record = simulation_->state_.player.actOneQuests
        .at(size_t(state().population.difficulty)).at(questIndex(id));
    auto changed = [&](bool advanced) {
        if (advanced) simulation_->emit(QuestAdvanced{id, record.stage});
    };
    switch (id) {
    case QuestId::SevenTombs:
        if (target->npcClass == "tyrael1" && record.stage == 2) {
            ensureRegion(RegionId(40));
            if (!portalResources_ || !townPortalArrivals_.contains(RegionId(40))) return;
            simulation_->state_.publicPortals.push_back({true, ++simulation_->state_.nextPortalRevision,
                RegionId(73), map().grid.nearest(state().player.pos), townPortalArrivals_.at(RegionId(40)), state().time});
            record.stage = 3;
        } else if (target->npcClass == "jerhyn" && record.stage == 3) record.stage = 4;
        else if (target->npcClass == "meshif1" && record.stage == 4) record.stage = 5;
        else if (target->npcClass == "jerhyn" && !record.stage) record.stage = 1;
        else return;
        simulation_->emit(QuestAdvanced{id, record.stage});
        break;
    case QuestId::Summoner:
        if (record.stage == 2) { record.stage = 3; simulation_->emit(QuestAdvanced{id, record.stage}); }
        break;
    case QuestId::TaintedSun:
        record.stage = record.stage == 3 ? 4 : 2;
        simulation_->emit(QuestAdvanced{id, record.stage});
        break;
    case QuestId::ArcaneSanctuary:
        record.stage = std::max(record.stage, target->npcClass == "drognan" ? 1u : 2u);
        simulation_->emit(QuestAdvanced{id, record.stage});
        break;
    case QuestId::HoradricStaff:
        record.flags |= 1;
        simulation_->emit(QuestAdvanced{id, record.stage});
        break;
    case QuestId::RadamentsLair:
        changed(radamentAdvance(record, record.stage == uint32_t(RadamentStage::Slain)
            ? RadamentStage::Rewarded : RadamentStage::Assigned));
        break;
    case ActOneQuest::DenOfEvil:
        if (denClaimReward(record)) {
            ++simulation_->state_.player.unspentSkills;
            changed(true);
        } else changed(denAdvanceOnTalk(record));
        break;
    case ActOneQuest::SistersBurialGrounds:
        if (record.stage == uint32_t(BurialStage::BloodRavenSlain)) {
            if (!assignKashyaHireling()) {
                simulation_->emit(InteractionFailed{npc, "Original Rogue hireling data is unavailable."});
                return;
            }
            changed(burialClaimReward(record));
        } else changed(burialAdvanceOnTalk(record,
            quest(ActOneQuest::DenOfEvil).stage >= uint32_t(DenStage::Rewarded)));
        break;
    case ActOneQuest::SearchForCain:
        if (record.stage == uint32_t(CainStage::BarkAcquired))
            translateCainScroll(npc);
        else if (record.stage == uint32_t(CainStage::Rescued)) {
            if (!claimCainReward())
                simulation_->emit(InteractionFailed{npc, "Make room for Akara's original ring reward."});
        } else if (record.stage == uint32_t(CainStage::Unstarted))
            changed(cainAdvance(record, CainStage::Assigned));
        break;
    case ActOneQuest::ToolsOfTheTrade:
        if (record.stage == uint32_t(ToolsStage::MalusAcquired)) {
            ItemHandle malus;
            for (auto container : {playerContainers_.backpack, playerContainers_.equipment})
                for (auto itemId : inventory_.contents(container)) {
                    const auto *item = inventory_.item(itemId);
                    if (item && item->definition == "hdm") malus = item->handle();
                }
            if (!malus.id) {
                simulation_->emit(InteractionFailed{npc, "Bring the Horadric Malus to Charsi."});
                return;
            }
            const auto *held = inventory_.item(malus.id);
            const auto *location = held ? std::get_if<ContainerLocation>(&held->location) : nullptr;
            auto removed = location && location->container == playerContainers_.equipment
                ? inventory_.consumeEquipped(malus.id, playerContainers_)
                : inventory_.consume(malus, 1, inventoryAccess());
            if (!removed) return;
            publishInventory(std::move(removed), malus.id);
            changed(toolsAdvance(record, ToolsStage::RewardReady));
        } else if (record.stage == uint32_t(ToolsStage::Unstarted))
            changed(toolsAdvance(record, ToolsStage::Assigned));
        break;
    case ActOneQuest::SistersToTheSlaughter:
        if (record.stage == uint32_t(SlaughterStage::AndarielSlain))
            changed(slaughterAdvance(record, SlaughterStage::PassageReady));
        else if (record.stage == uint32_t(SlaughterStage::Unstarted))
            changed(slaughterAdvance(record, SlaughterStage::Assigned));
        break;
    default: break;
    }
}

void GameSession::claimAkaraRespec(EntityId npc) {
    const auto *target = object(npc);
    if (!target || target->name != "Akara" || engagedNpc_ != npc ||
        region().definition.id != RegionId::Encampment || !canReach(*target)) {
        simulation_->emit(InteractionFailed{npc, "Akara is unavailable or too far away."});
        return;
    }
    auto &player = simulation_->state_.player;
    auto &record = denRecord(simulation_->state_);
    if (!denClaimRespec(record)) {
        simulation_->emit(InteractionFailed{npc, "No free respec remains for this difficulty."});
        return;
    }
    player.unspentAttributes += allocatedPoints(player.allocated);
    player.allocated = {};
    for (const auto &[skill, rank] : player.skillRanks)
        player.unspentSkills += rank;
    player.skillRanks.clear();
    player.skillHotkeys = {};
    refreshCharacter(true);
    simulation_->emit(QuestAdvanced{ActOneQuest::DenOfEvil, record.stage});
}
} // namespace d2x
