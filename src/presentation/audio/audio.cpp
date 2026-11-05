#include "audio.hpp"
#include "core/random.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
namespace d2x {
struct SoundBank::Async {
    std::unique_ptr<Archives> archives;
    std::mutex mutex;
    std::condition_variable_any wake;
    std::deque<std::function<void()>> commands;
    std::set<std::string> travel;
    std::set<std::string> prepared;
    std::vector<std::string> failures;
    std::vector<SoundEmitter> live;
    uint64_t frame{};
    bool syncPending{};
    bool controlsPending{}, resetPending{}, paused{};
    int sound = 100, music = 50;
    uint64_t controlsRevision{}, appliedControlsRevision{}, appliedStopGeneration{};
    std::atomic<uint64_t> generation{};
    std::atomic<uint64_t> stopGeneration{};
    std::jthread worker;
};
SoundBank::SoundBank(Archives &archives)
    : groupRandom_(initialRandom(0)), enabled(IsAudioDeviceReady()), async_(std::make_unique<Async>()) {
    async_->archives = archives.independent();
    async_->worker = std::jthread([this](std::stop_token stop) {
        while (!stop.stop_requested()) {
            std::function<void()> command;
            std::vector<SoundEmitter> live;
            uint64_t frame = 0;
            bool sync = false;
            bool controls = false, reset = false, paused = false;
            int sound = 100, music = 50;
            uint64_t controlsRevision = 0;
            {
                std::unique_lock lock(async_->mutex);
                if (!async_->wake.wait(lock, stop, [this] {
                    return !async_->commands.empty() || async_->syncPending || async_->controlsPending || async_->resetPending;
                })) break;
                controls = std::exchange(async_->controlsPending, false);
                reset = std::exchange(async_->resetPending, false);
                paused = async_->paused;
                sound = async_->sound;
                music = async_->music;
                controlsRevision = async_->controlsRevision;
                if (async_->syncPending) {
                    live = async_->live;
                    frame = async_->frame;
                    sync = true;
                    async_->syncPending = false;
                }
                if (!async_->commands.empty()) {
                    command = std::move(async_->commands.front());
                    async_->commands.pop_front();
                }
            }
            try {
                if (reset) resetReady();
                if (controls) {
                    volumesReady(sound, music);
                    pauseReady(paused);
                    std::lock_guard lock(async_->mutex);
                    async_->appliedControlsRevision = controlsRevision;
                }
                if (sync) syncReady(live, frame);
                if (command) command();
            } catch (const std::exception &error) {
                std::cerr << "Original sound unavailable: " << error.what() << '\n';
                std::lock_guard lock(async_->mutex);
                async_->failures.emplace_back(error.what());
            }
        }
        emitters_.reset();
    });
}
SoundBank::~SoundBank() {
    async_->worker.request_stop();
    async_->wake.notify_all();
    if (async_->worker.joinable()) async_->worker.join();
}
void SoundBank::enqueue(std::function<void()> command) {
    {
        std::lock_guard lock(async_->mutex);
        async_->commands.push_back(std::move(command));
    }
    async_->wake.notify_one();
}
void SoundBank::registerOriginalGroup(Archives &, std::string key, const DataTable &table, size_t row) {
    if (!enabled) return;
    enqueue([this, key = std::move(key), &table, row] {
        registerGroup(*async_->archives, key, &table, row, false, async_->worker.get_stop_token());
        std::lock_guard lock(async_->mutex);
        async_->prepared.insert(key);
    });
}
void SoundBank::registerTravelGroup(Archives &, std::string key, const DataTable &table, size_t row) {
    if (!enabled) return;
    {
        std::lock_guard lock(async_->mutex);
        async_->travel.insert(key);
    }
    enqueue([this, key = std::move(key), &table, row] {
        registerGroup(*async_->archives, key, &table, row, true, async_->worker.get_stop_token());
        std::lock_guard lock(async_->mutex);
        async_->syncPending = true;
        async_->controlsPending = true;
    });
}
bool SoundBank::hasEmitterSound(const std::string &key) const {
    std::lock_guard lock(async_->mutex);
    return async_->travel.contains(key);
}
void SoundBank::registerFile(std::string path) {
    if (!enabled) return;
    enqueue([this, path = std::move(path)] {
        registerGroup(*async_->archives, path, nullptr, 0, false, async_->worker.get_stop_token(), path);
        std::lock_guard lock(async_->mutex);
        async_->prepared.insert(path);
    });
}
void SoundBank::stopAll() {
    const auto generation = ++async_->stopGeneration;
    enqueue([this, generation] {
        stopAllReady();
        std::lock_guard lock(async_->mutex);
        async_->appliedStopGeneration = generation;
    });
}
void SoundBank::play(const std::string &name, uint64_t frame, float volume, bool transient) {
    if (!enabled) return;
    if (!transient) {
        std::lock_guard lock(async_->mutex);
        if (async_->prepared.contains(name) &&
            async_->appliedControlsRevision == async_->controlsRevision &&
            async_->appliedStopGeneration == async_->stopGeneration) {
            playReady(name, frame, volume);
            return;
        }
    }
    const auto requested = std::chrono::steady_clock::now();
    const auto generation = async_->generation.load();
    const auto stopGeneration = async_->stopGeneration.load();
    enqueue([this, name, frame, volume, requested, generation, stopGeneration, transient] {
        if (stopGeneration == async_->stopGeneration && (!transient || (generation == async_->generation &&
            std::chrono::steady_clock::now() - requested < std::chrono::milliseconds(250))))
            playReady(name, frame, volume);
    });
}
void SoundBank::setVolumes(int sound, int music) {
    {
        std::lock_guard lock(async_->mutex);
        if (async_->sound == sound && async_->music == music) return;
        async_->sound = sound;
        async_->music = music;
        ++async_->controlsRevision;
        async_->controlsPending = true;
    }
    async_->wake.notify_one();
}
void SoundBank::syncEmitters(std::span<const SoundEmitter> live, uint64_t frame) {
    {
        std::lock_guard lock(async_->mutex);
        async_->live.assign(live.begin(), live.end());
        async_->frame = frame;
        async_->syncPending = true;
    }
    async_->wake.notify_one();
}
void SoundBank::pauseEmitters(bool paused) {
    {
        std::lock_guard lock(async_->mutex);
        if (async_->paused == paused) return;
        async_->paused = paused;
        ++async_->controlsRevision;
        async_->controlsPending = true;
    }
    async_->wake.notify_one();
}
void SoundBank::resetEmitters() {
    ++async_->generation;
    {
        std::lock_guard lock(async_->mutex);
        async_->live.clear();
        async_->syncPending = false;
        async_->resetPending = true;
    }
    async_->wake.notify_one();
}
std::vector<std::string> SoundBank::takeFailures() {
    std::lock_guard lock(async_->mutex);
    return std::exchange(async_->failures, {});
}
} // namespace d2x
