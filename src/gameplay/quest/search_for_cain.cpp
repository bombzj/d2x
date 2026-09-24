#include "search_for_cain.hpp"

namespace d2x {
bool cainAdvance(QuestRecord &record, CainStage stage) {
    if (record.stage >= uint32_t(stage)) return false;
    record.stage = uint32_t(stage);
    return true;
}
bool cainStoneActivated(QuestRecord &record, bool correct) {
    if (record.stage < uint32_t(CainStage::ScrollTranslated) ||
        record.stage >= uint32_t(CainStage::PortalOpened)) return false;
    unsigned count = record.flags & cainStoneCountMask;
    record.flags = (record.flags & ~cainStoneCountMask) |
                   (correct ? count + 1 : 0);
    if (correct && count + 1 == 5)
        record.stage = uint32_t(CainStage::PortalOpened);
    return true;
}
} // namespace d2x
