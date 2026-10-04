#include "gameplay/quest/acts/act_two_state.hpp"
#include "rules.hpp"

namespace d2x {
namespace {
std::string_view descriptionKey(QuestId quest, uint32_t stage) {
    switch (quest) {
    case QuestId::SevenTombs:
        return stage == 4 ? "qstsa2q65" : stage == 3 ? "qstsa2q64" : stage == 2 ? "qstsa2q63" : "qstsa2q61";
    case QuestId::Summoner:
        return stage == 2 ? "qstsa2q53" : "qstsa2q51";
    case QuestId::ArcaneSanctuary:
        return stage >= 3 ? "qstsa2q42" : "qstsa2q41";
    case QuestId::TaintedSun:
        return stage == 3 ? "qstsa2q33" : stage == 2 ? "qstsa2q32" : "qstsa2q31a";
    case QuestId::HoradricStaff:
        return stage >= 5 ? "qstsa2q24" : "qstsa2q21";
    case QuestId::RadamentsLair:
        if (stage == uint32_t(RadamentStage::Assigned)) return "qstsa2q11";
        if (stage == uint32_t(RadamentStage::LeftTown)) return "qstsa2q12";
        if (stage == uint32_t(RadamentStage::Slain)) return "qstsa2q13";
        break;
    default: break;
    }
    return {};
}
} // namespace
QuestLogSelection actTwoLog(QuestId id, const QuestRecord &record, const QuestLogFacts &facts) {
    QuestLogSelection result;
    if (record.stage) result.descriptionKey = descriptionKey(id, record.stage);
    if (id == QuestId::HoradricStaff && record.stage && record.stage < uint32_t(StaffStage::Submitted)) {
        const auto &items = facts.items;
        result.descriptionKey = items.staff ? "qstsa2q24" : items.cube && items.shaft && items.head ? "qstsa2q23" :
            record.flags & staffScrollExplained ? "qstsa2q22" : "qstsa2q25";
    }
    if (id == QuestId::SevenTombs && facts.journalRead && record.stage < uint32_t(TombsStage::DurielSlain)) {
        result.descriptionKey = "qstsa2q61a";
        result.tombSymbol = true;
    }
    return result;
}
} // namespace d2x
