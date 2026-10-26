#pragma once
namespace d2x {
// Do-only projectile programs shared by players and monsters. Programs with
// a start callback, contact weapon or independent lifetime need their own path.
constexpr bool pd2MonsterProjectileProgram(int start,int action) {
    if(start) return false;
    switch(action) {
    case 0:case 8:case 10:case 17:case 22:case 24:case 26:case 28:
    case 43:case 68:case 73:case 80:case 118:case 143:case 151:case 159:case 166:case 169:
    case 175:case 182:case 183:case 187:case 188:return true;
    default:return false;
    }
}
}
