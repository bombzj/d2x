#include "state.hpp"
#include "release.hpp"
#include "server/player_store.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::actions {
struct State::Releases {
    std::map<EntityId, SkillRelease> pending;
    std::map<EntityId, MonsterRelease> monsters;
};
State::State(FrameEventQueue &events) : releases_(std::make_unique<Releases>()),frameEvents_(events) {}
State::~State() = default;
State::PreparedTimeline State::prepareProgramTimeline(EntityId actor,uint64_t started) {
    PreparedTimeline result{{FrameEventUnit::Player,actor},frameEvents_.reserveIdentity(),{}};
    result.events.push_back(frameEvents_.prepareInsertion({{},result.owner,FrameEventType::ModeChange,
        started+1,true,0,-1,0,result.generation}));
    return result;
}
State::PreparedTimeline State::prepareTimeline(EntityId actor,uint64_t started,const ActionTimeline &timeline) {
    PreparedTimeline result{{FrameEventUnit::Player,actor},frameEvents_.reserveIdentity(),{}};
    result.events.reserve(timeline.events.size()+1);
    for(const auto &action:timeline.events)
        result.events.push_back(frameEvents_.prepareInsertion({{},result.owner,FrameEventType::ModeChange,
            started+uint64_t(action.tick),false,0,action.event,action.parameter,result.generation}));
    result.events.push_back(frameEvents_.prepareInsertion({{},result.owner,FrameEventType::EndAnimation,
        started+uint64_t(timeline.end),false,0,0,0,result.generation}));
    return result;
}
void State::cancelTimeline(EntityId actor) {
    const FrameEventOwner owner{FrameEventUnit::Player,actor};
    frameEvents_.cancelHandler(owner,FrameEventType::ModeChange,0);
    frameEvents_.cancelHandler(owner,FrameEventType::EndAnimation,0);
}
void State::commitTimeline(PreparedTimeline &&prepared) noexcept {
    cancelTimeline(prepared.owner.unit);
    // Preserve scheduling order, including multiple flags crossed in one tick.
    for(auto &event:prepared.events) frameEvents_.publish(std::move(event));
}
std::map<EntityId, SkillRelease> &State::releases() { return releases_->pending; }
std::map<EntityId, MonsterRelease> &State::monsterReleases() { return releases_->monsters; }
size_t State::pendingReleases() const { return releases_->pending.size() + releases_->monsters.size(); }
State::PreparedRecovery State::prepareRecovery(EntityId actor) const {
    std::map<EntityId, RecoveryAction> candidate;
    candidate.emplace(actor, recovery(actor));
    return candidate.extract(actor);
}
void State::commitRecovery(PreparedRecovery next) noexcept {
    if (next.empty()) return;
    const auto found = recoveries_.find(next.key());
    if (found != recoveries_.end()) found->second = next.mapped();
    else recoveries_.insert(std::move(next));
}
RecoveryAction State::recovery(EntityId actor) const {
    const auto found = recoveries_.find(actor);
    return found == recoveries_.end() ? RecoveryAction{} : found->second;
}
void State::clearRecovery(EntityId actor) { recoveries_.erase(actor); }
void State::pruneRecoveries(const PlayerStore &players) {
    std::erase_if(recoveries_, [&](const auto &entry) { return players.findActor(entry.first) == nullptr; });
}
State::PreparedSkillUse State::prepareUsedSkill(EntityId actor,NativeSkillSlot skill) const {
    if(!actor || skill.skill<0) throw std::invalid_argument("Invalid used skill identity");
    std::map<EntityId,std::optional<NativeSkillSlot>> candidate;
    candidate.emplace(actor,skill);return candidate.extract(actor);
}
void State::commitUsedSkill(PreparedSkillUse next) noexcept {
    if(next.empty()) return;
    const auto found=usedSkills_.find(next.key());
    if(found==usedSkills_.end()) usedSkills_.insert(std::move(next));
    else found->second=next.mapped();
}
std::optional<NativeSkillSlot> State::usedSkill(EntityId actor) const {
    const auto found=usedSkills_.find(actor);
    return found==usedSkills_.end()?std::optional<NativeSkillSlot>{}:found->second;
}
void State::removeUsedSkills(EntityId actor,std::span<const SkillUseRemoval> removals) noexcept {
    const auto found=usedSkills_.find(actor);
    if(found!=usedSkills_.end()) removeSkillUse(found->second,removals);
    if(const auto values=skillParameters_.find(actor);values!=skillParameters_.end()) {
        std::erase_if(values->second,[&](const auto &entry) {
            return std::any_of(removals.begin(),removals.end(),[&](const auto &removed) {
                return entry.first.skill==removed.skill && (!removed.owner || entry.first.owner==*removed.owner);
            });
        });
    }
}
State::WeaponPhase State::weaponPhase(EntityId actor,uint64_t tick) const {
    const auto frame=[](uint64_t elapsed,int speed,int frames) {
        if(speed<=0 || frames<=0) return uint32_t{};
        return uint32_t((elapsed*uint64_t(speed)%uint64_t(frames*256))>>8);
    };
    if(const auto monster=releases_->monsters.find(actor);monster!=releases_->monsters.end()) {
        const auto &release=monster->second;
        const auto elapsed=tick>release.started?tick-release.started:0;
        return {release.nativeSkill && bool(release.nativeSkill->whirlwindAttackHand%2),
            frame(elapsed,release.sequenceSpeed,release.attack.animation?release.attack.animation->frames:0)};
    }
    const auto found=releases_->pending.find(actor);
    if(found==releases_->pending.end()) return {};
    const auto &release=found->second;
    const auto elapsed=std::max<int64_t>(0,int64_t(tick)-release.animationAnchor);
    // Whirlwind St38 clears 0x2000; Do76 toggles it after each hand and
    // RemoveWhirlwindStats clears it again. No flag survives the release.
    return {bool(release.whirlwindAttackHand%2),frame(uint64_t(elapsed),release.weaponSpeed,release.weaponFrames)};
}
std::optional<int> State::activeSkill(EntityId actor) const {
    if(const auto used=usedSkill(actor)) return used->skill;
    if(const auto monster=releases_->monsters.find(actor);monster!=releases_->monsters.end()) return monster->second.request.skill;
    return {};
}
int32_t State::skillParameter(EntityId actor,NativeSkillSlot skill,size_t index) const {
    const auto owner=skillParameters_.find(actor);if(owner==skillParameters_.end()) return 0;
    const auto value=owner->second.find(skill);return value==owner->second.end()?0:value->second.at(index);
}
void State::skillParameter(EntityId actor,NativeSkillSlot skill,size_t index,int32_t value) {skillParameters_[actor][skill].at(index)=value;}
void State::pruneSkillParameters(const std::function<bool(EntityId)> &exists) {std::erase_if(skillParameters_,[&](const auto &entry){return !exists(entry.first);});}
void State::pruneUsedSkills(const PlayerStore &players) {
    std::erase_if(usedSkills_,[&](const auto &entry){return players.findActor(entry.first)==nullptr;});
}
bool State::recovering(EntityId actor, uint64_t tick) const {
    return recovery(actor).until > tick;
}
bool State::busy(EntityId actor, uint64_t tick) const {
    const auto found = casts.find(actor);
    const auto used=usedSkills_.find(actor);
    if(found!=casts.end() && found->second.usesSkillPointer && used!=usedSkills_.end() && !used->second)
        return releases_->monsters.contains(actor);
    return releases_->pending.contains(actor) || releases_->monsters.contains(actor) ||
        (found != casts.end() && found->second.until > tick);
}
bool State::coolingDown(EntityId actor, uint64_t tick) const {
    const auto found = casts.find(actor);
    return found != casts.end() && found->second.cooldownUntil > tick;
}
void State::cancel(EntityId actor) {
    cancelTimeline(actor);
    const auto current=releases_->pending.find(actor);
    if(current!=releases_->pending.end() && (current->second.nativeCallbackStarted ||
        std::find(executingReleases_.begin(),executingReleases_.end(),actor)!=executingReleases_.end())) current->second.cancelled=true;
    else releases_->pending.erase(actor);
    const auto monster=releases_->monsters.find(actor);
    if(monster!=releases_->monsters.end() && (monster->second.nativeCallbackStarted ||
        std::find(executingReleases_.begin(),executingReleases_.end(),actor)!=executingReleases_.end())) monster->second.cancelled=true;
    else releases_->monsters.erase(actor);
    if (const auto found = casts.find(actor); found != casts.end()) found->second.interrupted = true;
}
State::ReleaseGuard::ReleaseGuard(State &state,EntityId actor):state_(state) {state_.executingReleases_.push_back(actor);}
State::ReleaseGuard::~ReleaseGuard() {state_.executingReleases_.pop_back();}
void State::interrupt(EntityId actor, uint64_t tick) {
    if (const auto found = casts.find(actor); found != casts.end()) {
        found->second.interrupted = true;
        found->second.until = tick;
    }
}
void State::expire(uint64_t tick) {
    std::erase_if(casts, [&](const auto &entry) {
        return std::max(entry.second.until, entry.second.cooldownUntil) <= tick &&
            !releases_->pending.contains(entry.first);
    });
}
}
