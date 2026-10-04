#include "act_five_state.hpp"
#include "rules.hpp"
namespace d2x {
QuestLogSelection actFiveLog(QuestId id, const QuestRecord &record, const QuestLogFacts &facts) {
    QuestLogSelection result;
    if (id == QuestId::EveOfDestruction && record.stage) result.descriptionKey = record.stage == 5 ? record.flags & baalTyraelSpoken ? "qstsa5q64" : "qstsa5q63" :
        record.stage == 4 ? "qstsa5q62a" : record.stage == 3 ? "qstsa5q62b" : record.stage == 2 ? "qstsa5q61a" : "qstsa5q61";
    if (id == QuestId::RiteOfPassage && record.stage) result.descriptionKey = record.stage >= 3 ? facts.ancientsBattle ? "qstsa5q53" : "qstsa5q52" : "qstsa5q51";
    if (id == QuestId::BetrayalOfHarrogath && record.stage) result.descriptionKey = record.stage >= 4 ? "qstsa5q43a" : record.stage == 3 ? "qstsa5q43" : record.stage == 2 ? "qstsa5q42a" : "qstsa5q41";
    if (id == QuestId::PrisonOfIce && record.stage) result.descriptionKey = record.stage >= 5 ? record.flags & iceScrollGranted ? "qstsa5q35" : "qstsa5q34" : record.stage == 4 ? "qstsa5q33" : record.stage == 3 ? "qstsa5q32" : "qstsa5q31";
    if (id == QuestId::RescueOnMountArreat && record.stage) {
        result.descriptionKey = record.stage >= 3 ? "qstsa5q23" : record.stage == 2 ? "qstsa5q22" : "qstsa5q21";
        if (record.stage == 2) result.formatNumber = 15 - record.flags;
    }
    if (id == QuestId::SiegeOnHarrogath && record.stage) result.descriptionKey = record.stage >= 4 ? "qsta5q14" : record.stage == 3 ? "qsta5q13" : record.stage == 2 ? "qsta5q12" : "qsta5q11";
    return result;
}
} // namespace d2x
