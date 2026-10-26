#include "boss_program.hpp"
#include <array>
#include <algorithm>
namespace d2x::pd2 {
bool scenePauseClass(int definition) {
    // Current core 100F0D60 / 105DC028, room-release suppression set.
    constexpr std::array classes{789,887,890,888,889,891,886,790,791,792,793,794,
        933,934,935,936,938,966,939,937,968,969,1106,1107,1112,1113,1114,1115,1140,1141};
    return std::find(classes.begin(),classes.end(),definition)!=classes.end();
}
bool sceneBossAbsorption(int definition) {
    // Current 1AA870 / 105DEE74: only these six receive the extra channels.
    switch(definition) {case 789:case 933:case 934:case 935:case 936:case 1112:return true;default:return false;}
}
std::span<const int> uberMinionPool(int type) {
    // 41d49 constructors 167E80 / 167EF0 / 167F40 own these ordered pools.
    static constexpr int mephisto[]{725,726,727,728,729,730};
    static constexpr int diablo[]{712};
    static constexpr int baal[]{731,732};
    switch(type) {case 704:return mephisto;case 705:return diablo;case 709:return baal;default:return {};}
}
}
