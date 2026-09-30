#pragma once
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include "core/id.hpp"
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <raylib.h>
namespace d2x {
struct SoundEmitter {
    EntityId entity;
    std::string key;
};
class SoundBank {
    struct EmitterAudio;
    std::shared_ptr<EmitterAudio> emitters_;
    std::map<std::string, Sound> sounds;
    bool enabled = false;

  public:
    bool muted = false;
    explicit SoundBank(Archives &archives);
    ~SoundBank();
    SoundBank(const SoundBank &) = delete;
    SoundBank &operator=(const SoundBank &) = delete;
    void play(const std::string &name);
    void registerOriginal(Archives &archives, std::string key, std::string_view path, float volume = .45f);
    void registerTravelGroup(Archives &archives, std::string key, const DataTable &sounds, size_t row);
    bool hasEmitterSound(const std::string &key) const;
    void syncEmitters(std::span<const SoundEmitter> emitters, uint64_t frame);
    void pauseEmitters(bool paused);
    void resetEmitters();
};
} // namespace d2x
