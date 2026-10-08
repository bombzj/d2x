#pragma once
#include <optional>
#include <span>
#include <cstdint>
namespace d2x {
struct CastAnimationTiming { int frames{}, speed{}, actionFrame{}; };
struct CastTiming { int duration{}, impact{}, speed{}; };
std::optional<CastAnimationTiming> prepareCastAnimationTiming(int frames, int speed, std::span<const uint8_t> events);
struct PlayerCastSequence {
    std::span<const int> frames;
    int releaseFrame{}, holdFrame{-1}, fixedSpeed{};
};
// Native player sequence steps; the client supplies art, the server supplies clocks.
std::optional<PlayerCastSequence> playerCastSequence(int sequence);
CastTiming playerCastSequenceTiming(const PlayerCastSequence &, int speed);
// Normal SC timing extracted from the old single-player applySkillCastTiming.
CastTiming sorceressCastTiming(CastAnimationTiming, int fasterCast, bool arc, bool inferno);
CastTiming normalCastTiming(CastAnimationTiming, int fasterCast);
}
