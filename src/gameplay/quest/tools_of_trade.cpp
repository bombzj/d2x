#include "tools_of_trade.hpp"

namespace d2x {
bool toolsAdvance(QuestRecord &record, ToolsStage stage) {
    if (record.stage >= uint32_t(stage)) return false;
    record.stage = uint32_t(stage);
    return true;
}
} // namespace d2x
