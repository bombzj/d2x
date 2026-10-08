#pragma once
#include "contracts/online_world.hpp"
#include <map>
#include <vector>

namespace d2x {
// D2Client UMod callbacks consume replica events only. Timers are animation
// frames, independent of the server's MONUMOD event clock and damage authority.
class RemoteMonsterEffects {
public:
    struct Definition { bool unique{}, lightning{}, cold{}; };
    struct Release { OnlineUnitKey owner; int missile{}; };
    void clear() { actors_.clear(); releases_.clear(); }
    void observe(const OnlineUnit &, Definition, const OnlineCombatEvent &, float now, float age, float fps);
    std::vector<Release> advance(const OnlineWorldView &, float now);
private:
    struct Actor {uint64_t assignment{};uint8_t mode{1};bool lightningReady{};float lightning{-1},cold{-1};};
    std::map<OnlineUnitKey,Actor> actors_;
    std::vector<Release> releases_;
};
}
