#pragma once
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include "core/entity_id.hpp"
#include "core/math.hpp"
#include "audio_settings.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <functional>
#include <stop_token>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <raylib.h>
namespace d2x {
struct SoundEmitter {
    EntityId entity;
    std::string key;
  Vec position;
};
struct SoundSource {
  EntityId entity;
  Vec position;
};
class SoundBank {
    struct EmitterAudio;
    std::shared_ptr<EmitterAudio> emitters_;
    uint64_t groupRandom_ = 0;
    bool enabled = false;
    int volume_ = 100;
    int musicVolume_ = 50;
    struct Async;
    std::unique_ptr<Async> async_;
    void enqueue(std::function<void()> command);
    void registerGroup(Archives &, std::string, const DataTable *, size_t, bool, std::stop_token, std::string_view = {});
    void stopAllReady();
    void playReady(const std::string &, uint64_t, float, EntityId = {});
    void syncSourcesReady(std::span<const SoundSource>, Vec);
    void volumesReady(int, int);
    void syncReady(std::span<const SoundEmitter>, uint64_t);
    void pauseReady(bool);
    void resetReady();

  public:
    explicit SoundBank(Archives &);
    ~SoundBank();
    SoundBank(const SoundBank &) = delete;
    SoundBank &operator=(const SoundBank &) = delete;
    void play(const std::string &name, uint64_t frame = 0, float volume = -1, bool transient = false, EntityId source = {});
    void syncSources(std::span<const SoundSource> sources, Vec listener);
    void registerTravelGroup(Archives &archives, std::string key, const DataTable &sounds, size_t row);
    void registerOriginalGroup(Archives &archives, std::string key, const DataTable &table, size_t row);
    void registerFile(std::string path);
    void stopAll();
    bool hasEmitterSound(const std::string &key) const;
    void syncEmitters(std::span<const SoundEmitter> emitters, uint64_t frame);
    void pauseEmitters(bool paused);
    void resetEmitters();
    void setVolumes(int sound, int music);
    std::vector<std::string> takeFailures();
};
} // namespace d2x
