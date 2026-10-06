#include "client/map_client.hpp"
#include "presentation/controller.hpp"
#include "presentation/scene_view.hpp"

namespace d2x {
bool SceneController::handleTravel(const FrameInput &input) {
    auto &ui = view_.ui();
    if (!ui.travelMenu || ui.help) return false;
    if (!ui.waypointSource) {
        ui.travelMenu = false;
        return true;
    }
    if (input.insideViewport && input.leftPressed) {
        if (auto destination = view_.clickWaypointMenu(input.mouse)) {
            mapClient_.submit(WaypointTravel{ui.waypointSource, *destination});
            ui.travelMenu = false;
        } else if (!ui.travelMenu) mapClient_.closeTravel();
    }
    return true;
}
} // namespace d2x
