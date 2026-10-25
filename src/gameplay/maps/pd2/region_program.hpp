#pragma once
#include "property_program.hpp"
#include "gameplay/monsters/region_generation.hpp"

namespace d2x::pd2 {
struct MapRegionProperties {
    int level{},density{},uniqueMinimum{},uniqueMaximum{};
    std::vector<MonsterPopulationChoice> roster;
};
// Global MonReg programs are applied once, before any room is populated.
MapRegionProperties prepareMapRegionProperties(const MapProperties &,MapRegionProperties);
void appendMapGlobalUnitProperties(MapProperties &);
}
