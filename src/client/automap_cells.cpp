#include "automap_cells.hpp"
#include "content/world/automap_data.hpp"

namespace d2x {
bool insertNativeCell(std::set<NativeAutomapCell> &cells, NativeAutomapCell cell) {
    const int group = nativeAutomapCellGroup(cell.cel);
    for (auto it = cells.lower_bound({0, cell.x, cell.y});
         it != cells.end() && it->x == cell.x && it->y == cell.y; ++it)
        if (group < 0 || nativeAutomapCellGroup(it->cel) == group) return false;
    return cells.insert(cell).second;
}
}
