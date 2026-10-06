#pragma once
#include "presentation/scene_view.hpp"
namespace d2x {
struct ClientPreferences {
    bool running = false;
    bool miniPanelOpen = false;
    bool automapLarge = false, automapCenterWhenCleared = true;
    bool automapParty = true, automapNames = true;
    AutomapFade automapFade = AutomapFade::Auto;
    bool operator==(const ClientPreferences &) const = default;
};
ClientPreferences loadClientPreferences();
bool saveClientPreferences(const ClientPreferences &);
}
