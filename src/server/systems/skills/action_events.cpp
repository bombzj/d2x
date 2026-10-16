#include "system.hpp"
#include "runtime.hpp"
#include "weapon_melee.hpp"
#include "druid_melee.hpp"
#include "assassin_kick.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/companions/system.hpp"
#include "server/systems/effects/system.hpp"

namespace d2x::server::skills {
DomainResult<> System::actionEvent(const FrameEvent &event,uint64_t tick) {
    const auto cast=state_.casts.find(event.owner.unit);
    const auto pending=runtime_->releases.find(event.owner.unit);
    // Rewinds may publish a successor generation inside the entered callback.
    // Its retained contact still belongs to this executing event until accepted.
    const bool continuing=pending!=runtime_->releases.end() && pending->second.nativeCallbackStarted;
    if(cast==state_.casts.end() || !event.payload || (cast->second.timeline!=event.payload && !continuing)) {
        ports_.frameEvents.cancel(event.handle);
        return {DomainStatus::Applied,std::monostate{}};
    }
    if(cast->second.interrupted && !continuing) return {DomainStatus::Applied,std::monostate{}};
    if(event.type==FrameEventType::ModeChange) {
        if(event.customId==-1) {
            if(tick<event.due) return {DomainStatus::Applied,std::monostate{}};
            if(release({tick},event.owner.unit)==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
            const auto current=state_.casts.find(event.owner.unit);
            if(!runtime_->releases.contains(event.owner.unit) && (current==state_.casts.end() || current->second.until<=tick)) {
                const auto ended=ports_.effects.endMode(event.owner.unit,tick);if(!ended) return ended;
                ports_.frameEvents.cancel(event.handle);
            }
            return {DomainStatus::Applied,std::monostate{}};
        }
        // PlrModes::sub_6FC80EE0: ordinary actions execute SrvDo once, on 1/2.
        // Repeating programs retain the release and replace its generation
        // after each rewind; sequence flags in one tick remain separate calls.
        if(event.customId!=1 && event.customId!=2) return {DomainStatus::Applied,std::monostate{}};
        if(pending!=runtime_->releases.end() && pending->second.eventDriven && release({tick},event.owner.unit)==StepStatus::Blocked)
            return {DomainStatus::Capacity,{}};
    } else if(event.type==FrameEventType::EndAnimation) {
        const auto ended=ports_.effects.endMode(event.owner.unit,tick);if(!ended) return ended;
        cast->second.until=tick;
        // Animations can finish without crossing an action flag (high speed).
        if(pending!=runtime_->releases.end() && pending->second.eventDriven) {
            if(pending->second.weaponMelee && pending->second.weaponMelee->occurrence)
                ports_.combat.discardNativeMelee(event.owner.unit,*pending->second.weaponMelee->occurrence);
            if(pending->second.druidMelee && pending->second.druidMelee->occurrence)
                ports_.combat.discardNativeMelee(event.owner.unit,*pending->second.druidMelee->occurrence);
            if(pending->second.assassinKick && pending->second.assassinKick->occurrence)
                ports_.combat.discardNativeMelee(event.owner.unit,*pending->second.assassinKick->occurrence);
            ports_.companions.cancel(event.owner.unit);
            runtime_->releases.erase(pending);
        }
    }
    return {DomainStatus::Applied,std::monostate{}};
}
}
