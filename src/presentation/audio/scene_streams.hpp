#pragma once
#include "audio_settings.hpp"
#include <memory>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace d2x {
class Archives;
class SoundCatalog;
struct ClassicData;
// Optional original music/voice archives. Owns decoder memory for as long as
// raylib's streaming decoder borrows it; no device handles enter content/state.
class SceneStreams {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    SceneStreams(Archives &, const ClassicData &, const SoundCatalog &);
    ~SceneStreams();
    void settings(const AudioSettings &);
    void update(int level, float dt);
    void speak(std::string_view text, std::optional<uint32_t> stringId = {});
    void stopSpeech();
    bool speechFinished() const;
};
} // namespace d2x
