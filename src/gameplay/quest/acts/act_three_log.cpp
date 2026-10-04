#include "act_three_state.hpp"
#include "rules.hpp"

namespace d2x {
QuestLogSelection actThreeLog(QuestId id, const QuestRecord &record, const QuestLogFacts &facts) {
    QuestLogSelection result;
    if (id == QuestId::Guardian && record.stage)
        result.descriptionKey = record.stage == 3 ? "qstsa3q63" : record.stage == 2 ? "qstsa3q62" : "qstsa3q61";
    if (id == QuestId::BlackenedTemple && record.stage)
        result.descriptionKey = record.stage == 3 ? "qstsa3q53" : record.stage == 2 ? "qstsa3q52" : "qstsa3q51";
    if (id == QuestId::LamEsensTome && record.stage)
        result.descriptionKey = facts.items.lamTome ? "qstsa3q12" : "qstsa3q11";
    if (id == QuestId::GoldenBird && record.stage)
        result.descriptionKey = record.stage == 1 ? "qstsa3q41" : record.stage == 2 ? "qstsa3q42" :
            record.stage == 3 ? "qstsa3q43" : record.stage == 4 ? "qstsa3q44" : "qstsa3q45";
    if (id == QuestId::BladeOfTheOldReligion && record.stage)
        result.descriptionKey = record.stage == 3 ? "qstsa3q33" : record.stage == 4 ?
            (record.flags & gidbinnHirelingGranted ? "qstsa3q35" : "qstsa3q34") : facts.gidbinnOnGround ? "qstsa3q32" : "qstsa3q31";
    if (id == QuestId::KhalimsWill && record.stage) {
        const auto &held = facts.items.khalim;
        result.descriptionKey = held[4] || record.stage == 3 ? "qstsa3q26" :
            !(record.flags & khalimAssigned) ? "qstsa3q21a" : !held[0] ? "qstsa3q21" :
            !held[1] ? "qstsa3q22" : !held[2] ? "qstsa3q24" : !held[3] ? "qstsa3q23" : "qstsa3q25";
    }
    return result;
}
} // namespace d2x
