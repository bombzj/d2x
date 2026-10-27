#pragma once
#include "presentation/scene_view.hpp"
#include "presentation/audio/audio_settings.hpp"
namespace d2x {
struct ClientPreferences {
    bool running = false;
    bool miniPanelOpen = false;
    bool automapLarge = false, automapCenterWhenCleared = true;
    bool automapParty = true, automapNames = true;
    AutomapFade automapFade = AutomapFade::Auto;
    AudioSettings audio;
    bool lighting = true;
    // BH/D2Hackmap display features.
    bool bhRevealMap = false;
    bool bhAlwaysShowLoot = false;
    bool bhShowAreaName = false;
    bool bhShowXpText = false;
    bool bhShowBeltStatus = false;
    bool bhForceLightRadius = false;
    bool bhShowPathLine = false;
    bool bhShowResistances = false;
    bool bhShowMonsters = false;
    bool bhShowEnchantments = false;
    bool bhShowChests = false;
    bool bhAutomapOnJoin = false;
    bool bhShowLkChestLines = false;
    ControlBindings controls = defaultControlBindings();
    bool operator==(const ClientPreferences &) const = default;
};
ClientPreferences loadClientPreferences();
bool saveClientPreferences(const ClientPreferences &);
}
