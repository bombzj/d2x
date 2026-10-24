#include "requirements.hpp"
#include "../evaluation.hpp"
#include "server/player_store.hpp"
#include "gameplay/skills/pd2_program.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x::server::skills {
bool pd2StreamSkill(const SkillCastSpec &skill) {
    return skill.pd2 && skill.pd2->startFunction==11 && skill.pd2->doFunction==182;
}
int32_t pd2BloodWarpCost(const PlayerState &owner,const SkillCastSpec &skill) {
    auto input=formulaInput(owner);SkillFormulaValues values(*owner.rules.skills->formulas,input);
    const auto percent=values.calculation(FormulaDomain::Skill,skill.sourceId,skill.rank,"calc1");
    const auto minimum=values.calculation(FormulaDomain::Skill,skill.sourceId,skill.rank,"calc2");
    if(!percent || !minimum) throw std::runtime_error("Missing PD2 Blood Warp cost formula");
    // Current core St66 0x3B3FD0 / Do157 0x3AF610 treats the percentage as
    // unsigned, then CVTTSD2SI truncates (and yields INT_MIN on overflow).
    const auto scaled=(double(uint32_t(*percent))/100.0)*double(owner.persistent.player.hp.raw());
    const auto cost=scaled<double(INT32_MIN) || scaled>=2147483648.0?INT32_MIN:int32_t(std::trunc(scaled));
    return std::max(cost,*minimum);
}
bool pd2SkillStartAllowed(const PlayerState &owner,const SkillCastSpec &skill) {
    if(!skill.pd2) return true;
    if(!pd2SkillEquipmentAllowed(owner,skill)) return false;
    if(skill.pd2->startFunction==66) return pd2BloodWarpCost(owner,skill)<owner.persistent.player.hp.raw();
    return true;
}
bool pd2SkillEquipmentAllowed(const PlayerState &owner,const SkillCastSpec &skill) {
    if(!skill.pd2 || skill.pd2->requiredWeaponType.empty()) return true;
    // Native St73 (RVA 0x3B4500) checks the main weapon's actual item types.
    for(const auto &weapon:owner.totals.equipment.weapons) if(!weapon.leftHand && weapon.item &&
        std::find(weapon.types.begin(),weapon.types.end(),skill.pd2->requiredWeaponType)!=weapon.types.end()) return true;
    return false;
}
}
