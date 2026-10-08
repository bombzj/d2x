#include "audio.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x {
SoundBank::SoundBank() : groupRandom_(initialRandom(0)), enabled(IsAudioDeviceReady()) {}
SoundBank::~SoundBank() {
    emitters_.reset();
    for (const auto &[name, group] : originalGroups_)
        for (const auto &variant : group.sounds) UnloadSound(variant.sound);
}
void SoundBank::play(const std::string &name, uint64_t frame, float volume) {
    // Looping travel sounds are owned by live missile entities, not this event.
    if (hasEmitterSound(name)) return;
    if (const auto group = originalGroups_.find(name); group != originalGroups_.end()) {
        auto &sound = group->second;
        if (!enabled || sound.sounds.empty()) return;
        if (sound.lastStart && frame >= *sound.lastStart && frame - *sound.lastStart < sound.compound) return;
        const auto &variant = sound.sounds[limitedRandom(groupRandom_, uint32_t(sound.sounds.size()))];
        if (variant.deferInstance && IsSoundPlaying(variant.sound)) return;
        sound.lastStart = frame;
        // Stop Inst replaces the previous instance of the selected sound.
        // A request rejected by Compound must not interrupt that instance.
        if (variant.stopInstance) StopSound(variant.sound);
        SetSoundVolume(variant.sound, volume >= 0 ? .45f * std::clamp(volume, 0.f, 1.f) : variant.volume);
        PlaySound(variant.sound);
        return;
    }
}
void SoundBank::registerOriginalGroup(Archives &archives, std::string key, const DataTable &table, size_t row) {
    const int count = std::max(1, table.number(row, "Group Size").value_or(0));
    const int compound = table.number(row, "Compound").value_or(0);
    if (compound < 0) throw std::runtime_error("Unsupported original one-shot Compound rule");
    if (row + size_t(count) > table.rows().size()) throw std::runtime_error("Invalid original sound group");
    for (size_t variant = row; variant < row + size_t(count); ++variant)
        for (const auto field : {"Loop", "Duration", "Fade In"})
            if (table.number(variant, field).value_or(0) != 0)
                throw std::runtime_error("Unsupported original one-shot sound group: " + std::string(field));
    for (size_t variant = row; variant < row + size_t(count); ++variant) {
        // This path plays a finite WAV to its natural end. Fade Out only
        // needs an interrupt envelope when Stop Inst can replace that voice;
        // reject that combination until an interrupt mixer is available.
        const int fadeOut=table.number(variant,"Fade Out").value_or(0);
        if(fadeOut<0 || (fadeOut && table.number(variant,"Stop Inst").value_or(0)))
            throw std::runtime_error("Unsupported original one-shot interrupt fade");
        for (const auto field : {"Stop Inst", "Defer Inst"}) {
            const int flag = table.number(variant, field).value_or(0);
            if (flag != 0 && flag != 1)
                throw std::runtime_error("Invalid original one-shot flag: " + std::string(field));
        }
    }
    for (size_t variant = row; variant < row + size_t(count); ++variant)
        if (table.number(variant, "Compound").value_or(0) != compound)
            throw std::runtime_error("Inconsistent original one-shot Compound group");
    if (!enabled || originalGroups_.contains(key)) return;
    std::vector<OriginalSoundVariant> variants;
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
            variants.push_back({sound, table.number(variant, "Stop Inst").value_or(0) != 0,
                                      table.number(variant, "Defer Inst").value_or(0) != 0, .45f * *volume / 255.f});
            SetSoundVolume(sound, .45f * *volume / 255.f);
        }
        originalGroups_.emplace(std::move(key), OriginalSoundGroup{std::move(variants), unsigned(compound), {}});
    } catch (...) {
        for (const auto &variant : variants) UnloadSound(variant.sound);
        throw;
    }
}
} // namespace d2x
