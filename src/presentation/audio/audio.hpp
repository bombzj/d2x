#pragma once
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include "core/id.hpp"
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
        float volume = 1;
    };
    struct OriginalSoundGroup {
        std::vector<OriginalSoundVariant> sounds;
        unsigned compound = 0;
        std::optional<uint64_t> lastStart;
    };
    std::map<std::string, OriginalSoundGroup> originalGroups_;
    uint64_t groupRandom_ = 0;
    bool enabled = false;

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
};
} // namespace d2x
