#include "client/map_client.hpp"
#include "presentation/controller.hpp"
#include "presentation/scene_view.hpp"

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
    if (!ui.travelMenu || ui.help) return false;
    if (!ui.waypointSource) {
        ui.travelMenu = false;
        return true;
    }
    if (input.insideViewport && input.leftPressed)
        if (auto destination = view_.clickWaypointMenu(input.mouse)) {
            mapClient_.submit(WaypointTravel{ui.waypointSource, *destination});
            ui.travelMenu = false;
        }
    return true;
}
} // namespace d2x
