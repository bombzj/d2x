#pragma once
#include <cstdint>

namespace d2x {
// D2Common ConvertShapeShiftedMode and 11043 / Items::GetWeaponAttackSpeed.
uint8_t shapeMonsterMode(uint8_t playerMode,uint16_t enabledModes);
int shapeAttackSpeed(int shapeFrames,int shapeSpeed,int humanFrames,int humanSpeed,
    int itemIAS,int weaponSpeed,int skillRate,int weaponItemSpeed,int weaponItemIAS,bool equipped);
}
