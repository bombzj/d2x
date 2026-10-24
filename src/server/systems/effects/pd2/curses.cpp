#include "../system.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/player_store.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>

namespace d2x::server::effects {
bool System::configurePd2Curse(CombatEffectSpec &spec,EntityId target,uint64_t tick) const {
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,spec.source.entity);
    const auto *programs=caster?caster->programs():nullptr;
    if(!programs || !programs->pd2) return true;
    const auto source=unitStatValues(spec.source.entity,tick),recipient=unitStatValues(target,tick);
    if(!source || !recipient) return false;
    const auto &rules=*programs->stats;
    const auto allowance=nativeAdd(logicalStatValue(*source,rules,"max_curses"),1);
    auto effectiveness=logicalStatValue(*recipient,rules,"curse_effectiveness");
    if(ports_.players.findActor(target)) effectiveness=std::min(effectiveness,75);
    if(!spec.state.curse) {
        const auto battleCry=programs->formulas->skillIds.find("battle cry");
        if(battleCry==programs->formulas->skillIds.end() ||
            programs->formulas->skills.at(battleCry->second).auraTargetState!=spec.state.id) effectiveness=0;
    }
    spec.pd2CurseSlots=Pd2CurseSlots{allowance,{rules.names.at("curse_slots"),0},effectiveness};
    return true;
}
}
