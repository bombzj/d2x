#pragma once
#include "affix.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace d2x {
struct QualityGradeRecord {
    size_t row = 0;
    std::string name;
    bool weapon = false, armor = false;
    std::vector<std::string> specificTypes;
    std::vector<PropertyRange> properties;
};
} // namespace d2x
