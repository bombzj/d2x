#include "region_program.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <cmath>
#include <algorithm>

namespace d2x::pd2 {
std::span<const int> outerVoidBossPool(int marker) {
    // Ordered native sets, 41d49 constructors 1C3940 / 1C3A30 / 1C3B20.
    static constexpr int first[]{746,750,861,870,879,893,915,996,1242};
    static constexpr int second[]{800,882,884,894,963,997,1025,1026,1149,1240,1243};
    static constexpr int third[]{755,809,826,883,964,998,1121,1122};
    switch(marker) {case 738:return first;case 739:return second;case 740:return third;default:return {};}
}
int outerVoidLifePercent(int marker) {
    switch(marker) {case 738:return 200;case 739:return 175;case 740:return 150;default:return 0;}
}
int outerVoidDeathStage(int nativeClass) {
    // Independent death sets 199640 / 199730 / 199820 have the same members
    // as the three birth sets in 41d49; this is not inferred from boss flags.
    for(int stage=1;stage<=3;++stage) {
        const auto pool=outerVoidBossPool(737+stage);
        if(std::find(pool.begin(),pool.end(),nativeClass)!=pool.end()) return stage;
    }
    return 0;
}
int mapBossGroup(int nativeClass) {
    if(const int tier=outerVoidDeathStage(nativeClass)) return tier;
    // 41d49 1998E0 / 199940 initialize the independent fourth/fifth sets.
    switch(nativeClass) {
    case 747:case 1000:case 1103:return 4;
    case 743:case 1104:case 1105:return 5;
    default:return 0;
    }
}
bool suppressMapRoomPopulation(int level,int controller,int x,int y,int width,int height,int levelX,int levelY) {
    // 36E740 -> 37BAE0: the fixed rectangle's upper endpoint is included
    // when it meets the room's lower edge; the room's upper edge is not.
    const auto overlaps=[&](int left,int right,int top,int bottom) {
        return left<nativeAdd(x,width) && right>=x && top<nativeAdd(y,height) && bottom>=y;
    };
    if(controller==31 && level==182) return overlaps(0x5a10,0x5a88,0x25c2,0x263a);
    if(level==180) {
        // 41d49 370060 passes all eight coordinates to 37D900. The first
        // two are register arguments omitted by its inferred prototype.
        for(const auto rectangle:{std::array{65,155,290,380},std::array{440,530,885,975},
            std::array{275,365,620,710},std::array{800,890,545,635},std::array{785,875,165,255}})
            if(overlaps(nativeAdd(levelX,rectangle[0]),nativeAdd(levelX,rectangle[1]),
                nativeAdd(levelY,rectangle[2]),nativeAdd(levelY,rectangle[3]))) return true;
    }
    if(level==166) return overlaps(0x512d,0x5191,0x32e7,0x334b);
    return false;
}
int fixedMapController(int level) {
    switch(level) {
    case 152:case 153:case 164:case 165:case 171:case 172:case 186:case 187:return 9;
    case 183:case 184:return 32;
    case 176:case 177:case 178:case 179:return 30;
    case 181:case 182:return 31;
    case 195:case 196:return 35;
    case 192:case 194:return 34;
    case 180:return 33;
    case 197:case 198:case 199:return 36;
    case 185:return 41;
    case 137:return 42;
    case 161:case 162:case 163:return 43;
    case 168:return 44;
    case 188:case 189:return 45;
    default:return 0;
    }
}
int mapPresetClass(int level,int nativeClass,int controller) {
    if(controller==36 && nativeClass==738) {
        if(level==197 || level==199) return 1195;
        if(level==198) return 1196;
    }
    if(controller==34) {
        if(level==192 && nativeClass==738) return 1148;
        if(level==194) switch(nativeClass) {
        case 738:return 1168;
        case 739:return 1166;
        case 740:case 786:return 1164;
        }
    }
    if(controller==30 && nativeClass==738) switch(level) {
    case 176:return 1058;
    case 177:return 1062;
    case 178:return 1063;
    case 179:return 1061;
    }
    if(controller==31) {
        if(level==181 && nativeClass==738) return 1092;
        if(level==182) switch(nativeClass) {
        case 738:return 1060;
        case 739:return 1057;
        case 740:return 1093;
        }
    }
    if(controller==35 && nativeClass==738 && level==196) return 1174;
    if(controller==35 && level==195) switch(nativeClass) {
    case 738:return 1171;
    case 739:return 1172;
    case 740:return 1175;
    }
    if(controller==9 && nativeClass==738) switch(level) {
    case 164:return 743;
    case 171:return 1104;
    case 186:return 1105;
    }
    if(controller==32) {
        if(level==183 && nativeClass==891) return -1;
        if(nativeClass==738 && level==183) return 1096;
        if(nativeClass==738 && level==184) return 1094;
    }
    return ordinaryMapPresetClass(level,nativeClass);
}
int ordinaryMapPresetClass(int level,int nativeClass) {
    // These are code identities in the current 17d5 core, not TXT row numbers
    // or a name-based guess. Geometry, ranks and parameters remain MPQ-owned.
    if(nativeClass==739 && level==144) return 753;
    if(nativeClass!=738) return nativeClass;
    switch(level) {
    case 139:return 883;
    case 141:return 884;
    case 142:return 800;
    case 143:return 750;
    case 144:return 755;
    case 145:return 746;
    case 146:return 861;
    case 147:return 964;
    case 148:return 879;
    case 149:return 809;
    case 150:return 882;
    case 151:return 870;
    case 152:return 743;
    case 153:case 165:return 747;
    case 154:return 826;
    case 155:return 893;
    case 156:return 894;
    case 158:return 915;
    case 160:return 963;
    case 167:return 997;
    case 169:return 996;
    case 170:return 998;
    case 172:return 1000;
    case 174:return 1026;
    case 175:return 1025;
    case 187:return 1103;
    case 193:return 1149;
    case 200:return 1194;
    case 201:return 1192;
    case 207:return 1241;
    case 208:return 1242;
    case 209:return 1240;
    case 210:return 1243;
    default:return nativeClass;
    }
}
namespace {
int32_t scaled(int32_t value,float multiplier) {
    const float result=float(uint32_t(value))*multiplier;
    if(!std::isfinite(result) || result>=2147483648.f || result<-2147483648.f) return INT32_MIN;
    return int32_t(std::trunc(result));
}
}
MapRegionProperties prepareMapRegionProperties(const MapProperties &properties,MapRegionProperties region) {
    for(const auto &program:properties.global) {
        const float percent=float(uint32_t(program.value))/100.f;
        switch(program.key.id) {
        case 0:region.density=scaled(region.density,1.f+percent);break;
        case 1:region.level=nativeAdd(region.level,program.value);break;
        case 2:
            region.uniqueMinimum=uint8_t(scaled(region.uniqueMinimum,1.f+percent));
            region.uniqueMaximum=uint8_t(scaled(region.uniqueMaximum,1.f+percent));break;
        case 3:
            if(region.roster.size()==13) region.roster.back()={uint16_t(program.value),1};
            else region.roster.push_back({uint16_t(program.value),1});
            break;
        case 4:
            region.density=scaled(region.density,percent);
            region.uniqueMinimum=uint8_t(scaled(region.uniqueMinimum,percent));
            region.uniqueMaximum=uint8_t(scaled(region.uniqueMaximum,percent));break;
        }
    }
    return region;
}
void appendMapGlobalUnitProperties(MapProperties &properties) {
    for(const auto &program:properties.global) switch(program.key.id) {
    case 4:
        for(const auto stat:{76,25,329,331,330,332,357,493})
            properties.monsters.emplace_back(stat,stat==76?100:stat==357?20:stat==493?program.value:40);
        break;
    case 12:properties.monsters.emplace_back(273,program.value);break;
    case 14:
        properties.monsters.emplace_back(76,100);
        properties.monsters.emplace_back(276,program.value);break;
    }
}
}
