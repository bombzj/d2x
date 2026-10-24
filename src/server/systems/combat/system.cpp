#include "system.hpp"
#include "server/systems/effects/system.hpp"
namespace d2x::server::combat {
void System::cancel(EntityId source) {
    for(auto &entry:state_.spells)
        if(entry.source==source && entry.nativeDrainHeld && entry.nativeSkill) entry.nativeSkill->cancelled=true;
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    state_.targetAttempts=0;
    std::erase_if(state_.lifeNotifications,[&](const auto &entry){return entry.second.tick!=tick.tick;});
    std::erase_if(state_.pd2AvoidUntil,[&](const auto &entry){return entry.second<=tick.tick;});
    bool blocked=resolveSpells(tick)==StepStatus::Blocked;
    while(ports_.effects.hasContactEffects()) {
        if(ports_.effects.finishContacts(tick.tick)==StepStatus::Blocked) {blocked=true;break;}
        if(resolveSpells(tick)==StepStatus::Blocked) {blocked=true;break;}
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
