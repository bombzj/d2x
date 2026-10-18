#pragma once
#include "content/character/actor_appearance.hpp"
#include "contracts/character.hpp"
#include "content/world/object_mode.hpp"
#include "core/math.hpp"
#include "presentation/graphics/graphics.hpp"
#include "resources/anim_data.hpp"
#include "resources/data_table.hpp"
#include <cstdint>
#include <map>
#include <optional>

namespace d2x {
struct ActorAnimationRequest {
    std::string category, mode;
    bool sequenceUsesAttackRate{};
    bool skillUsesAttackRate{};
    bool monsterUnit{}; // Includes monsters using player graphics.
    bool monsterKick{}; // Native transformed Assassin maps MONMODE_A1 to KK.
    ActorAppearance appearance;
    std::optional<ActorAppearance> shapeSource; // Original player COF for native transformed attack speed.
    int paletteTransform = -1;
    bool randomTransform = false, finalFrame = false, shadow = false;
    bool nativeMonsterTransform = false; // Raw palshift row, including alliance rows 0/1.
    int playerSequence = -1;
    std::optional<int> fasterCast;
    std::optional<KnownAttackTiming> attackTiming;
    std::optional<KnownRecoveryTiming> recoveryTiming;
    int repeatCount{1},rollbackPercent{};
    int sequenceRateBonus{};
    bool sequenceLanding{};
    std::optional<int> animationRate;
    bool persistentCycle{};
    int sequenceStart{},sequenceEnd{-1};
    int animationHoldFrame{-1};
    int stateColorShift{-1};
};
struct ActorAnimation {
    GpuAnimation animation;
    float fps = 0, releaseTime = -1;
    std::vector<float> releaseTimes;
    int start = 0, frameCount = 0, holdFrame = -1;
    std::optional<int> durationFrames;
    bool cycle = false, shadow = false;
    Vec offset;
    int order = 0;
    bool ready() const;
    const Sprite *sample(float clock, float elapsed, Vec look) const;
    const Sprite *sampleFacing(float clock, float elapsed, int facing) const;
    std::optional<Vec> riderAnchor(float clock,float elapsed,Vec look) const;
    bool finished(float elapsed) const;
    float duration() const;
};
// A presentation clock only: a completed animation never changes server mode or life.
struct ActorAnimationState {
    std::optional<uint8_t> mode;
    uint64_t revision = 0;
    float startedAt = 0;
    std::optional<float> frozenAt;
    void observe(std::optional<uint8_t>, uint64_t actionRevision, float clock,
                 float receivedAge, bool frozen);
    float clock(float now) const { return frozenAt.value_or(now); }
    float elapsed(float now) const;
};
// One cache for world characters, monsters, NPCs and objects. Graphics still owns
// every texture; requests contain resolved appearance values rather than units.
class ActorAnimationCatalog {
    Archives &archives_;
    AnimDataTable timing_;
    DataTable objects_, sequences_;
    std::map<int, size_t> objectRows_;
    std::map<std::string, std::vector<size_t>, std::less<>> sequenceRows_;
    std::map<std::string, ActorAnimation, std::less<>> animations_;
    ActorAnimation composite(Graphics &,int palette,const ActorAnimationRequest &);
    ActorAnimation sequence(Graphics &, int palette, const ActorAnimationRequest &);
  public:
    explicit ActorAnimationCatalog(Archives &);
    const ActorAnimation *resolve(Graphics &, int palette, const ActorAnimationRequest &);
    const ActorAnimation *object(Graphics &, int palette, int identity, int mode);
    ObjectPresentation objectPresentation(int identity, int serverMode, float elapsed) const;
    std::string sequenceMode(std::string_view name) const;
};
} // namespace d2x
