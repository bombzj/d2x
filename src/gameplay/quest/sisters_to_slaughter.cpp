#include "sisters_to_slaughter.hpp"

namespace d2x {
bool slaughterAdvance(QuestRecord &record, SlaughterStage stage) {
    if (record.stage >= uint32_t(stage)) return false;
    record.stage = uint32_t(stage);
    return true;
}
} // namespace d2x
