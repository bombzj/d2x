#include "../system.hpp"
#include "../attribute_projection.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "gameplay/rules/pd2/arithmetic.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>

namespace d2x::server::effects {
void System::pd2RathmaReception(UnitReceptionPlan &plan) const {
    const auto other=ports_.monsters.pd2RathmaPartner(plan.target);
    const auto *partner=ports_.monsters.find(other);
    if(!partner || partner->nativeMode==0 || partner->nativeMode==12) return;
    auto &damage=plan.damage;int32_t mirrored=0;damage.dealt=0;
    const auto split=[&](int64_t &value) {
        value=pd2MultiplyDivide(nativeChecked(value),50.,100);
        mirrored=nativeAdd(mirrored,nativeChecked(value));
    };
    // 303010 tests the native damage descriptor's +20 flag: the six
    // damage channels AND life/mana/stamina drain, excluding all lengths.
    for(auto &channel:damage.channels) {split(channel.dealt);damage.dealt=nativeAdd(nativeChecked(damage.dealt),nativeChecked(channel.dealt));}
    for(auto *drain:{&damage.drain.life,&damage.drain.mana,&damage.drain.stamina}) split(*drain);
    if(damage.drain.shieldEligible) damage.dealt=nativeAdd(nativeChecked(damage.dealt),nativeChecked(damage.drain.life));
    if(unitStates(other,plan.tick).contains(8)) return;
    UnitEffect state;if(const auto found=units_.find(other);found!=units_.end()) state=found->second;state.area=partner->area;
    attributes::MonsterResourceTrace resources(*partner,plan.tick,&state.states);
    resources.seedLife(Fixed8::fromRaw(std::max(0,nativeSubtract(nativeChecked(partner->life),mirrored))));
    if(!plan.publication) plan.publication=std::make_shared<AuraUnitPublication>();
    // This is a direct HP write. It deliberately has no second resistance,
    // contact, hit-recovery, trigger, death action or experience callback.
    auto life=resources.take();if(!life.life.raw()) {life.modeAlive=true;life.changed=true;}
    prepareUnitEffects(*plan.publication,other,state,std::move(life),plan.tick);
}
}
