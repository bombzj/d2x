#pragma once
#include "wilderness.hpp"
namespace d2x {
// D2MOO DRLGOUTDESR_PlaceTombEntriesInCanyon, eight-tile outdoor cells.
struct RetailCanyonEntrance {int id,file,x,y;};
inline constexpr std::array<RetailCanyonEntrance,7> retailCanyonEntrances{{
    {383,2,6,0},{383,1,4,0},{383,0,2,0},{387,0,0,0},
    {385,0,0,2},{385,1,0,4},{385,2,0,6}}};
uint32_t initializeRetailDesert(const WorldCatalog &, const NativeActLayout &, int level,
                              RetailOutdoorGrid &, const RetailPatternReader &);
} // namespace d2x
