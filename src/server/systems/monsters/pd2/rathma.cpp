#include "../system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/combat/poison.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>

namespace d2x::server::monsters {
void System::stagePd2RathmaStatuses(effects::AuraUnitPublication &plan,EntityId id,EntityId source,
    const std::optional<PoisonApplication> &poison,const std::optional<OpenWoundsApplication> &wound,uint64_t tick) const {
    const auto other=pd2RathmaPartner(id);const auto *partner=find(other);
    if(!partner || partner->nativeMode==0 || partner->nativeMode==12) return;
    auto candidate=*partner;effects::UnitDamageStates updates;updates.source=source;
    bool changed=false;
    // 305920 calls the plain poison-list helper on the partner even if the
    // first list rejected the weaker rate. Do not resist the duration again.
    if(poison && (poison->rate>0 || poison->frames>0) && poison->state>=0 &&
       replacesPoison(candidate.poison?candidate.poison->damage.rate:0,poison->rate)) {
        const auto until=tick+std::min(poison->frames,UINT64_MAX-tick);
        if(candidate.poison) {
            candidate.poison->damage.rate=poison->rate;candidate.poison->damage.frames=poison->frames;
            candidate.poison->until=until;candidate.poison->nativeExpireFrame.reset();candidate.poison->removalDue.reset();
        } else candidate.poison=PoisonStatus{*poison,source,until,tick+1};
        updates.poisonApplied=updates.resetRegeneration=true;changed=true;
    }
    if(wound && wound->pd2Increment && !ports_.effects.unitStates(other,tick).contains(8)) {
        OpenWoundsApplication next{*wound->pd2Increment,wound->pd2Attacker,wound->pd2Credit,wound->until,tick+1,1,{},{},{}};
        if(candidate.wound && candidate.wound->until>tick && candidate.wound->pd2Stacks) {
            next=*candidate.wound;next.until=wound->until;
            if(next.pd2Stacks<3) {next.rate=nativeAdd(nativeChecked(next.rate),nativeChecked(*wound->pd2Increment));++next.pd2Stacks;}
        }
        next.pd2Increment.reset();next.pd2Attacker=next.pd2Credit={};
        candidate.wound=std::move(next);updates.woundApplied=updates.resetRegeneration=true;changed=true;
    }
    if(changed) {++candidate.revision;ports_.effects.stageDamageStates(plan,std::move(candidate),std::move(updates),tick);}
}
}
