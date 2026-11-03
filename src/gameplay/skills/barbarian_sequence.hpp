#pragma once
#include <vector>
#include <string_view>
namespace d2x {
struct BarbarianSequenceFrame { std::string_view mode;int frame{};bool hit{},leftHand{}; };
std::vector<BarbarianSequenceFrame> barbarianSequence(int sequence,std::string_view weapon);
}
