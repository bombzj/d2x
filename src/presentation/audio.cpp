#include "audio.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x {
SoundBank::SoundBank(Archives &a) {
    groupRandom_ = initialRandom(0);
    const std::pair<const char *, const char *> files[] = {
        {"drink", "item/potiondrink.wav"},
        {"belt", "item/belt.wav"},
        {"quest_done", "cursor/questdone.wav"},
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
    emitters_.reset();
    for (auto [name, s] : sounds)
        UnloadSound(s);
    for (const auto &[name, variants] : originalGroups_)
        for (auto sound : variants) UnloadSound(sound);
}
void SoundBank::play(const std::string &name) {
    // Looping travel sounds are owned by live missile entities, not this event.
    if (hasEmitterSound(name)) return;
    if (const auto group = originalGroups_.find(name); group != originalGroups_.end()) {
        if (enabled && !muted && !group->second.empty())
            PlaySound(group->second[limitedRandom(groupRandom_, uint32_t(group->second.size()))]);
        return;
    }
    auto it = sounds.find(name);
    if (enabled && !muted && it != sounds.end())
        PlaySound(it->second);
}
void SoundBank::registerOriginal(Archives &archives, std::string key, std::string_view path, float volume) {
    auto found = sounds.find(key);
    if (found != sounds.end()) {
        UnloadSound(found->second);
        sounds.erase(found);
    }
    if (!enabled) return;
    auto bytes = archives.read(std::string(path));
    Wave wave = LoadWaveFromMemory(".wav", bytes.data(), int(bytes.size()));
    if (!wave.data) return;
    auto sound = LoadSoundFromWave(wave);
    UnloadWave(wave);
    SetSoundVolume(sound, volume);
    sounds.emplace(std::move(key), sound);
}
void SoundBank::registerOriginalGroup(Archives &archives, std::string key, const DataTable &table, size_t row) {
    const int count = std::max(1, table.number(row, "Group Size").value_or(0));
    if (row + size_t(count) > table.rows().size()) throw std::runtime_error("Invalid original sound group");
    for (size_t variant = row; variant < row + size_t(count); ++variant)
        for (const auto field : {"Loop", "Compound", "Duration", "Defer Inst", "Stop Inst", "Fade In", "Fade Out"})
            if (table.number(variant, field).value_or(0) != 0)
                throw std::runtime_error("Unsupported original one-shot sound group: " + std::string(field));
    if (!enabled || originalGroups_.contains(key)) return;
    std::vector<Sound> variants;
    variants.reserve(size_t(count));
    try {
        for (size_t variant = row; variant < row + size_t(count); ++variant) {
            const auto volume = table.number(variant, "Volume");
            if (!volume || *volume < 0 || *volume > 255)
                throw std::runtime_error("Invalid original sound group volume");
            const auto bytes = archives.read("data/global/sfx/" + std::string(table.value(variant, "FileName")));
            Wave wave = LoadWaveFromMemory(".wav", bytes.data(), int(bytes.size()));
            if (!wave.data) throw std::runtime_error("Original sound group WAV could not be decoded");
            const auto sound = LoadSoundFromWave(wave);
            UnloadWave(wave);
            if (!sound.stream.buffer) throw std::runtime_error("Original sound group could not be loaded");
            variants.push_back(sound);
            SetSoundVolume(sound, .45f * *volume / 255.f);
        }
        originalGroups_.emplace(std::move(key), std::move(variants));
    } catch (...) {
        for (auto sound : variants) UnloadSound(sound);
        throw;
    }
}
} // namespace d2x
