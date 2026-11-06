#pragma once
#include "content/audio/sound_catalog.hpp"
#include "sound_events.hpp"
#include "audio.hpp"
#include <deque>
#include <set>
#include <span>

namespace d2x {
class SceneStreams;
struct OnlineQuestState;
// Sole presentation sound consumer. It owns presentation-only randomness and clocks.
class SceneAudio {
    Archives &archives_;
    const SoundCatalog &catalog_;
    SoundBank &bank_;
    SceneStreams &streams_;
    struct Pending { SoundRule rule; EntityId source; uint64_t actionRevision{}; float due{}; bool transient = true; };
    struct Cadence { bool moving{}, neutral{}; float nextStep{}, nextNeutral{}, cycle{}; };
    std::deque<Pending> pending_;
    std::map<EntityId, Cadence> cadence_;
    std::set<std::string, std::less<>> registered_, limitations_;
    std::set<std::string, std::less<>> unavailable_;
    std::map<std::string, std::string, std::less<>> aliases_;
    uint64_t random_ = 0x1234;
    uint64_t greetingRandom_;
    float clock_ = -1;
    uint64_t narrativeGame_{UINT64_MAX};
    std::set<int> enteredNarrativeGroups_;
    int narrativeLevel_{-1};
    double lastNarrativeAt_{};
    void enqueue(const SoundRule &, const SoundActorView &, float age = 0, float extraDelay = 0, bool transient = true);
  public:
    SceneAudio(Archives &, const SoundCatalog &, SoundBank &,SceneStreams &);
    bool registerSound(std::string_view sound, std::string key, bool travel = false);
    bool play(std::string_view sound, uint64_t frame, float volume = -1, bool transient = false);
    bool playNativeSound(int row,uint64_t frame);
    bool npcReturnGreeting(int identity);
    bool npcGreeting(int identity, int mode, int period);
    void playerNarrative(int identity, int event);
    void enterLevel(uint64_t game, int identity, int level, const OnlineQuestState &);
    bool itemDrop(const ItemDropSoundEvent &); // True once consumed, including stale/inaudible events.
    void itemPlacement(std::string_view code, ItemQuality quality, int specialRow);
    void itemUse(std::string_view code, ItemQuality quality, int specialRow);
    void playerFootstep(int identity, bool running, int material, int level, uint64_t frame);
    void playRegistered(const std::string &key, uint64_t frame = UINT64_MAX);
    void update(float clock, std::span<const SoundActorView>, std::span<const PresentationSoundEvent>, Vec listener);
    void reset();
    const std::set<std::string, std::less<>> &limitations() const;
};
} // namespace d2x
