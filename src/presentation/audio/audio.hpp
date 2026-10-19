#pragma once
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include "core/id.hpp"
#include "audio_settings.hpp"
#include <cstdint>
#include <map>
#include <memory>
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
};
class SoundBank {
    struct EmitterAudio;
    std::shared_ptr<EmitterAudio> emitters_;
    struct OriginalSoundVariant {
        Sound sound;
        bool stopInstance = false;
        bool deferInstance = false;
        bool musicVolume = false;
        int volume = 255, activeVolume = 255;
    };
    struct OriginalSoundGroup {
        std::vector<OriginalSoundVariant> sounds;
        unsigned compound = 0;
        std::optional<uint64_t> lastStart;
    };
    std::map<std::string, OriginalSoundGroup> originalGroups_;
    uint64_t groupRandom_ = 0;
    bool enabled = false;
    int volume_ = 100;
    int musicVolume_ = 50;

  public:
    SoundBank();
    ~SoundBank();
    SoundBank(const SoundBank &) = delete;
    SoundBank &operator=(const SoundBank &) = delete;
    void play(const std::string &name, uint64_t frame = 0, float volume = -1);
    void registerTravelGroup(Archives &archives, std::string key, const DataTable &sounds, size_t row);
    void registerOriginalGroup(Archives &archives, std::string key, const DataTable &table, size_t row);
    bool hasEmitterSound(const std::string &key) const;
    void syncEmitters(std::span<const SoundEmitter> emitters, uint64_t frame);
    void pauseEmitters(bool paused);
    void resetEmitters();
    void setVolumes(int sound, int music);
};
} // namespace d2x
