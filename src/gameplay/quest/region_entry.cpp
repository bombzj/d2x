#include "region_entry.hpp"
#include "den_of_evil.hpp"
#include "burial_grounds.hpp"
#include "search_for_cain.hpp"
#include "forgotten_tower.hpp"
#include "tools_of_trade.hpp"
#include "sisters_to_slaughter.hpp"
#include <stdexcept>
#include <array>
#include <algorithm>

namespace d2x {
std::vector<QuestEntryStep> planQuestEntry(std::span<const QuestRecord> book,
                                         const QuestEntryFacts &facts) {
    if (book.size() != size_t(QuestId::Count)) throw std::invalid_argument("Incomplete quest records");
    std::array<QuestRecord, size_t(QuestId::Count)> records;
    std::copy(book.begin(), book.end(), records.begin());
    std::vector<QuestEntryStep> result;
    auto advance = [&](QuestId id, auto transition) {
        auto next = records[questIndex(id)];
        if (transition(next)) { records[questIndex(id)] = next; result.emplace_back(QuestTransition{id, next}); }
    };
    if (facts.level == 74) advance(QuestId::ArcaneSanctuary, [](QuestRecord &next) {
        if (next.stage >= 3) return false;
        next.stage = 3; return true;
    });
    if ((facts.level == 44 || facts.level == 45) && !book[questIndex(QuestId::TaintedSun)].stage && !facts.sunScheduled)
        result.emplace_back(ScheduleSunDarkening{});
    if (facts.level != 40 && facts.act == 1 && book[questIndex(QuestId::RadamentsLair)].stage == uint32_t(RadamentStage::Assigned))
        advance(QuestId::RadamentsLair, [](QuestRecord &next) { return radamentAdvance(next, RadamentStage::LeftTown); });
    if (facts.den) advance(QuestId::DenOfEvil, denAdvanceOnEntry);
    if (facts.burial) advance(QuestId::SistersBurialGrounds, burialAdvanceOnEntry);
    if (facts.tristram) advance(QuestId::SearchForCain, [](QuestRecord &next) {
        return next.stage >= uint32_t(CainStage::PortalOpened) && cainAdvance(next, CainStage::TristramEntered);
    });
    if (facts.tower) advance(QuestId::ForgottenTower, [](QuestRecord &next) { return towerAdvance(next, TowerStage::TowerEntered); });
    if (facts.towerCellar) advance(QuestId::ForgottenTower, [](QuestRecord &next) { return towerAdvance(next, TowerStage::CellarEntered); });
    if (facts.barracks) advance(QuestId::ToolsOfTheTrade, [](QuestRecord &next) { return toolsAdvance(next, ToolsStage::BarracksEntered); });
    if (facts.catacombsFour) advance(QuestId::SistersToTheSlaughter, [](QuestRecord &next) { return slaughterAdvance(next, SlaughterStage::CatacombsEntered); });
    return result;
}
} // namespace d2x
