#pragma once
#include "world/navigation.hpp"
namespace d2x {
// D2Common PATH_AllocDynamicPath: Wraith overrides flying, small monsters
// use the small path pattern; flying/opendoors flags belong to MonStats.
inline MovementCollisionRule monsterMovementCollision(bool wraith, bool flying, bool openDoors, int size) {
    return {uint16_t(wraith ? 0x0804 : flying ? 0x1804 : openDoors ? 0x3401 : 0x3c01),
        wraith ? 2 : size == 1 || size == 2 ? 2 : size};
}
inline MovementCollisionRule monsterSpawnCollision(int column, int size) {
    return {uint16_t(column == 1 ? 0x01c0 : column == 2 ? 0x3f11 : column == 3 ? 0 : 0x3c01),size};
}
}
