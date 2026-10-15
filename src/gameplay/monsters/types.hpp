#pragma once
#include <cstdint>
#include <map>
#include <vector>
#include <utility>
namespace d2x {
// Resolved MonStats.hcIdx -> MonType nesting. Layer zero is never a match.
struct MonsterTypeCatalog {
    std::map<int,std::vector<uint16_t>> classes;
    std::map<int,std::pair<bool,bool>> dimensions; // MonStats2 small/large flags.
};
}
