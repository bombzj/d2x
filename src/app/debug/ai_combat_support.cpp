#include "ai_impl.hpp"
#include "ai_combat_observation.hpp"

namespace d2x {
using debug_ai::position;
using debug_ai::point;
std::optional<uint16_t> DebugAi::Impl::battleAura(const Context &c) const {
    if(policy.aura) return policy.aura;
    for(const auto name:{"Fanaticism","Concentration","Might"}) {
        const auto skill=definitions->skill(name);if(knownSkill(c,skill)) return skill;
    }
    return {};
}
bool DebugAi::Impl::maintainArmor(Context &c) {
    if (Clock::now() < battle.nextArmorAttempt) return false;
    const auto &view = c.session.read();
    if (!view.load.playerUnitId || view.world.npcRequested || view.world.waypointSource) return false;
    std::optional<uint16_t> selected;
    for (const auto name : {"Shiver Armor", "Chilling Armor", "Frozen Armor"}) {
        const auto skill = definitions->skill(name);
        const auto row = definitions->skillRows.find(skill);
        if (row == definitions->skillRows.end()) continue;
        const auto stateName = definitions->skills.value(row->second, "aurastate");
        for (size_t state = 0; state < definitions->states.rows().size(); ++state)
            if (definitions->states.value(state, "state") == stateName) {
                const auto id = definitions->states.number(state, "Id");
                if (id && *id >= 0 && *id <= UINT8_MAX && recovering(c, uint8_t(*id))) return false;
            }
        if (!selected && knownSkill(c, skill) && !stateName.empty() && c.supplies && c.supplies->skillUsable &&
            c.supplies->skillUsable(skill).value_or(false)) selected = skill;
    }
    if (!selected) return false;
    if (!selectSkill(c, *selected, OnlineSkillHand::Right)) {
        wait("armor-select", "Selecting the available defensive armor skill");
        return true;
    }
    releaseCombat(c); cancelWalk(c);
    OnlineCombatCommand cast; cast.action = OnlineCombatCommand::Action::Cast;
    cast.hand = OnlineSkillHand::Right; cast.stationary = true;
    cast.point = OnlinePoint{uint16_t(c.position.x), uint16_t(c.position.y)};
    if (submitCombat(c, cast, DebugActionOwner::Combat, "armor-cast", c.position)) {
        battle.nextArmorAttempt = Clock::now() + std::chrono::seconds(3);
        ++castRequests;
        note("armor-cast", "Waiting for the normal server state assignment");
    }
    return true;
}
DebugAi::Impl::StaticFieldOpportunity DebugAi::Impl::staticFieldOpportunity(const Context &c,bool boss) const {
    const auto &view=c.session.read();
    unsigned staticRank=0;
    for(const auto &entry:c.combat.skills()) if(entry.id==definitions->staticFieldSkill) {
        staticRank=entry.level;
    }
    const auto staticRange=definitions->staticRadius(staticRank);
    const int staticFloor=!view.load.expansion?0:
        view.load.difficulty && *view.load.difficulty<definitions->difficulties.rows().size()?
        definitions->difficulties.number(*view.load.difficulty,"StaticFieldMin").value_or(-1):-1;
    const bool staticFloorKnown=staticFloor>=0 && staticFloor<=100;
    return {boss,staticRank,staticRange,staticFloor,staticFloorKnown};
}
bool DebugAi::Impl::cooldownAttack(Context &c,const OnlineUnit &chosen,const AttackStrategy &attack,
        const StaticFieldOpportunity &opportunity,bool ray,bool planningDefense,const RouteCombatTask *task,Clock::time_point now) {
    const auto &view=c.session.read();auto &run=battle.combatRun;
    const auto skill=attack.skill;const auto aura=attack.aura;
    const auto boss=opportunity.boss;const auto staticRange=opportunity.range;
    const auto staticFloorKnown=opportunity.floorKnown;const auto staticFloor=opportunity.floor;
    const Vec targetPosition=position(*chosen.position);
    // Static Field is a special cooldown filler, never a replacement in the
    // departure attack list. SrvDo020 permits it during a delayed spell's
    // shared cooldown. Retain nextCast for that primary spell unchanged.
    const bool staticWindow=!navigation.passageDefending && !task && policy.staticField && boss && run.strategyCasts && run.waitingForDelay &&
        skill!=definitions->staticFieldSkill && !aura &&
        staticRange && *staticRange>2 && staticFloorKnown && run.lastLife &&
        int(*run.lastLife)*100>staticFloor*128 &&
        ray && (targetPosition-c.position).length()<=*staticRange-2 &&
        knownSkill(c,definitions->staticFieldSkill) &&
        !c.combat.skillDelayActive(definitions->staticFieldSkill);
    if(staticWindow && (!c.supplies || !c.supplies->castInputBusy.value_or(false)) &&
        c.supplies && c.supplies->skillUsable && c.supplies->skillUsable(definitions->staticFieldSkill).value_or(false)) {
        if(view.world.combatRequest && view.world.combatRequest->state==OnlineCombatRequest::State::Pending) return true;
        if(now-battle.lastAttack<debugInputRetryInterval) return true;
        releaseCombat(c);cancelWalk(c);run.kitePoint.reset();run.kiteAfterCast=false;
        const auto hand=knownSkill(c,definitions->staticFieldSkill,true)?OnlineSkillHand::Left:OnlineSkillHand::Right;
        battle.combatDiagnostics["skill"]=definitions->staticFieldSkill;battle.combatDiagnostics["status"]="static-field-cooldown-fill";
        if(!selectSkill(c,definitions->staticFieldSkill,hand)) {
            wait("static-field-select","Selecting Static Field during the primary spell's cooldown");return true;
        }
        OnlineCombatCommand cast;cast.action=OnlineCombatCommand::Action::Cast;
        cast.hand=hand;cast.stationary=true;cast.point=point(c.position);
        if(submitCombat(c,cast,DebugActionOwner::Combat,"static-field-cast",c.position)) {
            battle.combatOwned=true;++run.casts;++run.staticCasts;++castRequests;battle.lastAttack=now;
            if(planningDefense) navigation.mobilityHold=true;
            note("static-field-between-primary",std::to_string(chosen.key.id));
            wait("static-field-cast","Static Field submitted; primary spell keeps its own cooldown and priority");
        }
        return true;
    }
    return false;
}
} // namespace d2x
