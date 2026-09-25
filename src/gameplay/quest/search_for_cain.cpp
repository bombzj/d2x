#include "search_for_cain.hpp"

namespace d2x {
bool cainAdvance(QuestRecord &record, CainStage stage) {
    if (record.stage >= uint32_t(stage)) return false;
    record.stage = uint32_t(stage);
    return true;
}
bool cainStoneActivated(QuestRecord &record, bool correct) {
    // D2MOO A1Q4: a wrong (or already lit) stone leaves earlier activations intact.
    if (!correct || record.stage < uint32_t(CainStage::ScrollTranslated) ||
        record.stage >= uint32_t(CainStage::PortalOpened)) return false;
    unsigned count = record.flags & cainStoneCountMask;
    if (count >= 5) return false;
    record.flags = (record.flags & ~cainStoneCountMask) | (count + 1);
    if (count + 1 == 5)
        record.stage = uint32_t(CainStage::PortalOpened);
    return true;
}
} // namespace d2x
