#pragma once
#include <vector>
#include "server/systems/attributes/calculation.hpp"
#include "server/runtime/domain_fact.hpp"

namespace d2x::server {struct SkillRules;}
namespace d2x::server::effects {
void applyPd2EquippedBuff(CombatEffectSet &,const ActorContext &,const SkillAuraChange &,const SkillRules &,
    const CombatStateRules &,const attributes::AuraSourceObserver &,std::vector<DomainFact> &,bool monster=false);
}
