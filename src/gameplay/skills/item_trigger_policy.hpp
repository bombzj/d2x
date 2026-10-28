#pragma once
#include "gameplay/items/skill_sources.hpp"

namespace d2x {
struct ItemTriggerRule {
    int effect{}, target{};
    bool targetDo{}, checkStart{}, alternativeCompatible{}, pd2{}, preparedStart{};
};
enum class ItemTriggerSupport { Ready, NoEffect, AlternativeProgram, TargetProgram };
enum class ItemTriggerTarget { EventTarget, Self, RandomPosition, RandomCorpse, LastAttacker };
struct ItemTriggerPolicy {
    ItemTriggerSupport support{ItemTriggerSupport::NoEffect};
    ItemTriggerTarget target{ItemTriggerTarget::EventTarget};
    bool alternativeProgram{}, death{};
    bool victimCasts{};
};
ItemTriggerPolicy resolveItemTriggerPolicy(ItemSkillEvent event, ItemTriggerRule rule);
}
