#include "client_preferences.hpp"
#include "resources/atomic_file.hpp"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
namespace d2x {
constexpr auto preferencesPath = "client-settings.json";
ClientPreferences loadClientPreferences() {
    std::ifstream file(preferencesPath);
    if (!file) return {};
    try {
        const auto settings = nlohmann::json::parse(file);
        if (!settings.is_object()) {
            std::cerr << "Invalid client settings: expected an object\n";
            return {};
        }
        auto setting = [&](const char *key, bool fallback = false) {
            const auto found = settings.find(key);
            if (found == settings.end()) return fallback;
            if (found->is_boolean()) return found->get<bool>();
            std::cerr << "Invalid client setting: " << key << " must be boolean\n";
            return fallback;
        };
        auto fade = AutomapFade::Auto;
        if (const auto found = settings.find("automapFade"); found != settings.end()) {
            if (found->is_string()) {
                const auto mode = found->get<std::string>();
                if (mode == "no") fade = AutomapFade::No;
                else if (mode == "everything") fade = AutomapFade::Everything;
                else if (mode == "center") fade = AutomapFade::Center;
                else if (mode != "auto") std::cerr << "Invalid client setting: automapFade mode\n";
            } else std::cerr << "Invalid client setting: automapFade must be a string\n";
        }
        const bool large = setting("automapLarge");
        if (!large && fade == AutomapFade::Center) fade = AutomapFade::No;
        auto integer = [&](const char *key, int fallback, int maximum, int step = 1) {
            const auto found = settings.find(key);
            if (found == settings.end()) return fallback;
            if (found->is_number_integer()) {
                const auto value = found->get<int64_t>();
                if (value >= 0 && value <= maximum && value % step == 0) return int(value);
            }
            std::cerr << "Invalid client setting: " << key << " range/step\n";
            return fallback;
        };
        const AudioSettings audio{integer("soundVolume", 100, 100, 5), integer("musicVolume", 50, 100, 5),
            NpcSpeechMode(integer("npcSpeech", 2, 2))};
        return {setting("running"), setting("miniPanelOpen"), large,
            setting("automapCenterWhenCleared", true),
            setting("automapParty", true), setting("automapNames", true), fade, audio, setting("lighting", true)};
    } catch (const nlohmann::json::exception &error) {
        std::cerr << "Invalid client settings: " << error.what() << '\n';
        return {};
    }
}
bool saveClientPreferences(const ClientPreferences &preferences) {
    try {
        const auto text = nlohmann::json{{"running", preferences.running},
            {"miniPanelOpen", preferences.miniPanelOpen}, {"automapLarge", preferences.automapLarge},
            {"automapCenterWhenCleared", preferences.automapCenterWhenCleared},
            {"automapParty", preferences.automapParty}, {"automapNames", preferences.automapNames},
            {"automapFade", preferences.automapFade == AutomapFade::No ? "no"
                : preferences.automapFade == AutomapFade::Everything ? "everything"
                : preferences.automapFade == AutomapFade::Center ? "center" : "auto"},
            {"soundVolume", preferences.audio.sound}, {"musicVolume", preferences.audio.music},
            {"npcSpeech", int(preferences.audio.speech)}, {"lighting", preferences.lighting}}.dump(2);
        writeFileAtomically(preferencesPath,
            std::span<const uint8_t>{reinterpret_cast<const uint8_t *>(text.data()), text.size()});
        return true;
    } catch (const std::exception &error) {
        std::cerr << "Cannot write client settings: " << error.what() << '\n';
        return false;
    }
}
}
