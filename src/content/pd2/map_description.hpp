#pragma once
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace d2x {struct ClassicData;struct StatValue;}
namespace d2x::pd2 {
// Original map-item stats, in report order; translated recipient stats are not descriptions.
std::vector<std::string> describeMap(const ClassicData &,std::span<const StatValue>,
    std::string_view areaName,unsigned quality);
}
