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
    // Retail 45570's single music/voice slot. Delays are client frames at 25 Hz.
    void questCue(std::string_view music,int startFrames,int endFrames,std::string_view voice,int voiceFrames,int resumeBlock);
    std::optional<std::string> advanceQuestCue(float dt,bool playerAlive);
    void resetQuestCue();
    void speak(std::string_view text, std::optional<uint32_t> stringId = {});
    void stopSpeech();
    bool speechFinished() const;
};
} // namespace d2x
