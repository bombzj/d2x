#include "region_program.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <cmath>

namespace d2x::pd2 {
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
