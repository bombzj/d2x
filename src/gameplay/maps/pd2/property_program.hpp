#pragma once
#include "gameplay/stats/stat_list.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace d2x::pd2 {
struct MapStatTransfer {uint32_t divide{};};
struct MapPropertyRules {
    std::map<uint16_t,MapStatTransfer> transfers;
    uint16_t forceEvent{};
    std::map<uint16_t,std::array<uint16_t,3>> physicalElements;
    std::array<uint16_t,6> sunderResistances{};
};
// Ordered native lists. A program identifier is not an ItemStatCost identity.
struct MapProperties {
    std::vector<StatValue> original,monsters,players,global;
    bool forceEvent{};
};
MapProperties prepareMapProperties(const MapPropertyRules &,std::span<const StatValue>);
int32_t mapGlobalValue(const MapProperties &,uint16_t program);
struct MapUnitProperties {
    std::vector<StatValue> values;
    std::optional<int32_t> maximumLife;
};
MapUnitProperties prepareMapUnitProperties(const MapPropertyRules &,const MapProperties &,
    bool player,int32_t physicalMinimum,int32_t physicalMaximum,int32_t maximumLife);
std::vector<StatValue> prepareMapSunderProperties(const MapPropertyRules &,const MapProperties &,int area,const StatSnapshot &base);
}
