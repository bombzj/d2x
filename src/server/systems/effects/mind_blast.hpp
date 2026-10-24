#pragma once
#include "server/runtime/contracts.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/missiles/damage_data.hpp"
#include <memory>
#include <vector>

namespace d2x::server::effects {
// A retained SrvDo051 invocation; damage and decisions are immutable after
// their native seed publication. The action owns this continuation.
struct MindBlastPulse {
    enum class Phase { Select,Convert,Damage } phase{};
    ActorContext actor;SkillCastSpec skill;Vec center;
    std::shared_ptr<const MissileHitDamage> base;
    std::vector<EntityId> targets;size_t next{};
    int chance{},duration{},durationBase{},durationRange{};
    uint32_t hitFlags{};uint16_t resultFlags{};
    std::optional<uint64_t> occurrence;
    bool prepared{},manaPaid{},progressivePosted{},complete{};
};
}
