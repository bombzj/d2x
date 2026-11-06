#include "audio.hpp"
#include "core/random.hpp"
#include "resources/formats.hpp"
#include <external/dr_wav.h>
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
        std::string soundId;
        unsigned channels = 0, frames = 0, loopBegin = 0, loopEnd = 0;
        unsigned fadeIn = 0, fadeOut = 0;
        unsigned rate = 0, duration = 0;
        int volume = 255;
        bool looping = false, stopInstance = false;
        bool deferInstance = false;
        bool musicVolume = false;
        float nearDistance = 60, farDistance = 700;
    };
    struct Group {
        std::vector<Sample> samples;
        unsigned compound = 0;
        std::optional<uint64_t> lastStart;
        bool travel = false;
    };
    struct Voice {
        const Sample *sample = nullptr;
        double cursor = 0, played = 0, released = 0;
        bool releasing = false, finished = false;
        EntityId entity{};
        EntityId source{};
        Vec position{};
        int volume = 255;
    };
    std::map<std::string, Group> groups;
    std::map<uint64_t, Voice> voices;
    uint64_t nextVoice{};
    std::set<EntityId> tracked;
    std::map<EntityId, Vec> sources;
    Vec listener;
    std::mutex mutex;
    AudioStream stream{};
    unsigned sampleRate = 0;
    bool paused = false;
    int masterVolume = 100;
    int musicVolume = 50;
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
            if (paused && (voice.entity || voice.source) && !voice.releasing) continue;
            const auto &sample = *voice.sample;
            const auto relative = project(voice.position - listener);
            const float distance = std::hypot(relative.x, relative.y * 2);
            const float spatial = voice.entity || voice.source ?
                (distance <= sample.nearDistance ? 1.f : distance >= sample.farDistance ? 0.f :
                    (sample.farDistance - distance) / (sample.farDistance - sample.nearDistance)) : 1.f;
            const double step = double(sample.rate) / sampleRate;
            for (unsigned i = 0; i < count && !voice.finished; ++i) {
                if (sample.duration && voice.played > sample.duration) { voice.finished = true; break; }
                int envelope = 255 * (sample.musicVolume ? musicVolume : 100) / 100;
                envelope = envelope * masterVolume / 100;
                envelope = int(float(envelope) * spatial);
                float gain = float(envelope * voice.volume / 255) / 255.f;
                if (sample.fadeIn && voice.played < sample.fadeIn)
                    gain *= float(voice.played) / sample.fadeIn;
                if (voice.releasing) {
                    if (voice.released >= sample.fadeOut) { voice.finished = true; break; }
                    gain *= 1.f - float(voice.released) / sample.fadeOut;
                    voice.released += step;
                }
                const size_t at = size_t(voice.cursor) * sample.channels;
                const auto nextFrame = size_t(voice.cursor) + 1 < sample.loopEnd ? size_t(voice.cursor) + 1
                    : sample.looping ? size_t(sample.loopBegin) : size_t(voice.cursor);
                const size_t next = nextFrame * sample.channels;
                const float fraction = float(voice.cursor - std::floor(voice.cursor));
                const size_t right = sample.channels == 2 ? 1 : 0;
                output[size_t(i) * 2] += std::lerp(sample.pcm[at], sample.pcm[next], fraction) * gain;
                output[size_t(i) * 2 + 1] += std::lerp(sample.pcm[at + right], sample.pcm[next + right], fraction) * gain;
                voice.played += step;
                voice.cursor += step;
                if (voice.cursor >= sample.loopEnd) {
                    if (sample.looping) voice.cursor = sample.loopBegin +
                        std::fmod(voice.cursor - sample.loopBegin, double(sample.loopEnd - sample.loopBegin));
                    else voice.finished = true;
                }
            }
        }
        std::erase_if(voices, [](const auto &entry) { return entry.second.finished; });
        // Mix before clamping so overlapping voices do not restart one another.
        for (size_t i = 0; i < size_t(count) * 2; ++i)
            output[i] = std::clamp(output[i], -1.f, 1.f);
    }
};

