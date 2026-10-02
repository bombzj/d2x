#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x {
void GameSession::updateSlaughterQuest(const EnemyDied &death) {
    const auto *andariel = monsterContent_.find("andariel");
    if (!andariel || !catacombsFourRegion_ || death.region != *catacombsFourRegion_ ||
        death.identity.monster != andariel->id || death.identity.rank != MonsterRank::Boss)
        return;
    auto &record = simulation_->state_.player.actOneQuests
        .at(size_t(state().population.difficulty)).at(questIndex(ActOneQuest::SistersToTheSlaughter));
    if (auto *source = simulation_->findEnemy(death.victim))
        for (auto &enemy : simulation_->state_.area.enemies) {
            const int horizontal = int(enemy.pos.x) - int(death.position.x);
            const int vertical = int(enemy.pos.y) - int(death.position.y);
            if (enemy.hp > 0 && simulation_->relation(source->id, enemy.id) == Relation::Allied &&
                region().map.activation.nearby(source->pos, enemy.pos) &&
                horizontal * horizontal + vertical * vertical <= 35 * 35)
                enemy.questDeathFrame = state().frame + 1 + limitedRandom(source->combatRandom, 50);
        }
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
    if (record.stage < uint32_t(SlaughterStage::PassageReady) || state().player.dead) return;
    const auto destination = std::find_if(regions_.begin(), regions_.end(), [](const Region &region) {
        return int(region.definition.id) == 40 && region.definition.safe;
    });
    if (destination == regions_.end()) {
        simulation_->emit(InteractionFailed{npc, "Original Lut Gholein town is unavailable."});
        return;
    }
    if (slaughterAdvance(record, SlaughterStage::Completed))
        simulation_->emit(QuestAdvanced{ActOneQuest::SistersToTheSlaughter, record.stage});
    engagedNpc_ = {};
    enter(destination->definition.id);
    for (const auto &object : region().objects)
        if (object.isWaypoint()) {
            if (simulation_->state_.waypoints.emplace(region().definition.id, state().time).second)
                simulation_->emit(WaypointActivated{object.id});
            break;
        }
}
} // namespace d2x
