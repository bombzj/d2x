#pragma once
#include "content/audio/sound_catalog.hpp"
#include "sound_events.hpp"
#include "audio.hpp"
#include <deque>
#include <set>
#include <span>

namespace d2x {
// Sole presentation sound consumer. It owns presentation-only randomness and clocks.
class SceneAudio {
    Archives &archives_;
    const SoundCatalog &catalog_;
    SoundBank &bank_;
    struct Pending { SoundRule rule; EntityId source; uint64_t actionRevision{}; float due{}; };
    struct Cadence { bool moving{}, neutral{}; float nextStep{}, nextNeutral{}, cycle{}; };
    std::deque<Pending> pending_;
    std::map<EntityId, Cadence> cadence_;
    std::set<std::string, std::less<>> registered_, limitations_;
    std::set<std::string, std::less<>> unavailable_;
    std::map<std::string, std::string, std::less<>> aliases_;
    uint64_t random_ = 0x1234;
    float clock_ = -1;
    void enqueue(const SoundRule &, const SoundActorView &, float age = 0, float extraDelay = 0);
  public:
    SceneAudio(Archives &, const SoundCatalog &, SoundBank &);
    bool registerSound(std::string_view sound, std::string key, bool travel = false);
    bool play(std::string_view sound, uint64_t frame, float volume = -1);
    bool itemDrop(const ItemDropSoundEvent &); // True once consumed, including stale/inaudible events.
    void playRegistered(const std::string &key, uint64_t frame = UINT64_MAX);
    void update(float clock, std::span<const SoundActorView>, std::span<const PresentationSoundEvent>);
    void reset();
    const auto &limitations() const { return limitations_; }
};
} // namespace d2x
