#include "automap.hpp"
#include "client/remote_town.hpp"

namespace d2x {
namespace {
int presetCellCn(int level, int type, int id, const AutomapCatalog &catalog) {
    // Historical BH-CN MapNotify::RevealRoom (68f7343). Keep reachable
    // branches, including the 1468 value filtered by its final 1258 bound.
    int cel = -1;
    if (type == 1) {
        if (id == 256) cel = 300;
        if (id == 745) cel = 745;
    } else if (type == 2) {
        if (id == 580 && level == 79) cel = 318;
        if (id == 371) cel = 301;
        else if (id == 152) cel = 300;
        else if (id == 460) cel = 1468;
        if (id == 402 && level == 46) cel = 0;
        if (id == 376) cel = 376;
        if (cel == -1 && id <= 572) cel = catalog.objectCel(id);
    }
    return cel > 0 && cel < 1258 ? cel : -1;
}
}
void updateAutomapRevealCn(RemoteTown &town, const OnlineView &view, bool enabled) {
    // User-selected scope: the current walking component, never an entire act.
    // Historical CN Act/Game ranges and old excluded IDs are not this policy.
    town.updateAutomapReveal(view, enabled, presetCellCn);
}
}
