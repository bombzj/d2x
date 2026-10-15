#include "frame_event_dispatch.hpp"
#include "game_systems.hpp"

namespace d2x::server {
DomainResult<> dispatchFrameEvent(GameSystems &systems,const FrameEvent &event,TickContext tick) {
    if(event.handler==effects::conversionTimerHandler && event.owner.type==FrameEventUnit::Monster && event.type==FrameEventType::RemoveState)
        return systems.effects.endConversion(event.owner.unit,tick.tick);
    if(event.handler==effects::nativeUnitSkillTimerHandler && event.owner.type==FrameEventUnit::Monster && event.type==FrameEventType::PeriodicSkills)
        return systems.effects.frameEvent(event,tick.tick);
    if(!event.handler && event.type==FrameEventType::ActiveState &&
       (event.owner.type==FrameEventUnit::Player || event.owner.type==FrameEventUnit::Monster)) return systems.skills.activeStateEvent(event,tick.tick);
    if(event.handler) return {DomainStatus::NotImplemented,{}};
    switch(event.owner.type) {
    case FrameEventUnit::Player:
        if(event.type==FrameEventType::StatRegeneration) return systems.effects.frameEvent(event,tick.tick);
        if(event.type==FrameEventType::PeriodicSkills) return systems.effects.frameEvent(event,tick.tick);
        if(event.type==FrameEventType::RemoveState) return systems.effects.frameEvent(event,tick.tick);
        if(event.type==FrameEventType::PeriodicStats) return systems.effects.frameEvent(event,tick.tick);
        break;
    case FrameEventUnit::Monster:
        if(event.type==FrameEventType::MonsterModifier) return systems.monsters.modifierEvent(event,tick.tick);
        if(event.type==FrameEventType::StatRegeneration) return systems.monsters.regenerate(event.owner.unit,tick.tick);
        if(event.type==FrameEventType::PeriodicSkills) return systems.effects.frameEvent(event,tick.tick);
        if(event.type==FrameEventType::RemoveState) return systems.effects.frameEvent(event,tick.tick);
        if(event.type==FrameEventType::PeriodicStats) return systems.effects.frameEvent(event,tick.tick);
        if(event.type==FrameEventType::AiReset) return systems.monsters.resetAiSelector(event.owner.unit,event.payload,tick.tick);
        break;
    case FrameEventUnit::Missile:
        if(event.type==FrameEventType::ModeChange) return systems.missiles.frameEvent(event.owner.unit,tick);
        break;
    case FrameEventUnit::Object:case FrameEventUnit::Item:case FrameEventUnit::Game:
        break;
    }
    return {DomainStatus::NotImplemented,{}};
}
}
