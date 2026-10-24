#include "item_trigger_policy.hpp"

namespace d2x {
ItemTriggerPolicy resolveItemTriggerPolicy(ItemSkillEvent event, ItemTriggerRule rule) {
    const bool attack = event == ItemSkillEvent::Attack || event == ItemSkillEvent::Hit || event == ItemSkillEvent::Kill || event == ItemSkillEvent::MeleeSplash;
    ItemTriggerPolicy result;
    result.alternativeProgram = attack && !rule.targetDo;
    result.death = event == ItemSkillEvent::Death;
    if (!rule.effect) return result;
    if (result.alternativeProgram && !rule.alternativeCompatible) {
        result.support = ItemTriggerSupport::AlternativeProgram;
        return result;
    }
    if ((attack && rule.targetDo) || rule.target == 3 || rule.checkStart) {
        result.support = ItemTriggerSupport::TargetProgram;
        return result;
    }
    result.support = ItemTriggerSupport::Ready;
    if (rule.target == 1) result.target = ItemTriggerTarget::Self;
    else if (rule.target == 2) result.target = ItemTriggerTarget::RandomPosition;
    return result;
}
}
