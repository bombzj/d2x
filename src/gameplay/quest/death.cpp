#include "death.hpp"
#include "burial_grounds.hpp"
#include "forgotten_tower.hpp"
#include "sisters_to_slaughter.hpp"
#include "search_for_cain.hpp"

namespace d2x {
QuestDeathPlan planQuestDeath(const EnemyDied &death, const QuestDeathContext &context) {
    QuestDeathPlan plan;
    auto records = context.records;
    auto &slaughter = records.at(questIndex(QuestId::SistersToTheSlaughter));
    auto &tombs = records.at(questIndex(QuestId::SevenTombs));
    plan.andarielFirstKill = death.identity.monster == "andariel" &&
        slaughter.stage < uint32_t(SlaughterStage::AndarielSlain) &&
        context.catacombsFour && death.region == *context.catacombsFour;
    plan.questFirstKill = plan.andarielFirstKill || (death.identity.monster == "duriel" &&
        int(death.region) == 73 && tombs.stage < 5);
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
    if (death.identity.origin != SpawnOrigin::Summoned) {
        if (death.identity.monster == "duriel" && int(death.region) == 73) {
            if (tombs.stage < 2) { tombs.stage = 2; transition(QuestId::SevenTombs); }
            plan.steps.emplace_back(QuestDeathWorldEffect::DurielDoor);
        }
        if (death.identity.monster == "summoner" && int(death.region) == 74) {
            auto &summoner = records.at(questIndex(QuestId::Summoner));
            if (summoner.stage < 2) { summoner.stage = 2; transition(QuestId::Summoner); }
            auto &arcane = records.at(questIndex(QuestId::ArcaneSanctuary));
            if (arcane.stage < 4) { arcane.stage = 4; transition(QuestId::ArcaneSanctuary); }
        }
        if (death.identity.monster == "radament" && int(death.region) == 49) {
            plan.steps.emplace_back(QuestDeathWave{QuestDeathWaveKind::Radament});
            if (radamentAdvance(records.at(questIndex(QuestId::RadamentsLair)), RadamentStage::Slain))
                transition(QuestId::RadamentsLair, QuestDeathNoticeEffect::DropRadamentBook);
        }
    }
    return plan;
}
} // namespace d2x
