#pragma once
#include "resources/archive.hpp"
#include "resources/formats.hpp"
#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace d2x {
// Automap.txt selects original DC6 cels by LvlTypes, DT1 tile type, style and sequence.
class AutomapCatalog {
    struct Rule {
        int style = -1, first = -1, last = -1;
        std::array<int, 4> cels{-1, -1, -1, -1};
    };
    std::map<int, int> objectCels_;
    std::map<std::pair<int, int>, std::vector<Rule>> rules_;

  public:
    explicit AutomapCatalog(Archives &archives);
    int tileCel(int levelType, const Tile &tile, int x, int y) const;
    int objectCel(int objectClass) const;
};
} // namespace d2x
