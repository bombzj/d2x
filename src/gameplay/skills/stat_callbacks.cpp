#include "stat_callbacks.hpp"
#include "gameplay/items/skill_sources.hpp"
#include "gameplay/skills/aura_resolve.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x {
namespace {
bool applyItemAuraTimerInstruction(std::vector<ItemAuraTimer> &timers,const ItemAuraCallbackInstruction &instruction,uint64_t order=0) {
    if(instruction.kind==ItemAuraCallbackKind::CancelTimers) {
        std::erase_if(timers,[&](const auto &timer){return timer.source==instruction.change.source;});
        return true;
    }
    if(instruction.kind==ItemAuraCallbackKind::ScheduleTimer) {
        timers.push_back({instruction.change.source,instruction.change.skill,instruction.change.state,instruction.due,order,instruction.continuous,instruction.change.equippedSkill});
        return true;
    }
    return false;
}
}
std::vector<ItemAuraCallbackInstruction> itemAuraCallbackInstructions(const SkillAuraChange &change,uint64_t tick) {
    // ItemMode activation/deactivation are synchronous programs. They are not
    // messages for the next effects tick; the owner executes each boundary.
    if(!change.rank) return {{ItemAuraCallbackKind::RemoveState,change},{ItemAuraCallbackKind::CancelTimers,change}};
    std::vector<ItemAuraCallbackInstruction> result{{ItemAuraCallbackKind::CancelTimers,change},
        {ItemAuraCallbackKind::ScheduleTimer,change,skillPeriodicTick(tick,change.period),skillPeriodicFrame(nativeNarrow(uint32_t(tick)),change.period)==-1}};
    if(change.immediate) result.push_back({ItemAuraCallbackKind::ExecuteImmediate,change});
    return result;
}
void executeItemAuraCallback(std::vector<ItemAuraTimer> &timers,const SkillAuraChange &change,uint64_t tick,const ItemAuraCandidateHooks &hooks) {
    if(!change.rank && !hooks.removeState) throw std::invalid_argument("Missing synchronous item aura state withdrawal");
    if(change.rank && change.immediate && !hooks.executeImmediate)
        throw std::invalid_argument("Missing synchronous immediate item aura program");
    // Own the instructions before invoking hooks: nested source writes must
    // not invalidate this callback or replace its captured formula inputs.
    const auto program=itemAuraCallbackInstructions(change,tick);
    for(const auto &instruction:program) {
        const auto order=instruction.kind==ItemAuraCallbackKind::ScheduleTimer && hooks.reserveEventOrder?hooks.reserveEventOrder():0;
        if(applyItemAuraTimerInstruction(timers,instruction,order)) continue;
        if(instruction.kind==ItemAuraCallbackKind::RemoveState) hooks.removeState(instruction.change);
        else if(instruction.kind==ItemAuraCallbackKind::ExecuteImmediate) hooks.executeImmediate(instruction.change);
    }
}
void refreshItemAuraTimers(std::vector<ItemAuraTimer> &timers,std::span<const SkillAuraChange> changes,uint64_t tick) {
    // This projection owns only registration. State/program boundaries are
    // executed by executeItemAuraCallback on the full mutation candidate.
    for(const auto &change:changes) for(const auto &instruction:itemAuraCallbackInstructions(change,tick))
        applyItemAuraTimerInstruction(timers,instruction);
}
int32_t itemAuraTimerRank(const StatRuleCatalog &rules,const StatSnapshot &values,const ItemAuraTimer &timer) {
    if(timer.skill<0 || timer.skill>UINT16_MAX) throw std::invalid_argument("Invalid native item aura skill layer");
    return nativeStatValue(values,rules,timer.equippedSkill?"item_skillonequip":"item_aura",uint16_t(timer.skill));
}
void refreshItemStatEvents(std::vector<ItemStatEvent> &events,const StatRuleCatalog &rules,StatKey key,int32_t value) {
    const auto &bindings=rules.definitions.at(key.id).itemEvents;
    if(bindings[0].event<=0) return;
    if(!value) {std::erase_if(events,[&](const auto &event){return event.key==key;});return;}
    if(std::any_of(events.begin(),events.end(),[&](const auto &event){return event.key==key;})) return;
    for(const auto binding:bindings) {
        // Retail has non-null slots 1..31; the PD2 table extends through 33.
        // Registration ignores slots outside the active callback table.
        if(binding.event>0 && binding.function>0 && binding.function<=(rules.pd2?33:31))
            events.insert(events.begin(),ItemStatEvent{key,binding});
    }
}
SkillStatRefresh skillStatRefresh(const StatRuleCatalog &rules,const SkillFormulaCatalog &formulas,StatKey key,SkillStatOwner owner) {
    const auto &name=rules.definitions.at(key.id).name;
    if(name=="item_nonclassskill" || name=="item_singleskill") {
        if(!key.layer) return {};
        const auto &skill=formulas.skills.at(key.layer);
        if(name=="item_singleskill" && (owner.type!=StatOwnerKind::Player || skill.classId!=owner.playerClass)) return {};
        return {SkillStatRefreshKind::Skill,key.layer};
    }
    if(name=="item_addclassskills" || name=="item_addskill_tab") {
        const int required=name=="item_addskill_tab"?key.layer>>3:key.layer;
        if((owner.type==StatOwnerKind::Player && owner.playerClass==required) || owner.hireling ||
            (owner.convertedPlayerClass && *owner.convertedPlayerClass==required)) return {SkillStatRefreshKind::Passives,0};
        return {};
    }
    if(name=="item_allskills" || name=="item_elemskill") return {SkillStatRefreshKind::Passives,0};
    if(name=="item_charged_skill") return {SkillStatRefreshKind::Charges,decodeItemSkillLayer(key.layer).skill};
    if(name=="item_aura") return {SkillStatRefreshKind::Aura,key.layer};
    if(rules.pd2 && name=="item_skillonequip") return {SkillStatRefreshKind::Aura,key.layer};
    return {};
}
}
