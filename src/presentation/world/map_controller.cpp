#include "client/map_client.hpp"
#include "presentation/controller.hpp"
#include "presentation/scene_view.hpp"
#include <algorithm>

namespace d2x {
bool SceneController::handleMapClick(Vec mouse) {
    const auto &scene = view_.mapView();
    // Cain portal preceded regular portals and exits in the original input path.
    for (bool cain : {true, false}) for (const auto &portal : scene.portals) {
        if (portal.cain != cain ||
            (view_.screen(staticUnitPosition(portal.position)) - Vec{0, 40} - mouse).length() >= 45) continue;
        if (cain) mapClient_.submit(UseCainPortal{});
        else mapClient_.submit(UseTownPortal{portal.revision});
        pickupClick_ = true;
        return true;
    }
    if (const auto *exit = view_.exitAt(mouse)) {
        mapClient_.submit(UseExit{exit->slot});
        pickupClick_ = true;
        return true;
    }
    return false;
}
bool SceneController::handleTravel(const FrameInput &input) {
    auto &ui = view_.ui();
    if (ui.travelMenu && !ui.help) {
        if (ui.waypointSource) {
            if (input.insideViewport && input.leftPressed)
                if (auto destination = view_.clickWaypointMenu(input.mouse)) {
                    mapClient_.submit(WaypointTravel{ui.waypointSource, *destination});
                    ui.travelMenu = false;
                    ui.pause = false;
                }
            return true;
        }
        const auto entries = view_.travelEntries();
        int pages = (int(entries.size()) + worldPageSize - 1) / worldPageSize;
        int delta = input.pageDelta;
        if (input.insideViewport && input.leftPressed) {
            if (CheckCollisionPointRec(rv(input.mouse), travelPageButton(false)))
                --delta;
            if (CheckCollisionPointRec(rv(input.mouse), travelPageButton(true)))
                ++delta;
        }
        ui.travelPage = std::clamp(ui.travelPage + delta, 0, std::max(0, pages - 1));
        for (int i = 0; i < worldPageSize && ui.travelPage * worldPageSize + i < int(entries.size()); ++i) {
            if ((i < int(input.belt.size()) && input.belt[i]) ||
                (input.insideViewport && input.leftPressed &&
                 CheckCollisionPointRec(rv(input.mouse), travelSlot(i)))) {
                const auto &entry = entries[ui.travelPage * worldPageSize + i];
                if (!entry.destination) {
                    view_.notice(entry.status + (entry.missing.empty() ? "" : ": " + entry.missing.front()),
                                 true);
                    return true;
                }
                mapClient_.submit(Travel{*entry.destination});
                ui.travelMenu = false;
                // Opening a paused travel panel does not trap its queued transition.
                ui.pause = false;
                break;
            }
        }
        return true;
    }
    return false;
}
} // namespace d2x
