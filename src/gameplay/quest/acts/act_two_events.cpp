#include "gameplay/quest/acts/act_two_state.hpp"
#include "rules.hpp"

namespace d2x {
void actTwoEntry(DifficultyQuests &records, const QuestEntryFacts &facts, std::vector<QuestEntryStep> &result) {
    auto advance = [&](QuestId id, auto transition) {
        auto next = records[questIndex(id)];
        if (transition(next)) { records[questIndex(id)] = next; result.emplace_back(QuestTransition{id, next}); }
    };
    if (facts.level == 74) advance(QuestId::ArcaneSanctuary, [](QuestRecord &next) {
        if (next.stage >= 3) return false;
        next.stage = 3; return true;
    });
    if ((facts.level == 44 || facts.level == 45) && !records[questIndex(QuestId::TaintedSun)].stage && !facts.sunScheduled)
        result.emplace_back(ScheduleSunDarkening{});
    if (facts.level != 40 && facts.act == 1 && records[questIndex(QuestId::RadamentsLair)].stage == uint32_t(RadamentStage::Assigned))
        advance(QuestId::RadamentsLair, [](QuestRecord &next) { return radamentAdvance(next, RadamentStage::LeftTown); });
}
void actTwoDeath(const EnemyDied &death, const QuestDeathContext &context, QuestDeathPlan &plan) {
    auto records = context.records;
    auto &tombs = records.at(questIndex(QuestId::SevenTombs));
    plan.questFirstKill |= death.identity.monster == "duriel" && int(death.region) == 73 &&
        tombs.stage < uint32_t(TombsStage::PassageGranted);
    auto transition = [&](QuestId id, QuestDeathNoticeEffect effect = QuestDeathNoticeEffect::None) {
        plan.steps.emplace_back(QuestDeathTransition{id, records.at(questIndex(id)), effect});
    };
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
}
} // namespace d2x
