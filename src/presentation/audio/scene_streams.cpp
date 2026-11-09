#include "scene_streams.hpp"
#include "content/audio/sound_catalog.hpp"
#include "content/classic_data.hpp"
#include "content/npc/npc_dialogue.hpp"
#include "content/string_table.hpp"
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <charconv>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <list>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>
#include <raylib.h>

namespace d2x {
namespace {
struct StreamRead {
    std::mutex mutex;
    std::atomic<bool> cancelled{};
    std::shared_ptr<const Bytes> bytes;
    std::string path, error;
    bool ready{};
};
class StreamLoader {
    std::unique_ptr<Archives> archives_;
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<std::weak_ptr<StreamRead>> pending_;
    std::list<std::pair<std::string, std::shared_ptr<const Bytes>>> cache_;
    size_t cacheBytes_{};
    std::jthread worker_;
  public:
    explicit StreamLoader(const Archives &archives) : archives_(archives.independent()),
        worker_([this](std::stop_token stop) {
            while (!stop.stop_requested()) {
                std::weak_ptr<StreamRead> request;
                {
                    std::unique_lock lock(mutex_);
                    if (!wake_.wait(lock, stop, [this] { return !pending_.empty(); })) return;
                    request = pending_.front();
                    pending_.pop_front();
                }
                const auto read = request.lock();
                if (!read || read->cancelled) continue;
                std::shared_ptr<const Bytes> bytes;
                std::string error;
                try {
                    const auto cached = std::find_if(cache_.begin(), cache_.end(),
                        [&](const auto &entry) { return entry.first == read->path; });
                    if (cached != cache_.end()) {
                        bytes = cached->second;
                        cache_.splice(cache_.end(), cache_, cached);
                    } else if (archives_->contains(read->path)) {
                        bytes = std::make_shared<const Bytes>(archives_->read(read->path, true,
                            [&] { return stop.stop_requested() || read->cancelled.load(); }));
                        if (stop.stop_requested() || read->cancelled) continue;
                        constexpr size_t budget = 64u * 1024 * 1024;
                        if (bytes->size() <= budget) {
                            while (!cache_.empty() && (cacheBytes_ + bytes->size() > budget || cache_.size() >= 64)) {
                                cacheBytes_ -= cache_.front().second->size();
                                cache_.pop_front();
                            }
                            cacheBytes_ += bytes->size();
                            cache_.emplace_back(read->path, bytes);
                        }
                    } else error = read->path;
                } catch (const std::exception &failure) {
                    error = read->path + ": " + failure.what();
                }
                std::lock_guard lock(read->mutex);
                read->bytes = std::move(bytes);
                read->error = std::move(error);
                read->ready = true;
            }
        }) {}
    std::shared_ptr<StreamRead> request(std::string path) {
        auto read = std::make_shared<StreamRead>();
        read->path = std::move(path);
        {
            std::lock_guard lock(mutex_);
            std::erase_if(pending_, [](const auto &entry) { return entry.expired(); });
            pending_.push_back(read);
        }
        wake_.notify_one();
        return read;
    }
};
struct Stream {
    std::shared_ptr<StreamRead> pending;
    std::shared_ptr<const Bytes> bytes;
    Music music{};
    int id = 0, volume = 255;
    std::atomic<int> modulation{255}, cursor{}, master{100}, musicGain{50};
    int resumeFrame{};
    bool musicVolume = false, looping = false;
    std::atomic<bool> releasing{}, failed{}, finished{}, paused{}, started{};
    float fadeIn = 0, fadeOut = 0;
    std::array<int, 3> blocks{-1, -1, -1};
    std::mutex waitMutex;
    std::condition_variable_any wake;
    std::jthread worker;
    ~Stream() {
        if (pending) pending->cancelled = true;
        worker.request_stop();
        wake.notify_all();
        if (worker.joinable()) worker.join();
    }
    void release() {
        if (music.ctxData) {
            void *decoder = music.ctxData;
            UnloadMusicStream(music);
            MemFree(decoder);
        }
        music = {};
        bytes.reset();
    }
    void stop() {
        pending->cancelled = true;
        releasing = true;
        wake.notify_all();
    }
    void unavailable(const std::string &message) {
        failed = true;
        release();
        std::cerr << "Original audio unavailable: " << message << '\n';
    }
    void update(float, const AudioSettings &settings) {
        master = settings.sound;
        musicGain = settings.music;
    }
    void start(const AudioSettings &settings) {
        update(0, settings);
        worker = std::jthread([this](std::stop_token stop) {
          try {
            while (!stop.stop_requested() && !releasing) {
                {
                    std::lock_guard lock(pending->mutex);
                    if (pending->ready) {
                        if (!pending->error.empty()) { unavailable(pending->error); return; }
                        bytes = std::move(pending->bytes);
                        break;
                    }
                }
                std::unique_lock lock(waitMutex);
                wake.wait_for(lock, stop, std::chrono::milliseconds(5), [] { return false; });
            }
            if (stop.stop_requested() || releasing) { pending->cancelled = true; finished = true; return; }
            music = LoadMusicStreamFromMemory(".wav", bytes->data(), int(bytes->size()));
            if (!music.ctxData || !music.stream.buffer || !music.frameCount)
                throw std::runtime_error("cannot decode " + pending->path);
            for (const int block : blocks)
                if (block >= 0 && unsigned(block) >= music.frameCount)
                    throw std::runtime_error("Original music resume block outside WAV: " + pending->path);
            music.looping = looping;
            for (int frame = 0; frame < resumeFrame && !stop.stop_requested() && !releasing;) {
                frame += std::min(resumeFrame - frame, int(music.stream.sampleRate / 20 + 1));
                SeekMusicStream(music, float(frame) / music.stream.sampleRate);
            }
            if (stop.stop_requested() || releasing) { release(); finished = true; return; }
            float envelope = fadeIn > 0 ? 0.f : 1.f;
            bool wasPaused = false;
            SetMusicVolume(music, 0);
            PlayMusicStream(music);
            started = true;
            auto previous = std::chrono::steady_clock::now();
            while (!stop.stop_requested()) {
                const auto now = std::chrono::steady_clock::now();
                const float dt = std::chrono::duration<float>(now - previous).count();
                previous = now;
                const bool isPaused = paused && !releasing;
                if (isPaused != wasPaused) {
                    if (isPaused) PauseMusicStream(music); else ResumeMusicStream(music);
                    wasPaused = isPaused;
                }
                if (!isPaused) {
                    envelope = releasing ? std::max(0.f, envelope - (fadeOut > 0 ? dt / fadeOut : 1.f))
                        : std::min(1.f, envelope + (fadeIn > 0 ? dt / fadeIn : 1.f));
                    SetMusicVolume(music, originalSoundGain(volume, master, musicVolume ? musicGain.load() : 100,
                        int(modulation.load() * envelope)));
                    if (releasing && envelope <= 0) break;
                    UpdateMusicStream(music);
                    cursor = int(GetMusicTimePlayed(music) * music.stream.sampleRate);
                    if (!looping && !IsMusicStreamPlaying(music)) break;
                }
                std::unique_lock lock(waitMutex);
                wake.wait_for(lock, stop, std::chrono::milliseconds(5), [] { return false; });
            }
            release();
            finished = true;
          } catch (const std::exception &error) { unavailable(error.what()); }
        });
    }
};
struct Speech { std::string sound; unsigned rate; };
}
struct SceneStreams::Impl {
    Archives &archives;
    const SoundCatalog &catalog;
    AudioSettings audio;
    std::map<int, int> songs, checkpoints;
    std::map<std::string, Speech, std::less<>> speeches;
    std::map<uint32_t, Speech> speechIds;
    unsigned scrollRate = 8;
    std::unique_ptr<Stream> song, speech;
    std::unique_ptr<Stream> rain;
    int rainVolume{};
    float rainFrames{};
    bool weatherPaused{};
    std::unique_ptr<Stream> questSong;
    struct QuestCue {std::string music,voice;double frames{};int start{},end{},voiceAt{};bool musicPending{true},voicePending{};};
    std::optional<QuestCue> quest;
    std::vector<std::unique_ptr<Stream>> fading;
    std::set<std::string> limitations;
    std::optional<int> requestedSong;
    int environmentSong{};
    float songAge = 0, checkpointAge = 0;
    StreamLoader loader, speechLoader, questLoader;
    ~Impl() {
        for (auto *stream : {song.get(), speech.get(), rain.get(), questSong.get()})
            if (stream) stream->stop();
        for (auto &stream : fading) stream->stop();
    }
    Impl(Archives &archives, const ClassicData *content, const SoundCatalog &catalog)
        : archives(archives), catalog(catalog), loader(archives), speechLoader(archives), questLoader(archives) {
        if (!content) return;
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
        for (const auto &[group, entries] : content->npcDialogues)
            for (const auto &entry : entries) {
                const auto raw = strings.find(entry.stringKey);
                const auto text = raw.empty() ? std::string_view(entry.text) : npcTblSpeech(raw);
                unsigned rate = entry.speed > 0 ? unsigned(entry.speed) : 8;
                if (text.size() < raw.size()) std::from_chars(raw.data(), raw.data()+raw.size(), rate);
                const Speech line{entry.soundId, rate};
                const int stringId = strings.index(entry.stringKey);
                if (stringId >= 0) speechIds.try_emplace(uint32_t(stringId), line);
                const auto key = npcSpeechText(text);
                if (key.empty()) continue;
                auto [found, inserted] = speeches.try_emplace(key, line);
                // Ambiguous text is never assigned another NPC's voice.
                if (!inserted && found->second.sound != entry.soundId) found->second.sound.clear();
            }
    }
    void unavailable(const std::string &message) {
        if (limitations.insert(message).second) std::cerr << "Original audio unavailable: " << message << '\n';
    }
    std::unique_ptr<Stream> load(size_t row, const std::string &root, int modulation = 255, int resume = 0) {
        const auto &table = catalog.sounds();
        const auto path = root + std::string(table.value(row, "FileName"));
        if (!IsAudioDeviceReady()) { unavailable("audio device"); return {}; }
        auto stream = std::make_unique<Stream>();
        const auto volume = table.number(row, "Volume");
        if (!volume || *volume < 0 || *volume > 255) throw std::runtime_error("Invalid original stream volume: " + path);
        stream->id = table.number(row, "Index").value_or(0);
        stream->volume = *volume;
        stream->modulation = modulation;
        stream->musicVolume = table.number(row, "Music Vol").value_or(0) != 0;
        stream->looping = table.number(row, "Loop").value_or(0) != 0;
        stream->fadeIn = table.number(row, "Fade In").value_or(0) / 25.f;
        stream->fadeOut = table.number(row, "Fade Out").value_or(0) / 25.f;
        stream->resumeFrame = resume;
        for (size_t index = 0; index < stream->blocks.size(); ++index) {
            const int block = table.number(row, "Block " + std::to_string(index + 1)).value_or(-1);
            stream->blocks[index] = block;
        }
        auto &source = root != "data/global/music/" ? speechLoader : stream->musicVolume ? loader : questLoader;
        stream->pending = source.request(path);
        stream->start(audio);
        return stream;
    }
    void checkpoint() {
        if (!song || !song->started || song->failed) return;
        const auto cursor = song->cursor.load();
        int next = 0;
        for (const int block : song->blocks)
            if (block > cursor) { next = block; break; }
        checkpoints[song->id] = next;
    }
    void retire(std::unique_ptr<Stream> &stream) {
        if (!stream) return;
        stream->stop();
        fading.push_back(std::move(stream));
    }
    void selectSong(int id) {
        environmentSong=id;
        if (!audio.sound || !audio.music) {
            retire(song);
            return;
        }
        if (song) {
            retire(song);
        }
        if (!id) return;
        const auto &table = catalog.sounds();
        for (size_t row = 0; row < table.rows().size(); ++row)
            if (table.number(row, "Index") == id) {
                song = load(row, "data/global/music/", 255, std::max(0, checkpoints[id]));
                break;
            }
        songAge = checkpointAge = 0;
    }
};
SceneStreams::SceneStreams(Archives &archives, const ClassicData &content, const SoundCatalog &catalog)
    : impl_(std::make_unique<Impl>(archives, &content, catalog)) {}
SceneStreams::SceneStreams(Archives &archives, const SoundCatalog &catalog)
    : impl_(std::make_unique<Impl>(archives, nullptr, catalog)) {}
SceneStreams::~SceneStreams() = default;
void SceneStreams::menu(bool active) {
    auto &state = *impl_;
    if (!active) {
        state.requestedSong = 0;
        state.song.reset();
        state.fading.clear();
        return;
    }
    const int desired = state.audio.sound && state.audio.music ? -1 : 0;
    if (!state.requestedSong || *state.requestedSong != desired) {
        state.requestedSong = desired;
        state.retire(state.song);
        if (desired) {
            state.song = std::make_unique<Stream>();
            state.song->musicVolume = true;
            state.song->looping = true;
            state.song->pending = state.loader.request("data/global/music/introedit.wav");
            state.song->start(state.audio);
        }
    }
    std::erase_if(state.fading, [](const auto &stream) { return stream->finished || stream->failed; });
}
void SceneStreams::settings(const AudioSettings &settings) {
    if (impl_->audio == settings) return;
    impl_->audio = settings;
    if (!settings.sound || !settings.music) {
        impl_->retire(impl_->song);
        impl_->requestedSong.reset();
    }
    if (settings.speech == NpcSpeechMode::TextOnly) stopSpeech();
    if (impl_->song) impl_->song->update(0, settings);
    if (impl_->speech) impl_->speech->update(0, settings);
    if (impl_->questSong) impl_->questSong->update(0, settings);
    if (impl_->rain) impl_->rain->update(0, settings);
    for (auto &stream : impl_->fading) stream->update(0, settings);
}
void SceneStreams::weather(float dt, bool rainAllowed, int density) {
    auto &state=*impl_;
    if (state.weatherPaused) return;
    if (!rainAllowed || !state.audio.sound) {
        // 80450/4FAA3 remove the instance when rain is disallowed. The
        // six-step volume change applies only while the instance is eligible.
        if (state.rain) state.rain->modulation=0;
        state.retire(state.rain); state.rainVolume=0; state.rainFrames=0;
        return;
    }
    const int target=std::clamp(density,0,256)*255/256;
    if (!state.rain && target>0) {
        if (const auto row=state.catalog.soundRow("scene_rain")) {
            state.rain=state.load(*row,"data/global/sfx/",target);
            state.rainVolume=target;
        } else state.unavailable("scene_rain");
    }
    state.rainFrames+=std::max(0.f,dt)*25.f;
    const int ticks=int(state.rainFrames); state.rainFrames-=float(ticks);
    for (int tick=0;tick<ticks;++tick)
        state.rainVolume+=std::clamp(target-state.rainVolume,-6,6);
    if (state.rain) {
        state.rain->modulation=state.rainVolume;
        if (!state.rainVolume) state.retire(state.rain);
        else state.rain->update(dt,state.audio);
    }
}
void SceneStreams::pauseWeather(bool paused) {
    auto &state=*impl_;
    if (state.weatherPaused==paused) return;
    state.weatherPaused=paused;
    if (state.rain) state.rain->paused = paused;
}
void SceneStreams::update(int level, float dt) {
    auto &state = *impl_;
    state.songAge += dt;
    state.checkpointAge += dt;
    const auto found = state.songs.find(level);
    const int desired = state.audio.sound && state.audio.music && found != state.songs.end() ? found->second : 0;
    // RVA 45740: retain a playing song for at least 75 game frames before
    // changing environments. Zero volume stops it without that wait.
    if (!state.quest && (!state.requestedSong || *state.requestedSong != desired) && (!state.song || state.songAge >= 3 || !desired)) {
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
    std::erase_if(state.fading, [](const auto &stream) { return stream->finished || stream->failed; });
    if (state.speech) state.speech->update(dt, state.audio);
    if (state.questSong) state.questSong->update(dt, state.audio);
}
void SceneStreams::questCue(std::string_view music,int startFrames,int endFrames,std::string_view voice,int voiceFrames,int resumeBlock) {
    if(startFrames<0 || endFrames<startFrames || voiceFrames<0 || resumeBlock<0 || resumeBlock>3) return;
    auto &state=*impl_;
    // 455B8..455E8 saves the selected fixed Block marker, then stops music.
    // Replacing a cue replaces its unplayed voice too; this is not a FIFO.
    if(state.song) {
        state.checkpoints[state.song->id]=resumeBlock?state.song->blocks[size_t(resumeBlock-1)]:0;
        state.retire(state.song);
    } else if(state.environmentSong) {
        // The native environment song identity survives while another task
        // cue is playing. Replacing that cue still rewrites its resume marker.
        const auto &table=state.catalog.sounds();
        for(size_t row=0;row<table.rows().size();++row) if(table.number(row,"Index")==state.environmentSong) {
            state.checkpoints[state.environmentSong]=resumeBlock?table.number(row,"Block "+std::to_string(resumeBlock)).value_or(0):0;
            break;
        }
    }
    state.retire(state.questSong);
    state.quest=Impl::QuestCue{std::string(music),std::string(voice),0,startFrames,endFrames,voiceFrames,true,!voice.empty()};
    state.requestedSong.reset();
}
std::optional<std::string> SceneStreams::advanceQuestCue(float dt,bool playerAlive) {
    auto &state=*impl_;
    if(!state.quest || !std::isfinite(dt) || dt<0) return {};
    auto &cue=*state.quest;cue.frames+=double(dt)*25.0;
    std::optional<std::string> voice;
    if(cue.musicPending && cue.frames>=cue.start) {
        cue.musicPending=false;
        // Task cues have their own Sounds.Music Vol flag (currently zero).
        // Muting environment music must not mute a sound-volume task cue.
        if(!cue.music.empty() && state.audio.sound) {
            if(const auto row=state.catalog.soundRow(cue.music)) state.questSong=state.load(*row,"data/global/music/");
            else state.unavailable(cue.music);
        }
    }
    if(cue.voicePending && cue.frames>=cue.voiceAt) {
        cue.voicePending=false;
        if(playerAlive) voice=cue.voice; // 457E9..45813 consumes even when the player is dead.
    }
    if(cue.frames>=cue.end) {
        state.retire(state.questSong);
        state.quest.reset();state.requestedSong.reset();
    }
    return voice;
}
void SceneStreams::resetQuestCue() {
    if(!impl_->quest && !impl_->questSong) return;
    impl_->retire(impl_->questSong);impl_->quest.reset();impl_->requestedSong.reset();
}
void SceneStreams::speak(std::string_view text, std::optional<uint32_t> stringId) {
    stopSpeech();
    auto &state = *impl_;
    const Speech *line = nullptr;
    if (stringId) {
        const auto found = state.speechIds.find(*stringId);
        if (found != state.speechIds.end()) line = &found->second;
    }
    if (!line) {
        const auto found = state.speeches.find(npcSpeechText(text));
        if (found != state.speeches.end()) line = &found->second;
    }
    state.scrollRate = line && line->rate ? line->rate : 8;
    if (state.audio.speech == NpcSpeechMode::TextOnly) return;
    if (!line || line->sound.empty()) {
        state.unavailable("unresolved NPC speech identity"); return;
    }
    const auto &sound = line->sound;
    const auto row = state.catalog.soundRow(sound);
    if (!row) { state.unavailable(sound); return; }
    state.speech = state.load(*row, "data/local/sfx/");
}
unsigned SceneStreams::speechScrollRate() const { return impl_->scrollRate; }
void SceneStreams::stopSpeech() {
    impl_->retire(impl_->speech);
}
std::optional<bool> SceneStreams::speechFinished() const {
    if (!impl_->speech) return {};
    if (impl_->speech->failed) return {};
    return impl_->speech->finished.load();
}
} // namespace d2x
