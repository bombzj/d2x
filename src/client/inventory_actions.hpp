#pragma once
#include "contracts/online.hpp"

namespace d2x {
std::optional<OnlinePoint> firstBackpackSpace(const OnlineView &, const OnlineInventoryView &, uint32_t movingItem);

inline OnlineItemCommand useItemCommand(uint32_t item, uint64_t revision, bool hireling = false) {
    OnlineItemCommand use;
    use.action = OnlineItemAction::Use;
    use.item = item;
    use.itemRevision = revision;
    use.mercenary = hireling;
    return use;
}

// A column action uses its bottom slot. An explicitly selected item elsewhere
// in the belt or backpack retains its own identity.
inline std::optional<OnlineItemCommand> useBeltColumnCommand(const OnlineView &view, int column, bool hireling = false) {
    if (column < 0 || column >= 4) return {};
    for (const auto &[id, item] : view.world.items)
        if (item.mode == 2 && item.positionKnown && item.x == column && onlineItemOwnedBy(item, view.load.playerUnitId))
            return useItemCommand(id, item.revision, hireling);
    return {};
}
}
