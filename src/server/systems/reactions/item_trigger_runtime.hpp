#pragma once
#include "item_triggers.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/skills/item_trigger_policy.hpp"
#include "gameplay/stats/stat_list.hpp"
#include "world/collision.hpp"
#include <optional>
#include <memory>

namespace d2x::server::actions {struct SkillRelease;}

namespace d2x::server::reactions {
enum class ItemTriggerPhase { ResolveTarget, Execute, Notify };
struct ItemTrigger {
    ActorContext actor;
    ItemSkillEvent event;
    SkillCastSpec skill;
    MissileCollisionRule collision;
    EntityId target;
    Vec position;
    int chance{};
    ItemTriggerPolicy policy;
    bool supported{};
    ItemTriggerPhase phase{ItemTriggerPhase::ResolveTarget};
    uint8_t targetType{1};
    std::optional<bool> rolled;
    std::optional<StatKey> healStat;
    std::optional<ActorContext> effectActor;
    std::shared_ptr<actions::SkillRelease> continuation;
};
}
