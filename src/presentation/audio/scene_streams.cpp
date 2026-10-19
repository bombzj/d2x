#include "scene_streams.hpp"
#include "content/audio/sound_catalog.hpp"
#include "content/classic_data.hpp"
#include "content/npc/npc_dialogue.hpp"
#include "content/string_table.hpp"
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>
#include <raylib.h>

namespace d2x {
namespace {
struct Stream {
    Bytes bytes;
    Music music{};
    int id = 0, volume = 255;
    bool musicVolume = false, releasing = false;
    float envelope = 0, fadeIn = 0, fadeOut = 0;
    std::array<int, 3> blocks{-1, -1, -1};
    ~Stream() { if (music.ctxData) UnloadMusicStream(music); }
    void update(float dt, const AudioSettings &settings) {
        envelope = releasing ? std::max(0.f, envelope - (fadeOut > 0 ? dt / fadeOut : 1.f))
            : std::min(1.f, envelope + (fadeIn > 0 ? dt / fadeIn : 1.f));
        SetMusicVolume(music, originalSoundGain(volume, settings.sound, musicVolume ? settings.music : 100, int(255 * envelope)));
        UpdateMusicStream(music);
    }
};
struct Speech { std::string sound; };
}
struct SceneStreams::Impl {
    Archives &archives;
    const SoundCatalog &catalog;
    AudioSettings audio;
    std::map<int, int> songs, checkpoints;
    std::map<std::string, Speech, std::less<>> speeches;
    std::map<uint32_t, Speech> speechIds;
    std::unique_ptr<Stream> song, speech;
    std::vector<std::unique_ptr<Stream>> fading;
    std::set<std::string> limitations;
    std::optional<int> requestedSong;
    float songAge = 0, checkpointAge = 0;
    bool speechStarted = false;
    Impl(Archives &archives, const ClassicData &content, const SoundCatalog &catalog)
        : archives(archives), catalog(catalog) {
        const DataTable levels(archives.read("data/global/excel/levels.txt"));
        const DataTable environments(archives.read("data/global/excel/soundenviron.txt"));
        std::map<int, int> environmentSongs;
        for (size_t row = 0; row < environments.rows().size(); ++row)
            if (auto index = environments.number(row, "Index"))
                environmentSongs.emplace(*index, environments.number(row, "Song").value_or(0));
        for (size_t row = 0; row < levels.rows().size(); ++row)
            if (auto id = levels.number(row, "Id")) {
                auto environment = environmentSongs.find(levels.number(row, "SoundEnv").value_or(0));
                if (environment != environmentSongs.end()) songs.emplace(*id, environment->second);
            }
        const ClassicStrings strings(archives);
        for (const auto &[group, entries] : content.npcDialogues)
            for (const auto &entry : entries) {
                if (entry.soundId.empty()) continue;
                const int stringId = strings.index(entry.stringKey);
                if (stringId >= 0) speechIds.try_emplace(uint32_t(stringId), Speech{entry.soundId});
                const auto text = entry.stringKey.empty() ? std::string_view(entry.text)
                    : npcTblSpeech(strings.find(entry.stringKey));
                const auto key = npcSpeechText(text);
                if (key.empty()) continue;
                auto [found, inserted] = speeches.try_emplace(key, Speech{entry.soundId});
                // Ambiguous text is never assigned another NPC's voice.
                if (!inserted && found->second.sound != entry.soundId) found->second.sound.clear();
            }
    }
    void unavailable(const std::string &message) {
        if (limitations.insert(message).second) std::cerr << "Original audio unavailable: " << message << '\n';
    }
    std::unique_ptr<Stream> load(size_t row, const std::string &root) {
        const auto &table = catalog.sounds();
        const auto path = root + std::string(table.value(row, "FileName"));
        if (!archives.contains(path)) { unavailable(path); return {}; }
        if (!IsAudioDeviceReady()) { unavailable("audio device"); return {}; }
        auto stream = std::make_unique<Stream>();
        stream->bytes = archives.read(path);
        stream->music = LoadMusicStreamFromMemory(".wav", stream->bytes.data(), int(stream->bytes.size()));
        if (!stream->music.ctxData || !stream->music.stream.buffer || !stream->music.frameCount) {
            unavailable("cannot decode " + path); return {};
        }
        const auto volume = table.number(row, "Volume");
        if (!volume || *volume < 0 || *volume > 255) throw std::runtime_error("Invalid original stream volume: " + path);
        stream->id = table.number(row, "Index").value_or(0);
        stream->volume = *volume;
        stream->musicVolume = table.number(row, "Music Vol").value_or(0) != 0;
        stream->music.looping = table.number(row, "Loop").value_or(0) != 0;
        stream->fadeIn = table.number(row, "Fade In").value_or(0) / 25.f;
        stream->fadeOut = table.number(row, "Fade Out").value_or(0) / 25.f;
        stream->envelope = stream->fadeIn > 0 ? 0.f : 1.f;
        for (size_t index = 0; index < stream->blocks.size(); ++index) {
            const int block = table.number(row, "Block " + std::to_string(index + 1)).value_or(-1);
            if (block >= 0 && unsigned(block) >= stream->music.frameCount)
                throw std::runtime_error("Original music resume block outside WAV: " + path);
            stream->blocks[index] = block;
        }
        // Set gain before starting: a muted preference must never leak one
        // buffer at raylib's default volume when a stream is opened.
        stream->update(0, audio);
        PlayMusicStream(stream->music);
        return stream;
    }
    void checkpoint() {
        if (!song) return;
        const auto cursor = int(GetMusicTimePlayed(song->music) * song->music.stream.sampleRate);
        int next = 0;
        for (const int block : song->blocks)
            if (block > cursor) { next = block; break; }
        checkpoints[song->id] = next;
    }
    void selectSong(int id) {
        if (!audio.sound || !audio.music) {
            song.reset();
            fading.clear();
            return;
        }
        if (song) {
            song->releasing = true;
            fading.push_back(std::move(song));
        }
        if (!id) return;
        const auto &table = catalog.sounds();
        for (size_t row = 0; row < table.rows().size(); ++row)
            if (table.number(row, "Index") == id) {
                song = load(row, "data/global/music/");
                if (song && checkpoints[id] > 0)
                    SeekMusicStream(song->music, float(checkpoints[id]) / song->music.stream.sampleRate);
                break;
            }
        songAge = checkpointAge = 0;
    }
};
SceneStreams::SceneStreams(Archives &archives, const ClassicData &content, const SoundCatalog &catalog)
    : impl_(std::make_unique<Impl>(archives, content, catalog)) {}
SceneStreams::~SceneStreams() = default;
void SceneStreams::settings(const AudioSettings &settings) {
    if (impl_->audio == settings) return;
    impl_->audio = settings;
    if (settings.speech == NpcSpeechMode::TextOnly) stopSpeech();
    if (impl_->song) impl_->song->update(0, settings);
    if (impl_->speech) impl_->speech->update(0, settings);
    for (auto &stream : impl_->fading) stream->update(0, settings);
}
void SceneStreams::update(int level, float dt) {
    auto &state = *impl_;
    state.songAge += dt;
    state.checkpointAge += dt;
    const auto found = state.songs.find(level);
    const int desired = state.audio.sound && state.audio.music && found != state.songs.end() ? found->second : 0;
    // RVA 45740: retain a playing song for at least 75 game frames before
    // changing environments. Zero volume stops it without that wait.
    if ((!state.requestedSong || *state.requestedSong != desired) && (!state.song || state.songAge >= 3 || !desired)) {
        state.requestedSong = desired;
        state.selectSong(desired);
    }
    if (state.song) {
        state.song->update(dt, state.audio);
        // RVA 458E8-45958: every 125 frames remember the next Block marker;
        // re-entering this song resumes there, rather than inventing a loop.
        if (state.checkpointAge > 5) { state.checkpoint(); state.checkpointAge = 0; }
    }
    for (auto &stream : state.fading) stream->update(dt, state.audio);
    std::erase_if(state.fading, [](const auto &stream) { return stream->envelope <= 0; });
    if (state.speech) state.speech->update(dt, state.audio);
}
void SceneStreams::speak(std::string_view text, std::optional<uint32_t> stringId) {
    stopSpeech();
    auto &state = *impl_;
    if (state.audio.speech == NpcSpeechMode::TextOnly) return;
    std::string sound;
    if (stringId) {
        const auto found = state.speechIds.find(*stringId);
        if (found != state.speechIds.end()) sound = found->second.sound;
    } else {
        const auto found = state.speeches.find(npcSpeechText(text));
        if (found != state.speeches.end()) sound = found->second.sound;
    }
    if (sound.empty()) {
        state.unavailable("unresolved NPC speech identity"); return;
    }
    const auto row = state.catalog.soundRow(sound);
    if (!row) { state.unavailable(sound); return; }
    state.speech = state.load(*row, "data/local/sfx/");
    state.speechStarted = bool(state.speech);
}
void SceneStreams::stopSpeech() { impl_->speech.reset(); impl_->speechStarted = false; }
bool SceneStreams::speechFinished() const {
    return impl_->speechStarted && impl_->speech && !IsMusicStreamPlaying(impl_->speech->music);
}
} // namespace d2x
