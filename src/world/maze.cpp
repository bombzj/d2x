#include "world/maze/room_graph.hpp"

namespace d2x {
bool supportsMaze(int level) {
    return (level >= 8 && level <= 12) || level == 18 || level == 19 || (level >= 21 && level <= 24) ||
           (level >= 28 && level <= 31) || (level >= 34 && level <= 36) ||
           (level >= 47 && level <= 49) || (level >= 51 && level <= 54) || level == 65;
}
MapRecipe generateMaze(const WorldCatalog &catalog, int level, uint32_t seed, int difficulty,
                       int entranceDirection) {
    if (!supportsMaze(level))
        throw std::runtime_error("This original maze family is not implemented");
    return maze::RoomMaze(catalog, level, seed).build(level, seed, difficulty, entranceDirection);
}
} // namespace d2x
