#pragma once
#include <algorithm>

namespace d2x {
enum class NpcSpeechMode { AudioOnly, TextOnly, AudioAndText };
struct AudioSettings {
    // D2Sound 1.13c defaults (RVA 9B30). Menus expose 21 positions, 5% apart.
    int sound = 100, music = 50;
    NpcSpeechMode speech = NpcSpeechMode::AudioAndText;
    bool operator==(const AudioSettings &) const = default;
};
// D2Client 5D24C/5D26A/5D45D: music attenuation, master attenuation,
// then Sounds.Volume, each with integer truncation before the next stage.
inline float originalSoundGain(int volume, int master, int music = 100, int envelope = 255) {
    envelope = std::clamp(envelope, 0, 255) * std::clamp(music, 0, 100) / 100;
    envelope = envelope * std::clamp(master, 0, 100) / 100;
    return float(envelope * std::clamp(volume, 0, 255) / 255) / 255.f;
}
} // namespace d2x
