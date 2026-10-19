#include "client/map_client.hpp"
#include "presentation/controller.hpp"
#include "presentation/scene_view.hpp"
#include "presentation/hud/classic_panel.hpp"
#include <algorithm>

namespace d2x {
void SceneController::closeTravel() {
    mapClient_.closeTravel();
    auto &ui = view_.ui();
    ui.travelMenu = false;
    ui.waypointSource = {};
}
void SceneController::prepareTravelInput(const FrameInput &input) {
    const auto &ui = view_.ui();
    if (!input.focused || input.escape || (!ui.travelMenu && !mapClient_.read().travelRequested)) return;
    // D2Client 1.13c AAD50 closes on character input, except Tab. Semantic
    // shortcuts also cover injected input without a physical character event.
    const bool keyboard = input.inventory || input.character || input.skillTree || input.quests ||
        input.hireling || input.party || input.togglePortraits || input.messageLog || input.chat || input.enter ||
        input.help || input.run || input.weaponSwap || input.expandBelt || input.backspace ||
        !input.entryText.empty() || !input.text.empty() || input.entryUnsupported ||
        std::ranges::any_of(input.skills, [](bool pressed) { return pressed; }) ||
        std::ranges::any_of(input.belt, [](bool pressed) { return pressed; });
    const bool outsidePress = input.insideViewport && (input.leftPressed || input.rightPressed) &&
        (!ui.travelMenu || !CheckCollisionPointRec(rv(input.mouse), classicSideBounds(false)));
    // Close before chat/panel dispatch and before capturing the pointer. A
    // press outside the list belongs to the requested HUD/world action.
    if (keyboard || outsidePress) closeTravel();
}
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
        } else if (!ui.travelMenu) closeTravel();
    }
    return true;
}
} // namespace d2x
