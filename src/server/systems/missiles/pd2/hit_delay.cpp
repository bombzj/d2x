#include "../system.hpp"
#include "server/player_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/rules/arithmetic.hpp"

namespace d2x::server::missiles {
std::optional<std::array<int32_t,2>> System::pd2NextHitKey(const Missile &missile) const {
    if(!missile.rules || !missile.rules->pd2) return {};
    // Current core 0x3A6E10 / 0x3A6EF0. Non-player owners share one
    // gate; player-owned missiles normally separate by owner and class.
    if(!ports_.players.findActor(missile.owner)) return std::array<int32_t,2>{-1,-1};
    return std::array<int32_t,2>{missile.pd2SharedClassHitDelay?-1:nativeNarrow(missile.owner.value),missile.definition};
}
bool System::missileHitAllowed(const Missile &missile,EntityId target) const {
    if(const auto key=pd2NextHitKey(missile)) {
        if(!missile.nativeDamage || !missile.nativeDamage->rule.nextHit || !missile.nativeDamage->rule.nextDelay) return true;
        return ports_.effects.pd2MissileHitAllowed(target,*key);
    }
    return ports_.effects.missileHitAllowed(target,missile.executionTick);
}
}
