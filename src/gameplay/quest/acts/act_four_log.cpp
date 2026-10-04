#include "act_four_state.hpp"
#include "rules.hpp"
namespace d2x {
QuestLogSelection actFourLog(QuestId id, const QuestRecord &record, const QuestLogFacts &facts) {
    QuestLogSelection result;
    if (id == QuestId::TerrorsEnd && record.stage) {
        result.descriptionKey = record.stage >= 3 ? "qstsa4q22" : "qstsa4q21";
        if (record.stage == 3 && facts.sealsRemaining && *facts.sealsRemaining) {
            result.descriptionKey = *facts.sealsRemaining == 1 ? "qstsa4q24" : "qstsa4q23";
            result.formatNumber = facts.sealsRemaining;
        }
    }
    if (id == QuestId::FallenAngel && record.stage) result.descriptionKey = record.stage == 3 ?
        record.flags & izualGhostSpoken ? "qstsa4q13" : "qstsa4q13a" : record.stage == 2 ? "qstsa4q12" : "qstsa4q11";
    if (id == QuestId::HellsForge && record.stage) result.descriptionKey = record.stage == 4 ? "qstsa4q34" :
        record.stage == 3 ? "qstsa4q33" : facts.items.forgeHammer ? "qstsa4q32" : "qstsa4q31";
    return result;
}
} // namespace d2x
