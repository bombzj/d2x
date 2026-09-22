#include "room_graph.hpp"

namespace d2x::maze {
FamilyRules catacombsRules(int level) {
    FamilyRules rules{257, 12, {291}};
    if (level == 35)
        rules.specialRooms.push_back(295);
    return rules;
}
void RoomMaze::initializeCatacombs(int level) {
    int preset = 290;
    if (level == 34) {
        for (int direction : {1, 2, 3, 0})
            if (add(0, direction, true) < 0)
                throw std::runtime_error("Cannot place catacombs entrance arms");
    } else if (seed_.next() & 1) {
        add(0, 0, true);
        add(0, 2, true);
        preset = 288;
    } else {
        add(0, 1, true);
        add(0, 3, true);
        preset = 289;
    }
    rooms_.front().preset = preset;
    rooms_.front().fixed = true;
}
} // namespace d2x::maze