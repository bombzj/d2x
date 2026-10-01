#include "audio.hpp"
#include "core/random.hpp"
#include "resources/formats.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace d2x {
struct SoundBank::EmitterAudio {
    struct Sample {
        std::vector<float> pcm;
        unsigned channels = 0, frames = 0, loopBegin = 0, loopEnd = 0;
        unsigned fadeIn = 0, fadeOut = 0;
        float volume = 1;
    };
    struct Group {
        std::vector<Sample> samples;
        unsigned compound = 0;
        std::optional<uint64_t> lastStart;
    };
    struct Voice {
        const Sample *sample = nullptr;
        unsigned cursor = 0, played = 0, released = 0;
        bool releasing = false, finished = false;
    };
    std::map<std::string, Group> groups;
    std::map<EntityId, Voice> voices;
    std::set<EntityId> tracked;
    std::mutex mutex;
    AudioStream stream{};
    unsigned sampleRate = 0;
    bool paused = false;
    uint64_t random = 0x6963656f7262;
    size_t slot = 0;
    // raylib's callback has no user-data argument. Slots identify SoundBank
    // instances only; all missile voices share their bank's PCM mixer.
    static inline std::mutex callbacksMutex;
    static inline std::array<EmitterAudio *, 8> callbacks{};
    template <size_t Slot> static void callback(void *buffer, unsigned frames) {
        std::lock_guard lock(callbacksMutex);
        auto *output = static_cast<float *>(buffer);
        std::fill(output, output + size_t(frames) * 2, 0.f);
        if (auto *mixer = callbacks[Slot]) mixer->render(output, frames);
    }
    template <size_t... Slots> static auto makeCallbacks(std::index_sequence<Slots...>) {
        return std::array<AudioCallback, sizeof...(Slots)>{callback<Slots>...};
    }
    void open(unsigned rate) {
        sampleRate = rate;
        stream = LoadAudioStream(rate, 32, 2);
        if (!stream.buffer) throw std::runtime_error("Original travel audio stream could not be opened");
        {
            std::lock_guard lock(callbacksMutex);
            slot = 0;
            while (slot < callbacks.size() && callbacks[slot]) ++slot;
            if (slot < callbacks.size()) callbacks[slot] = this;
        }
        if (slot == callbacks.size()) {
            UnloadAudioStream(stream);
            stream = {};
            throw std::runtime_error("Travel audio callback slots are exhausted");
        }
        const auto functions = makeCallbacks(std::make_index_sequence<8>{});
        SetAudioStreamCallback(stream, functions[slot]);
        SetAudioStreamVolume(stream, 1.f);
        PlayAudioStream(stream);
    }
    ~EmitterAudio() {
        if (!stream.buffer) return;
        // Clear the pointer while excluding every in-flight callback before
        // destroying the stream or any PCM/voice storage.
        { std::lock_guard lock(callbacksMutex); callbacks[slot] = nullptr; }
        StopAudioStream(stream);
        UnloadAudioStream(stream);
    }
    void render(float *output, unsigned count) {
        std::lock_guard lock(mutex);
        for (auto &[id, voice] : voices) {
            const auto &sample = *voice.sample;
            for (unsigned i = 0; i < count && !voice.finished; ++i) {
                float gain = sample.volume * .45f; // Existing scene SFX level, before mixing/clamping.
                if (sample.fadeIn && voice.played < sample.fadeIn)
                    gain *= float(voice.played) / sample.fadeIn;
                if (voice.releasing) {
                    if (voice.released >= sample.fadeOut) { voice.finished = true; break; }
                    gain *= 1.f - float(voice.released++) / sample.fadeOut;
                }
                const size_t at = size_t(voice.cursor) * sample.channels;
                output[size_t(i) * 2] += sample.pcm[at] * gain;
                output[size_t(i) * 2 + 1] += sample.pcm[at + (sample.channels == 2 ? 1 : 0)] * gain;
                ++voice.played;
                if (++voice.cursor == sample.loopEnd) voice.cursor = sample.loopBegin;
            }
        }
        std::erase_if(voices, [](const auto &entry) { return entry.second.finished; });
        // Mix before clamping so overlapping voices do not restart one another.
        for (size_t i = 0; i < size_t(count) * 2; ++i)
            output[i] = std::clamp(output[i], -1.f, 1.f);
    }
};

