#include "rules.hpp"
#include "gameplay/quest/den_of_evil.hpp"
#include "gameplay/quest/burial_grounds.hpp"
#include "gameplay/quest/search_for_cain.hpp"
#include "gameplay/quest/forgotten_tower.hpp"
#include "gameplay/quest/tools_of_trade.hpp"
#include "gameplay/quest/sisters_to_slaughter.hpp"

namespace d2x {
void actOneEntry(DifficultyQuests &records, const QuestEntryFacts &facts, std::vector<QuestEntryStep> &result) {
    auto advance = [&](QuestId id, auto transition) {
        auto next = records[questIndex(id)];
        if (transition(next)) { records[questIndex(id)] = next; result.emplace_back(QuestTransition{id, next}); }
    };
    if (facts.den) advance(QuestId::DenOfEvil, denAdvanceOnEntry);
    if (facts.burial) advance(QuestId::SistersBurialGrounds, burialAdvanceOnEntry);
    if (facts.tristram) advance(QuestId::SearchForCain, [](QuestRecord &next) {
        return next.stage >= uint32_t(CainStage::PortalOpened) && cainAdvance(next, CainStage::TristramEntered);
    });
    if (facts.tower) advance(QuestId::ForgottenTower, [](QuestRecord &next) { return towerAdvance(next, TowerStage::TowerEntered); });
    if (facts.towerCellar) advance(QuestId::ForgottenTower, [](QuestRecord &next) { return towerAdvance(next, TowerStage::CellarEntered); });
    if (facts.barracks) advance(QuestId::ToolsOfTheTrade, [](QuestRecord &next) { return toolsAdvance(next, ToolsStage::BarracksEntered); });
    if (facts.catacombsFour) advance(QuestId::SistersToTheSlaughter, [](QuestRecord &next) { return slaughterAdvance(next, SlaughterStage::CatacombsEntered); });
}
void actOneDeath(const EnemyDied &death, const QuestDeathContext &context, QuestDeathPlan &plan) {
    auto records = context.records;
    auto &slaughter = records.at(questIndex(QuestId::SistersToTheSlaughter));
    plan.andarielFirstKill = death.identity.monster == "andariel" &&
        slaughter.stage < uint32_t(SlaughterStage::AndarielSlain) &&
        context.catacombsFour && death.region == *context.catacombsFour;
    plan.questFirstKill |= plan.andarielFirstKill;
    auto transition = [&](QuestId id, QuestDeathNoticeEffect effect = QuestDeathNoticeEffect::None) {
        plan.steps.emplace_back(QuestDeathTransition{id, records.at(questIndex(id)), effect});
    };
    if (context.burial && death.region == *context.burial && death.identity.monster == "bloodraven") {
        if (burialAdvanceOnBloodRaven(records.at(questIndex(QuestId::SistersBurialGrounds))))
            transition(QuestId::SistersBurialGrounds);
        plan.steps.emplace_back(QuestDeathWave{QuestDeathWaveKind::BloodRaven});
    }
    if (!context.countessSuperUnique.empty() && context.towerCellar && death.region == *context.towerCellar &&
        death.identity.superUnique == context.countessSuperUnique && death.identity.monster == context.countessMonster) {
        if (towerAdvance(records.at(questIndex(QuestId::ForgottenTower)), TowerStage::CountessSlain)) {
            transition(QuestId::ForgottenTower);
            plan.steps.emplace_back(QuestDeathWorldEffect::TowerChests);
        }
    }
    if (context.andarielAvailable && context.catacombsFour && death.region == *context.catacombsFour &&
        death.identity.monster == "andariel" && death.identity.rank == MonsterRank::Boss) {
        plan.steps.emplace_back(QuestDeathWave{QuestDeathWaveKind::Andariel});
        if (slaughterAdvance(slaughter, SlaughterStage::AndarielSlain)) {
            transition(QuestId::SistersToTheSlaughter, QuestDeathNoticeEffect::SlaughterReactions);
            auto &cain = records.at(questIndex(QuestId::SearchForCain));
            if (cain.stage < uint32_t(CainStage::Rescued)) {
                cain.stage = uint32_t(CainStage::Rewarded);
                cain.flags |= cainRescuedByRogues;
                transition(QuestId::SearchForCain, QuestDeathNoticeEffect::ReconcileCain);
            }
            plan.steps.emplace_back(QuestDeathWorldEffect::SlaughterPortal);
        }
    }
}
} // namespace d2x
