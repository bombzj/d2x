#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session_impl.hpp"
#include "core/random.hpp"
#include "gameplay/quest/npc_conversation.hpp"
#include "gameplay/quest/region_entry.hpp"
#include <algorithm>

namespace d2x {
namespace {
QuestRecord &denRecord(WorldState &world) {
    return world.player.character.quests.at(size_t(world.population.difficulty))
        .at(questIndex(QuestId::DenOfEvil));
}
} // namespace

void GameSessionImpl::onQuestRegionEntered(RegionId id) {
    auto &book = simulation_->state_.player.character.quests
        .at(size_t(state().population.difficulty));
    const auto level = worldContent_.levels().find(int(id));
    const QuestEntryFacts facts{int(id), level == worldContent_.levels().end() ? -1 : level->second.act,
        sunDarkeningFrame_.has_value(), denRegion_ && id == *denRegion_,
        burialRegion_ && id == *burialRegion_, tristramRegion_ && id == *tristramRegion_,
        towerRegion_ && id == *towerRegion_, towerCellarRegion_ && id == *towerCellarRegion_,
        barracksRegion_ && id == *barracksRegion_, catacombsFourRegion_ && id == *catacombsFourRegion_};
    for (const auto &step : planQuestEntry(book, facts)) {
        if (const auto *transition = std::get_if<QuestTransition>(&step)) {
            book.at(questIndex(transition->quest)) = transition->next;
            simulation_->emit(QuestAdvanced{transition->quest, transition->next.stage});
        } else sunDarkeningFrame_ = state().frame + 15 + limitedRandom(random_, 2);
    }
}

void GameSessionImpl::updateDenQuest() {
    if (!denRegion_ || state().area.region != *denRegion_) return;
    const auto &area = state().area;
    bool alive = std::any_of(area.enemies.begin(), area.enemies.end(),
                             [](const Enemy &enemy) { return enemy.hp > 0; });
    auto &record = denRecord(simulation_->state_);
    if (denAdvanceOnClear(record, area.kills > 0,
                          alive || !area.pendingSpawns.empty()))
        simulation_->emit(QuestAdvanced{QuestId::DenOfEvil, record.stage});
}
void GameSessionImpl::talkToNpc(EntityId npc) {
    const auto *target = object(npc);
    const auto access = npcAccess(npc);
    if (!target || !access.contact() ||
        (!access.safe && !(int(access.region) == 73 && target->npcClass == "tyrael1"))) return;
    const auto dialogue = npcQuestDialogue(npc);
    if (!dialogue.readKey.empty())
        pendingNpcQuestMessages_.erase(dialogue.readKey);
    if (dialogue.prelude) {
        simulation_->state_.player.character.questPreludes
            .at(size_t(state().population.difficulty)).at(size_t(*dialogue.prelude)) = true;
    }
    if (!dialogue.advancesQuest) return;
    const auto id = *dialogue.advancesQuest;
    auto &record = simulation_->state_.player.character.quests
        .at(size_t(state().population.difficulty)).at(questIndex(id));
    const auto plan = planNpcQuest(id, record, {target->npcClass,
        quest(QuestId::DenOfEvil).stage >= uint32_t(DenStage::Rewarded), dialogue.staffExplanation});
    if (!plan || !deliverQuestReward(plan->reward, npc)) return;
    record = plan->next;
    simulation_->emit(QuestAdvanced{id, record.stage});
}

void GameSessionImpl::claimAkaraRespec(EntityId npc) {
    const auto *target = object(npc);
    if (!target || target->name != "Akara" || !npcAccess(npc).townService() ||
        region().definition.id != RegionId::Encampment) {
        simulation_->emit(InteractionFailed{npc, "Akara is unavailable or too far away."});
        return;
    }
    auto &record = denRecord(simulation_->state_);
    if (!denClaimRespec(record)) {
        simulation_->emit(InteractionFailed{npc, "No free respec remains for this difficulty."});
        return;
    }
    resetCharacterAttributes(characterProgressionContext());
    refundCharacterSkills(characterSkillContext());
    refreshCharacter(true);
    simulation_->emit(QuestAdvanced{QuestId::DenOfEvil, record.stage});
}
} // namespace d2x
