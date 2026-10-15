#pragma once
#include "gameplay/combat/reception.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "server/runtime/contracts.hpp"
#include <functional>
#include <set>

namespace d2x::server::effects {
using AbsorptionFormula=std::function<int32_t(int,int,std::string_view)>;
// Mutates a private recipient candidate. Resource observers see each native
// write/withdrawal before the following registration is evaluated.
DomainResult<> applyDamageAbsorption(CombatEffectSet &,std::array<int64_t,6> &,DamageReception &,
    const StatRuleCatalog &,const SkillFormulaCatalog &,uint64_t,const AbsorptionFormula &,
    const std::function<void()> &,const std::function<int32_t()> &,const std::function<void(int32_t)> &,std::set<int> &);
}
