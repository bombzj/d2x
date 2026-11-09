#pragma once
#include <optional>
#include <span>
#include <cstdint>
#include <vector>
namespace d2x {
struct CastAnimationTiming { int frames{}, speed{}, actionFrame{}; std::vector<uint8_t> events; };
struct CastTiming { int duration{}, impact{}, speed{}; };
std::optional<CastAnimationTiming> prepareCastAnimationTiming(int frames, int speed, std::span<const uint8_t> events);
struct PlayerCastSequence {
    std::span<const int> frames;
    int releaseFrame{}, holdFrame{-1}, fixedSpeed{};
};
// Native player sequence steps; the client supplies art, the server supplies clocks.
std::optional<PlayerCastSequence> playerCastSequence(int sequence);
CastTiming playerCastSequenceTiming(const PlayerCastSequence &, int speed);
// SC duration is client completion / ordinary cast reentry, not the later
// server ENDANIM cleanup from playerActionTimeline. Keep both consumers on
// this MPQ/FCR-derived boundary so held casts cannot accumulate a tick of lag.
CastTiming sorceressCastTiming(const CastAnimationTiming &, int fasterCast, bool arc, bool inferno);
CastTiming normalCastTiming(const CastAnimationTiming &, int fasterCast);
}
