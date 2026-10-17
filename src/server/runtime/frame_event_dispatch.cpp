#include "frame_event_dispatch.hpp"
#include "game_systems.hpp"

namespace d2x::server {
DomainResult<> dispatchFrameEvent(GameSystems &systems,const FrameEvent &event,TickContext tick) {
    // Native continuous timers discard explicit callbacks; retain the domain
    // routing identity in customId, which survives the continuous queue.
    if(event.continuous && event.customId==int32_t(movementTimerHandler) && event.owner.type==FrameEventUnit::Player && event.type==FrameEventType::ModeChange)
        return systems.movement.frameEvent(event,tick);
    if(event.handler==effects::modeEndTimerHandler && event.type==FrameEventType::EndAnimation)
        return systems.effects.modeEndEvent(event.owner.unit,tick.tick);
    if(event.handler==effects::monsterModifierTimerHandler && event.owner.type==FrameEventUnit::Monster && event.type==FrameEventType::MonsterModifier)
        return systems.effects.monsterModifierEvent(event,tick.tick);
    if(event.handler==effects::conversionTimerHandler && event.owner.type==FrameEventUnit::Monster && event.type==FrameEventType::RemoveState)
        return systems.effects.endConversion(event.owner.unit,tick.tick);
    if(event.handler==effects::nativeUnitSkillTimerHandler && event.owner.type==FrameEventUnit::Monster && event.type==FrameEventType::PeriodicSkills)
        return systems.effects.frameEvent(event,tick.tick);
    if(!event.handler && event.type==FrameEventType::ActiveState &&
       (event.owner.type==FrameEventUnit::Player || event.owner.type==FrameEventUnit::Monster)) return systems.skills.activeStateEvent(event,tick.tick);
    if(event.handler) return {DomainStatus::NotImplemented,{}};
    switch(event.owner.type) {
    case FrameEventUnit::Player:
        if(event.type==FrameEventType::ModeChange || event.type==FrameEventType::EndAnimation) return systems.skills.actionEvent(event,tick.tick);
        if(event.type==FrameEventType::StatRegeneration) return systems.effects.frameEvent(event,tick.tick);
        if(event.type==FrameEventType::PeriodicSkills) return systems.effects.frameEvent(event,tick.tick);
        if(event.type==FrameEventType::RemoveState) return systems.effects.frameEvent(event,tick.tick);
        if(event.type==FrameEventType::PeriodicStats) return systems.effects.frameEvent(event,tick.tick);
        break;
    case FrameEventUnit::Monster:
        if(event.type==FrameEventType::AiThink) {
            if(const auto *unit=systems.monsters.find(event.owner.unit);unit && unit->rule.trap!=MonsterRule::Trap::None)
                return systems.objects.trapEvent(event.owner.unit,tick.tick);
            if(systems.companions.read().companions.contains(event.owner.unit)) return systems.companions.think(event.owner.unit,tick);
            return systems.ai.think(event.owner.unit,tick)==StepStatus::Blocked?
                DomainResult<>{DomainStatus::Capacity,{}}:DomainResult<>{DomainStatus::Applied,std::monostate{}};
        }
        if(event.type==FrameEventType::ModeChange) {
            const auto moved=systems.monsters.modeEvent(event,tick.tick);if(!moved) return moved;
            return event.payload?systems.skills.monsterActionEvent(event,tick.tick):moved;
        }
        if(event.type==FrameEventType::MonsterModifier) return systems.monsters.modifierEvent(event,tick.tick,systems.spatial.nativeCollisionSample());
        if(event.type==FrameEventType::StatRegeneration) return systems.monsters.regenerate(event.owner.unit,tick.tick);
        if(event.type==FrameEventType::PeriodicSkills) return systems.effects.frameEvent(event,tick.tick);
        if(event.type==FrameEventType::RemoveState) return systems.effects.frameEvent(event,tick.tick);
        if(event.type==FrameEventType::PeriodicStats) return systems.effects.frameEvent(event,tick.tick);
        if(event.type==FrameEventType::AiReset) return systems.monsters.resetAiSelector(event.owner.unit,event.payload,tick.tick);
        break;
    case FrameEventUnit::Missile:
        if(event.type==FrameEventType::ModeChange) return systems.missiles.frameEvent(event.owner.unit,tick);
        break;
    case FrameEventUnit::Item:
        if(event.type==FrameEventType::StatRegeneration) return systems.inventory.replenishItem(event.owner.unit,tick.tick);
        break;
    case FrameEventUnit::Object:
        return systems.objects.frameEvent(event,tick.tick);
    case FrameEventUnit::Game:
        break;
    }
    return {DomainStatus::NotImplemented,{}};
}
}
