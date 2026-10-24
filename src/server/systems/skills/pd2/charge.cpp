#include "charge.hpp"
#include "execution.hpp"
#include "../system.hpp"
#include "server/systems/actions/release.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/effects/skill_state.hpp"
#include "server/player_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>

namespace d2x::server::skills {
DomainStatus pd2ChargeBeforeHit(const Ports &ports,actions::SkillRelease &pending,const ActorContext &actor) {
    if(!pending.pd2Execution) return DomainStatus::Unavailable;
    auto &execution=*pending.pd2Execution;
    if(execution.chargeState) return DomainStatus::Applied;
    const auto *owner=ports.players.find(actor.player);if(!owner) return DomainStatus::InvalidActor;
    // St72 saves UNITS_GetDistanceToCoordinates in the used skill's Param3.
    // Do180 consumes it once, before DrainCombat applies the contact/splash.
    effects::NativeSkillStateValues values;values.state=224;
    values.stats[0]={owner->rules.skills->stats->names.at("inc_splash_radius"),
        nativeMultiply(nativeDivide(execution.chargeDistance,4),20)};
    int duration=3;
    if(const auto *states=ports.effects.unitEffects(actor.actor)) for(const auto &entry:states->entries())
        if(entry.spec.state.id==224 && entry.expiresAt)
            duration=*entry.expiresAt>actor.tick?int(std::min<uint64_t>(*entry.expiresAt-actor.tick,INT32_MAX)):0;
    const auto posted=ports.effects.skillAuraState(actor.actor,actor.actor,pending.skill.sourceId,pending.skill.rank,duration,
        actor.tick,effects::NativeSkillStateProcedure::Warcry,&values);
    if(isDeferred(posted.status)) return posted.status;
    execution.chargeState=true;return DomainStatus::Applied;
}
}
