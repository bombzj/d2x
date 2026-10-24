#include "item_events.hpp"
#include "../item_triggers.hpp"
#include "../item_trigger_runtime.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/runtime/skill_rules.hpp"

namespace d2x::server::reactions {
void ItemTriggers::schedulePd2Block(EntityId blocker,EntityId attacker,uint64_t tick) {
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,blocker);
    if(!caster || !caster->programs() || !caster->programs()->pd2 || ports_.areas.at(caster->area()).definition.town) return;
    // Core 0x302930 schedules doblock for both shield and weapon blocks,
    // before the ordinary hit reaction. The callback samples current gear.
    auto timer=ports_.frameEvents.prepareInsertion({{}, {caster->player?FrameEventUnit::Player:FrameEventUnit::Monster,blocker},
        FrameEventType::MonsterModifier,tick+2,false,pd2ItemEventTimerHandler,15,ports_.players.findActor(attacker)?0:1,attacker.value});
    ports_.frameEvents.publish(std::move(timer));
}
DomainResult<> ItemTriggers::frameEvent(const FrameEvent &event,uint64_t tick) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    if(event.customId!=14 && event.customId!=15) return {DomainStatus::NotImplemented,{}};
    if(!scheduled_.contains(event.handle)) {
        const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,event.owner.unit);if(!caster) return done();
        EntityId target{event.payload};auto position=caster->position();
        if(event.customParameter==255) target=caster->id();
        else if(const auto *player=ports_.players.findActor(target);player && event.customParameter==0) position=player->position;
        else if(const auto *monster=ports_.monsters.find(target);monster && event.customParameter==1) position=monster->position;
        else target={};
        auto prepared=prepare(caster->context(ports_.areas,tick),{event.customId==14?ItemSkillEvent::Cast:ItemSkillEvent::Block},target,position);
        if(!prepared) return {prepared.status,{}};
        scheduled_.emplace(event.handle,std::move(*prepared.value));
    }
    auto &plan=scheduled_.at(event.handle);
    if(advance(plan,tick)==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
    scheduled_.erase(event.handle);return done();
}
}
