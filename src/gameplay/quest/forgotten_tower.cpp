#include "forgotten_tower.hpp"

namespace d2x {
bool towerAdvance(QuestRecord &record, TowerStage stage) {
    if (record.stage >= uint32_t(stage)) return false;
    record.stage = uint32_t(stage);
    return true;
}
} // namespace d2x