void SoundBank::registerGroup(Archives &archives, std::string key, const DataTable *table, size_t row,
                              bool travel, std::stop_token stop, std::string_view file) {
    if (!enabled) return;
    const auto number = [&](size_t index, std::string_view column) -> std::optional<int> {
        return table ? table->number(index, column) : std::nullopt;
    };
    EmitterAudio::Group group;
    group.travel = travel;
    const int count = std::max(1, number(row, "Group Size").value_or(0));
    if (table && row + size_t(count) > table->rows().size())
        throw std::runtime_error("Original travel sound group is truncated");
    group.compound = unsigned(std::max(0, number(row, "Compound").value_or(0)));
    unsigned rate = 0;
    for (int i = 0; i < count; ++i) {
        if (stop.stop_requested()) return;
        const auto variant = row + size_t(i);
        const std::string soundId(table ? table->value(variant, "Sound") : file);
        const int defer = number(variant, "Defer Inst").value_or(0);
        if (defer != 0 && defer != 1)
            throw std::runtime_error("Invalid original travel Defer Inst: " + soundId);
        for (const auto field : {"Loop", "Stop Inst"}) {
            const int flag = number(variant, field).value_or(0);
            if (flag != 0 && flag != 1) throw std::runtime_error("Invalid original sound flag: " + soundId);
        }
        for (const auto field : {"Duration", "Fade In", "Fade Out"})
            if (number(variant, field).value_or(0) < 0)
                throw std::runtime_error("Invalid original sound timing: " + soundId);
        const int compound = number(variant, "Compound").value_or(0);
        if ((compound < 0 && !(travel && compound == -1)) ||
            unsigned(std::max(0, compound)) != group.compound)
            throw std::runtime_error("Inconsistent original sound Compound: " + soundId);
        const auto volume = table ? number(variant, "Volume") : std::optional<int>(255);
        if (!volume || *volume < 0 || *volume > 255)
            throw std::runtime_error("Invalid original sound volume: " + soundId);
        auto path=table ? "data/global/sfx/" + std::string(table->value(variant, "FileName")) : std::string(file);
        if(table && !archives.contains(path)) {
            const auto speechPath="data/local/sfx/" + std::string(table->value(variant, "FileName"));
            if(archives.contains(speechPath)) path=speechPath;
        }
        const auto bytes = archives.read(path, true,
            [&] { return stop.stop_requested(); });
        if (stop.stop_requested()) return;
        Reader reader(bytes);
        if (reader.u32() != 0x46464952 || reader.u32() + uint64_t(8) != bytes.size() || reader.u32() != 0x45564157)
            throw std::runtime_error("Original travel WAV header is invalid");
        std::optional<std::pair<unsigned, unsigned>> loop;
        while (reader.pos + 8 <= bytes.size()) {
            const auto tag = reader.u32(), length = reader.u32();
            const auto begin = reader.pos;
            reader.need(length);
            if (tag == 0x6c706d73) { // RIFF smpl, offsets are PCM sample frames.
                if (length < 36) throw std::runtime_error("Original WAV sampler header is truncated: " + path);
                reader.skip(28);
                const auto loopCount=reader.u32();
                if(uint64_t(loopCount)*24+36>length)
                    throw std::runtime_error("Original WAV loop is truncated: " + path);
                if(loopCount) {
                    if (loopCount != 1) throw std::runtime_error("Unsupported original WAV loop count: " + path);
                    reader.skip(8);
                    if (reader.u32() != 0) throw std::runtime_error("Unsupported original WAV loop type: " + path);
                    const auto first = reader.u32(), last = reader.u32();
                    if (last == UINT32_MAX) throw std::runtime_error("Original WAV loop end overflows: " + path);
                    if (reader.u32() != 0 || reader.u32() != 0)
                        throw std::runtime_error("Unsupported original WAV fractional or finite loop: " + path);
                    loop = std::pair{first, last + 1}; // smpl's end is inclusive.
                }
            }
            reader.seek(begin + length + (length & 1));
        }
        const int block = number(variant, "Block 1").value_or(-1);
        if (travel && ((block != -1 && (!loop || unsigned(block) != loop->first)) ||
            number(variant, "Block 2").value_or(-1) != -1 ||
            number(variant, "Block 3").value_or(-1) != -1))
            throw std::runtime_error("Original travel WAV loop and Sounds.Block 1 disagree");
        drwav decoder{};
        if (!drwav_init_memory(&decoder, bytes.data(), bytes.size(), nullptr))
            throw std::runtime_error("Original WAV could not be decoded: " + soundId);
        const auto cleanup = [](drwav *value) { drwav_uninit(value); };
        const std::unique_ptr<drwav, decltype(cleanup)> guard(&decoder, cleanup);
        if (decoder.totalPCMFrameCount > UINT32_MAX)
            throw std::runtime_error("Original WAV is too long: " + soundId);
        const auto frames = unsigned(decoder.totalPCMFrameCount);
        if (!loop) loop = std::pair{0u, frames};
        const bool looping = number(variant, "Loop").value_or(0) != 0;
        if (!looping) loop = std::pair{0u, frames};
        if (decoder.channels < 1 || decoder.channels > 2 || loop->first >= loop->second ||
            loop->second > frames || !decoder.sampleRate) {
            throw std::runtime_error("Unsupported original travel PCM or loop bounds");
        }
        rate = decoder.sampleRate;
        EmitterAudio::Sample sample;
        sample.soundId = soundId;
        sample.rate = rate;
        sample.looping = looping;
        sample.stopInstance = number(variant, "Stop Inst").value_or(0) != 0;
        sample.duration = unsigned(uint64_t(number(variant, "Duration").value_or(0)) * rate / 25);
        sample.deferInstance = defer != 0;
        sample.musicVolume = number(variant, "Music Vol").value_or(0) != 0;
        const int falloff = number(variant, "Falloff").value_or(1);
        static constexpr float nearDistances[]{200,60,200,400,2000};
        static constexpr float farDistances[]{400,700,1000,1500,2000};
        const size_t falloffIndex = falloff >= 0 && falloff <= 4 ? size_t(falloff) : 1;
        sample.nearDistance = nearDistances[falloffIndex];
        sample.farDistance = farDistances[falloffIndex];
        sample.channels = decoder.channels;
        sample.frames = frames;
        sample.loopBegin = loop->first;
        sample.loopEnd = loop->second;
        sample.volume = *volume;
        // Legacy reference SoundInfo/AudioManager use game frames (25 Hz).
        // D2R's audio-tick documentation cannot establish legacy fade units.
        sample.fadeIn = unsigned(uint64_t(std::max(0, number(variant, "Fade In").value_or(0))) * rate / 25);
        sample.fadeOut = unsigned(uint64_t(std::max(0, number(variant, "Fade Out").value_or(0))) * rate / 25);
        sample.pcm.resize(size_t(frames) * sample.channels);
        for (unsigned offset = 0; offset < frames;) {
            if (stop.stop_requested()) return;
            const auto count = std::min(4096u, frames - offset);
            if (drwav_read_pcm_frames_f32(&decoder, count, sample.pcm.data() + size_t(offset) * sample.channels) != count)
                throw std::runtime_error("Original WAV is truncated: " + soundId);
            offset += count;
        }
        group.samples.push_back(std::move(sample));
    }
    if (!emitters_) {
        emitters_ = std::make_shared<EmitterAudio>();
        emitters_->masterVolume = volume_;
        emitters_->musicVolume = musicVolume_;
        emitters_->open(rate);
    }
    std::lock_guard lock(emitters_->mutex);
    if (emitters_->groups.contains(key))
        throw std::runtime_error("Original travel sound group was registered twice");
    emitters_->groups.emplace(std::move(key), std::move(group));
}
void SoundBank::stopAllReady() {
    if (!emitters_) return;
    std::lock_guard lock(emitters_->mutex);
    emitters_->voices.clear();
    emitters_->tracked.clear();
    for (auto &[key, group] : emitters_->groups) group.lastStart.reset();
}
void SoundBank::playReady(const std::string &name, uint64_t frame, float volume, EntityId source) {
    if (!emitters_) return;
    std::lock_guard lock(emitters_->mutex);
    const auto found = emitters_->groups.find(name);
    if (found == emitters_->groups.end() || found->second.travel) return;
    auto &group = found->second;
    if (group.lastStart && frame >= *group.lastStart && frame - *group.lastStart < group.compound) return;
    const auto &sample = group.samples[limitedRandom(groupRandom_, uint32_t(group.samples.size()))];
    if (sample.deferInstance && std::any_of(emitters_->voices.begin(), emitters_->voices.end(), [&](const auto &entry) {
        return !entry.second.finished && entry.second.sample->soundId == sample.soundId;
    })) return;
    group.lastStart = frame;
    if (sample.stopInstance)
        for (auto &[id, voice] : emitters_->voices)
            if (voice.sample->soundId == sample.soundId) voice.releasing = true;
    EmitterAudio::Voice voice{};
    voice.sample = &sample;
    voice.source = source;
    if (source) {
        const auto at = emitters_->sources.find(source);
        if (at == emitters_->sources.end()) return;
        voice.position = at->second;
    }
    voice.volume = volume >= 0 ? int(255 * std::clamp(volume, 0.f, 1.f)) : sample.volume;
    emitters_->voices.emplace(++emitters_->nextVoice, voice);
}
void SoundBank::syncSourcesReady(std::span<const SoundSource> sources, Vec listener) {
    if (!emitters_) return;
    std::lock_guard lock(emitters_->mutex);
    emitters_->listener = listener;
    emitters_->sources.clear();
    for (const auto &source : sources) emitters_->sources.emplace(source.entity, source.position);
    for (auto &[id, voice] : emitters_->voices) {
        if (!voice.source) continue;
        const auto at = emitters_->sources.find(voice.source);
        if (at == emitters_->sources.end()) voice.releasing = true;
        else voice.position = at->second;
    }
}
void SoundBank::volumesReady(int sound, int music) {
    sound = std::clamp(sound, 0, 100);
    music = std::clamp(music, 0, 100);
    if (volume_ == sound && musicVolume_ == music) return;
    volume_ = sound;
    musicVolume_ = music;
    if (emitters_) {
        std::lock_guard lock(emitters_->mutex);
        emitters_->masterVolume = volume_;
        emitters_->musicVolume = musicVolume_;
    }
}
void SoundBank::syncReady(std::span<const SoundEmitter> live, uint64_t frame) {
    if (!emitters_) return;
    std::lock_guard lock(emitters_->mutex);
    std::set<EntityId> present;
    for (const auto &emitter : live) {
        auto found = emitters_->groups.find(emitter.key);
        if (!emitter.entity || found == emitters_->groups.end()) continue;
        present.insert(emitter.entity);
        for (auto &[id, voice] : emitters_->voices)
            if (voice.entity == emitter.entity) voice.position = emitter.position;
        if (!emitters_->tracked.insert(emitter.entity).second) continue;
        auto &group = found->second;
        // A suppressed creation remains suppressed for that entity's lifetime.
        if (group.lastStart && frame >= *group.lastStart && frame - *group.lastStart < group.compound) continue;
        rollRandom(emitters_->random);
        const auto index = uint32_t(emitters_->random) % group.samples.size();
        const auto &sample = group.samples[index];
        // Defer Inst rejects a duplicate request while the original sound is
        // playing, including its fade-out. Compare Sounds identities across
        // event keys; different missile registrations can use the same sound.
        if (sample.deferInstance && std::any_of(emitters_->voices.begin(), emitters_->voices.end(),
                [&](const auto &entry) {
                    return !entry.second.finished && entry.second.sample->soundId == sample.soundId;
                })) continue;
        group.lastStart = frame;
        if (sample.stopInstance)
            for (auto &[id, voice] : emitters_->voices)
                if (voice.sample->soundId == sample.soundId) voice.releasing = true;
        EmitterAudio::Voice voice{};
        voice.sample = &sample;
        voice.entity = emitter.entity;
        voice.position = emitter.position;
        voice.volume = sample.volume;
        emitters_->voices.emplace(++emitters_->nextVoice, voice);
    }
    for (auto &[id, voice] : emitters_->voices)
        if (voice.entity && !present.contains(voice.entity)) voice.releasing = true;
    std::erase_if(emitters_->tracked, [&](EntityId id) { return !present.contains(id); });
}
void SoundBank::pauseReady(bool paused) {
    if (!emitters_) return;
    std::lock_guard lock(emitters_->mutex);
    emitters_->paused = paused;
}
void SoundBank::resetReady() {
    if (!emitters_) return;
    {
        std::lock_guard lock(emitters_->mutex);
        for (auto &[id, voice] : emitters_->voices)
            if (voice.entity || voice.source) voice.releasing = true;
        emitters_->tracked.clear();
        emitters_->sources.clear();
        for (auto &[key, group] : emitters_->groups) group.lastStart.reset();
    }
}
} // namespace d2x
