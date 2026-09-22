#include "audio.hpp"
namespace d2x {
SoundBank::SoundBank(Archives &a) {
    const std::pair<const char *, const char *> files[] = {
        {"drink", "item/potiondrink.wav"},
        {"belt", "item/belt.wav"},
        {"0", "skill/sorceress/fireball1.wav"},
        {"1", "skill/sorceress/novaice.wav"},
        {"2", "skill/barbarian/circle1.wav"},
        {"3", "skill/sorceress/teleport.wav"},
        {"4", "skill/barbarian/grunt101.wav"},
        {"5", "skill/barbarian/howl1.wav"},
        {"step", "ambient/footstep/heavydirtrun1.wav"},
        {"swing", "combat/weapon/one hand swing small01.wav"},
        {"impact", "skill/sorceress/largefireimpact1.wav"},
        {"fire", "object/fire2.wav"}};
    enabled = IsAudioDeviceReady();
    for (auto [name, path] : files) {
        auto b = a.read(std::string("data/global/sfx/") + path, false);
        if (b.empty() || !enabled)
            continue;
        Wave wave = LoadWaveFromMemory(".wav", b.data(), int(b.size()));
        if (wave.data) {
            auto sound = LoadSoundFromWave(wave);
            UnloadWave(wave);
            SetSoundVolume(sound, std::string(name) == "step" ? .18f : .45f);
            sounds.emplace(name, sound);
        }
    }
}
SoundBank::~SoundBank() {
    for (auto [name, s] : sounds)
        UnloadSound(s);
}
void SoundBank::play(const std::string &name) {
    auto it = sounds.find(name);
    if (enabled && !muted && it != sounds.end())
        PlaySound(it->second);
}
} // namespace d2x
