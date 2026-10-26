#include "map_values.hpp"
#include "../monster_resources.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/maps/pd2/definition.hpp"
#include "gameplay/monsters/pd2/boss_program.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/pd2/arithmetic.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <stdexcept>

#include <algorithm>

namespace d2x::server::attributes {
void preparePd2BossScaling(monsters::Actor &actor,const pd2::MapInstance &map,uint64_t tick,bool dead) {
    // 371BF0 -> 381160 runs on native map birth, guarded by STATE241.
    if(!pd2::scenePauseClass(actor.identity.nativeClass) || (actor.initialEffects && actor.initialEffects->hasState(241,tick))) return;
    if(!actor.rule.states || !actor.rule.states->contains(241)) throw std::runtime_error("Missing native scene scaling state");
    const auto &rules=*actor.rule.stats;const int difficulty=map.bossDifficulty;
    const int additional=std::max(0,int(map.participants)-1);const bool boss=pd2::sceneBossAbsorption(actor.identity.nativeClass);
    auto states=actor.initialEffects?std::make_shared<CombatEffectSet>(*actor.initialEffects):std::make_shared<CombatEffectSet>();
    MonsterResourceTrace resources(actor,tick,states.get());
    const uint32_t maximum=uint32_t(resources.read().maximum.raw());
    const int64_t base=difficulty<1?int64_t(maximum)*3/2:int64_t(maximum)*(boss?4:3);
    const int64_t scaled=base+int64_t(pd2MultiplyDivide(nativeNarrow(base),20.,100))*difficulty;
    resources.writeBaseStat("maxhp",scaled<0 || scaled>INT32_MAX?INT32_MAX:int32_t(scaled),{});
    CombatEffectSpec spec;spec.state=actor.rule.states->at(241);spec.nativeState=spec.nativeDirectAllocation=true;
    spec.source={CombatEffectSource::Environment,actor.id,241,0};spec.nativeValues.emplace();
    const auto write=[&](uint16_t id,int value){writeNativeBaseStat(*spec.nativeValues,rules,{id,0},value);};
    write(127,difficulty*4);
    if(boss) {
        for(const uint16_t id:{142,148,144}) write(id,difficulty*7+additional*2);
        write(146,difficulty*3+additional*2);write(36,additional*3);write(110,additional+difficulty*3);
    }
    spec.modifiers=nativeCharacterModifiers(StatSnapshot{*spec.nativeValues},rules);
    if(!states->apply(spec,tick).accepted) throw std::runtime_error("Native scene scaling state rejected");
    resources.sample(*states,true);if(!dead) resources.seedLife(resources.read().maximum);
    auto next=resources.take();actor.nativeStats=std::move(next.intrinsic);actor.life=next.life.raw();actor.maximumLife=next.maximum.raw();
    actor.initialEffects=std::move(states);
}
}
