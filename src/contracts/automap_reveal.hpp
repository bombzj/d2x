#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace d2x {
// Display-only work progress; never a world, navigation or interaction grant.
struct AutomapRevealProgress {
    std::string state{"disabled"}, reason;
    bool enabled{};
    int rootLevel{};
    size_t roomsCompleted{}, roomsTotal{}, borrowed{}, released{}, activeRooms{};
    struct Level {
        int id{}, layer{};
        size_t roomsCompleted{}, roomsTotal{}, cells{}, presetCells{}, exits{};
        bool complete{};
    };
    std::vector<Level> levels;
};
}
