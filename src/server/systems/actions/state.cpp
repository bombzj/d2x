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
State::State() : releases_(std::make_unique<Releases>()) {}
State::~State() = default;
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
}
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
    releases_->pending.erase(actor);
    releases_->monsters.erase(actor);
    if (const auto found = casts.find(actor); found != casts.end()) found->second.interrupted = true;
}
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
