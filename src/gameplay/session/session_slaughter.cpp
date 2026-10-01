#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session.hpp"

namespace d2x {
void GameSession::updateSlaughterQuest(const EnemyDied &death) {
    const auto *andariel = monsterContent_.find("andariel");
    if (!andariel || !catacombsFourRegion_ || death.region != *catacombsFourRegion_ ||
        death.identity.monster != andariel->id || death.identity.rank != MonsterRank::Boss)
        return;
    auto &record = simulation_->state_.player.actOneQuests
        .at(size_t(state().population.difficulty)).at(questIndex(ActOneQuest::SistersToTheSlaughter));
    if (!slaughterAdvance(record, SlaughterStage::AndarielSlain)) return;
    for (const auto *npc : {"Deckard Cain", "Akara", "Kashya"})
        pendingNpcQuestMessages_.insert(std::string("A1Q6/Successful/") + npc);
    simulation_->emit(QuestAdvanced{ActOneQuest::SistersToTheSlaughter, record.stage});
    auto &cain = simulation_->state_.player.actOneQuests
        .at(size_t(state().population.difficulty)).at(questIndex(ActOneQuest::SearchForCain));
    if (cain.stage < uint32_t(CainStage::Rescued)) {
        cain.stage = uint32_t(CainStage::Rewarded);
        cain.flags |= cainRescuedByRogues;
        reconcileCainObjects();
        simulation_->emit(QuestAdvanced{ActOneQuest::SearchForCain, cain.stage});
    }
    if (portalResources_ && townPortalArrival_ && portalReach_ > 0 &&
        state().nextPortalRevision < UINT64_MAX) {
        auto position = map().grid.nearest(death.position);
        simulation_->state_.portal = {true, ++simulation_->state_.nextPortalRevision,
            death.region, position, *townPortalArrival_, state().time};
    }
}
void GameSession::completeActOne(EntityId npc) {
    const auto *warriv = object(npc);
    if (!warriv || warriv->name != "Warriv" || engagedNpc_ != npc ||
        !canReach(*warriv) || region().definition.id != RegionId::Encampment)
        return;
    auto &record = simulation_->state_.player.actOneQuests
        .at(size_t(state().population.difficulty)).at(questIndex(ActOneQuest::SistersToTheSlaughter));
    if (record.stage != uint32_t(SlaughterStage::PassageReady)) return;
    if (slaughterAdvance(record, SlaughterStage::Completed))
        simulation_->emit(QuestAdvanced{ActOneQuest::SistersToTheSlaughter, record.stage});
}
} // namespace d2x
