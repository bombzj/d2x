#include "world/maze/room_graph.hpp"

namespace d2x {
std::array<int, 2> actTwoTombs(uint32_t seed) {
    Seed random(seed);
    random.next();
    int staff = 0, boss = 0;
    do { staff = random.below(7); boss = random.below(7); } while (staff == boss);
    return {66 + staff, 66 + boss};
}
bool supportsMaze(int level) {
    return (level >= 8 && level <= 12) || level == 18 || level == 19 || (level >= 21 && level <= 24) ||
           (level >= 28 && level <= 31) || (level >= 34 && level <= 36) ||
           (level >= 47 && level <= 49) || (level >= 51 && level <= 72) || level == 74 ||
           (level >= 84 && level <= 89) || level == 92 || level == 100 || level == 101 || level == 107 ||
           (level >= 113 && level <= 116) || level == 118 || level == 119 ||
           level == 122 || level == 123 || (level >= 125 && level <= 130) || level == 133 || level == 135;
}
MapRecipe generateMaze(const WorldCatalog &catalog, int level, uint32_t seed, int difficulty,
                       int entranceDirection) {
    if (!supportsMaze(level))
        throw std::runtime_error("This original maze family is not implemented");
    if (level == 61)
        return catalog.preset(480, catalog.level(level).levelType);
    if (level == 114 || level == 116 || level == 119) {
        Seed world(seed);
        Seed random(world.next() + uint32_t(level));
        const int preset = level == 114 ? (random.below(2) ? 1038 : 1039)
                         : level == 116 ? 1040 : 1041;
        return catalog.preset(preset, catalog.level(level).levelType,
                              random.below(catalog.presets().at(preset).files));
    }
    return maze::RoomMaze(catalog, level, seed).build(level, seed, difficulty, entranceDirection);
}
} // namespace d2x
