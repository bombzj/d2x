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
// Current core 3899D0's deterministic ordinary-map preset replacement.
// Special scene controllers own their additional mutations separately.
int ordinaryMapPresetClass(int level,int nativeClass);
int mapPresetClass(int level,int nativeClass,int controller);
int fixedMapController(int level);
bool suppressMapRoomPopulation(int level,int controller,int x,int y,int width,int height);
}
