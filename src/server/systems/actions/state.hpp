#pragma once
#include "core/id.hpp"
#include "world/identity.hpp"
#include "gameplay/skills/use_source.hpp"
#include "gameplay/actions/timeline.hpp"
#include "gameplay/events/frame_queue.hpp"
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include <array>
#include <functional>

namespace d2x::server { class PlayerStore; }
namespace d2x::server::skills { class System; }
namespace d2x::server::actions {
struct SkillRelease;
struct MonsterRelease;
struct SkillAction {
    EntityId actor;
    uint16_t skill{};
    uint64_t started{}, revision{}, until{};
    RegionId area;
    EntityId target;
    bool interrupted{};
    uint64_t cooldownUntil{};
    bool usesSkillPointer{};
    uint64_t timeline{};
};
struct RecoveryAction {
    uint64_t until{}, lastBlockTick{};
};
struct State {
    private:
        friend class skills::System;
        struct Releases;
        std::unique_ptr<Releases> releases_;
        FrameEventQueue &frameEvents_;
        std::map<EntityId, RecoveryAction> recoveries_;
        // Player used-skill identity survives action expiry; deleting a source
        // clears the pointer without cancelling already released effects.
        std::map<EntityId,std::optional<NativeSkillSlot>> usedSkills_;
        std::vector<EntityId> executingReleases_;
        std::map<EntityId,std::map<NativeSkillSlot,std::array<int32_t,4>>> skillParameters_;
        std::map<EntityId, SkillRelease> &releases();
        std::map<EntityId, MonsterRelease> &monsterReleases();
    public:
        class ReleaseGuard {
            State &state_;
          public:
            ReleaseGuard(State &,EntityId);
            ~ReleaseGuard();
            ReleaseGuard(const ReleaseGuard &)=delete;
            ReleaseGuard &operator=(const ReleaseGuard &)=delete;
        };
        ReleaseGuard executingRelease(EntityId actor) {return {*this,actor};}
        explicit State(FrameEventQueue &);
        ~State();
        State(const State &) = delete;
        State &operator=(const State &) = delete;
    std::map<EntityId, SkillAction> casts;
    struct PreparedTimeline {
        FrameEventOwner owner;
        uint64_t generation{};
        std::vector<FrameEventQueue::PreparedInsertion> events;
    };
    PreparedTimeline prepareTimeline(EntityId,uint64_t started,const ActionTimeline &);
    PreparedTimeline prepareProgramTimeline(EntityId,uint64_t started);
    void commitTimeline(PreparedTimeline &&) noexcept;
    void cancelTimeline(EntityId);
    using PreparedRecovery = std::map<EntityId, RecoveryAction>::node_type;
    PreparedRecovery prepareRecovery(EntityId actor) const;
    void commitRecovery(PreparedRecovery next) noexcept;
    RecoveryAction recovery(EntityId actor) const;
    void clearRecovery(EntityId actor);
    void pruneRecoveries(const PlayerStore &players);
    using PreparedSkillUse=std::map<EntityId,std::optional<NativeSkillSlot>>::node_type;
    PreparedSkillUse prepareUsedSkill(EntityId actor,NativeSkillSlot) const;
    void commitUsedSkill(PreparedSkillUse) noexcept;
    std::optional<NativeSkillSlot> usedSkill(EntityId actor) const;
    void removeUsedSkills(EntityId actor,std::span<const SkillUseRemoval>) noexcept;
    void pruneUsedSkills(const PlayerStore &players);
    int32_t skillParameter(EntityId,NativeSkillSlot,size_t index) const;
    void skillParameter(EntityId,NativeSkillSlot,size_t index,int32_t value);
    void pruneSkillParameters(const std::function<bool(EntityId)> &exists);
    bool recovering(EntityId actor, uint64_t tick) const;
    bool busy(EntityId actor, uint64_t tick) const;
    bool coolingDown(EntityId actor, uint64_t tick) const;
    size_t pendingReleases() const;
    void cancel(EntityId actor);
    void interrupt(EntityId actor, uint64_t tick);
    void expire(uint64_t tick);
};
}