void SoundBank::registerTravelGroup(Archives &archives, std::string key, const DataTable &table, size_t row) {
    if (!enabled) return;
    if (table.number(row, "Loop").value_or(0) != 1)
        throw std::runtime_error("Travel emitter requires an original looping sound");
    EmitterAudio::Group group;
    const int count = std::max(1, table.number(row, "Group Size").value_or(0));
    if (row + size_t(count) > table.rows().size())
        throw std::runtime_error("Original travel sound group is truncated");
    group.compound = unsigned(std::max(0, table.number(row, "Compound").value_or(0)));
    unsigned rate = 0;
    for (int i = 0; i < count; ++i) {
        const auto variant = row + size_t(i);
        if (table.number(variant, "Loop").value_or(0) != 1 ||
            table.number(variant, "Defer Inst").value_or(0) ||
            table.number(variant, "Stop Inst").value_or(0) ||
            table.number(variant, "Duration").value_or(0))
            throw std::runtime_error("Unsupported original travel sound instance rule");
        const auto bytes = archives.read("data/global/sfx/" + std::string(table.value(variant, "FileName")));
        Reader reader(bytes);
        if (reader.u32() != 0x46464952 || reader.u32() + uint64_t(8) != bytes.size() || reader.u32() != 0x45564157)
            throw std::runtime_error("Original travel WAV header is invalid");
        std::optional<std::pair<unsigned, unsigned>> loop;
        while (reader.pos + 8 <= bytes.size()) {
            const auto tag = reader.u32(), length = reader.u32();
            const auto begin = reader.pos;
            reader.need(length);
            if (tag == 0x6c706d73) { // RIFF smpl, offsets are PCM sample frames.
                if (length < 60) throw std::runtime_error("Original WAV loop is truncated");
                reader.skip(28);
                if (reader.u32() != 1) throw std::runtime_error("Unsupported original WAV loop count");
                reader.skip(8);
                if (reader.u32() != 0) throw std::runtime_error("Unsupported original WAV loop type");
                const auto first = reader.u32(), last = reader.u32();
                if (last == UINT32_MAX) throw std::runtime_error("Original WAV loop end overflows");
                if (reader.u32() != 0 || reader.u32() != 0)
                    throw std::runtime_error("Unsupported original WAV fractional or finite loop");
                loop = std::pair{first, last + 1}; // smpl's end is inclusive.
            }
            reader.seek(begin + length + (length & 1));
        }
        const int block = table.number(variant, "Block 1").value_or(-1);
        if ((loop ? block < 0 || unsigned(block) != loop->first : block != -1) ||
            table.number(variant, "Block 2").value_or(-1) != -1 ||
            table.number(variant, "Block 3").value_or(-1) != -1)
            throw std::runtime_error("Original travel WAV loop and Sounds.Block 1 disagree");
        Wave wave = LoadWaveFromMemory(".wav", bytes.data(), int(bytes.size()));
        if (!wave.data) throw std::runtime_error("Original travel WAV could not be decoded");
        // Sounds.Loop without a smpl loop or Block markers loops the complete
        // original clip (also used by the reference AudioManager).
        if (!loop) loop = std::pair{0u, unsigned(wave.frameCount)};
        if (wave.channels < 1 || wave.channels > 2 || loop->first >= loop->second ||
            loop->second > wave.frameCount || (rate && rate != wave.sampleRate) ||
            (emitters_ && emitters_->sampleRate != wave.sampleRate)) {
            UnloadWave(wave);
            throw std::runtime_error("Unsupported original travel PCM or loop bounds");
        }
        rate = wave.sampleRate;
        EmitterAudio::Sample sample;
        sample.channels = wave.channels;
        sample.frames = wave.frameCount;
        sample.loopBegin = loop->first;
        sample.loopEnd = loop->second;
        sample.volume = table.number(variant, "Volume").value_or(255) / 255.f;
        // Legacy reference SoundInfo/AudioManager use game frames (25 Hz).
        // D2R's audio-tick documentation cannot establish legacy fade units.
        sample.fadeIn = unsigned(uint64_t(std::max(0, table.number(variant, "Fade In").value_or(0))) * rate / 25);
        sample.fadeOut = unsigned(uint64_t(std::max(0, table.number(variant, "Fade Out").value_or(0))) * rate / 25);
        float *pcm = LoadWaveSamples(wave);
        if (!pcm) { UnloadWave(wave); throw std::runtime_error("Original travel PCM samples are unavailable"); }
        sample.pcm.assign(pcm, pcm + size_t(sample.frames) * sample.channels);
        UnloadWaveSamples(pcm);
        UnloadWave(wave);
        group.samples.push_back(std::move(sample));
    }
    if (!emitters_) {
        emitters_ = std::make_shared<EmitterAudio>();
        emitters_->open(rate);
    }
    if (emitters_->groups.contains(key))
        throw std::runtime_error("Original travel sound group was registered twice");
    emitters_->groups.emplace(std::move(key), std::move(group));
}
bool SoundBank::hasEmitterSound(const std::string &key) const {
    return emitters_ && emitters_->groups.contains(key);
}
void SoundBank::syncEmitters(std::span<const SoundEmitter> live, uint64_t frame) {
    if (!emitters_) return;
    std::lock_guard lock(emitters_->mutex);
    std::set<EntityId> present;
    for (const auto &emitter : live) {
        auto found = emitters_->groups.find(emitter.key);
        if (!emitter.entity || found == emitters_->groups.end()) continue;
        present.insert(emitter.entity);
        if (!emitters_->tracked.insert(emitter.entity).second) continue;
        auto &group = found->second;
        // A suppressed creation remains suppressed for that entity's lifetime.
        if (group.lastStart && frame - *group.lastStart < group.compound) continue;
        group.lastStart = frame;
        rollRandom(emitters_->random);
        const auto index = uint32_t(emitters_->random) % group.samples.size();
        emitters_->voices.emplace(emitter.entity, EmitterAudio::Voice{&group.samples[index]});
    }
    for (auto &[id, voice] : emitters_->voices)
        if (!present.contains(id)) voice.releasing = true;
    std::erase_if(emitters_->tracked, [&](EntityId id) { return !present.contains(id); });
}
void SoundBank::pauseEmitters(bool paused) {
    if (!emitters_ || !emitters_->stream.buffer) return;
    SetAudioStreamVolume(emitters_->stream, muted ? 0.f : 1.f);
    if (paused == emitters_->paused) return;
    emitters_->paused = paused;
    if (paused) PauseAudioStream(emitters_->stream);
    else ResumeAudioStream(emitters_->stream);
}
void SoundBank::resetEmitters() {
    for (auto &[key, group] : originalGroups_) group.lastStart.reset();
    if (!emitters_) return;
    // Flush queued audio as well as the voices; a new region must not play
    // samples already mixed for missiles from the preceding scene.
    StopAudioStream(emitters_->stream);
    {
        std::lock_guard lock(emitters_->mutex);
        emitters_->voices.clear();
        emitters_->tracked.clear();
        for (auto &[key, group] : emitters_->groups) group.lastStart.reset();
    }
    PlayAudioStream(emitters_->stream);
    if (emitters_->paused) PauseAudioStream(emitters_->stream);
}
} // namespace d2x
