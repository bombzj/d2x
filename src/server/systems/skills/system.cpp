#include "system.hpp"
#include "runtime.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include "assassin_kick.hpp"
#include "gameplay/skills/use_source.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/movement.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/companions/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/inventory/item_skills.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include <algorithm>
namespace d2x::server::skills {
System::System(Ports ports):state_(ports.actions),ports_(ports),runtime_(std::make_unique<Runtime>(state_.releases(),state_.monsterReleases())) {}
System::~System()=default;
bool System::uninterruptible(EntityId actor) const {
    if(!state_.usedSkill(actor)) return false;
    const auto &releases_=runtime_->releases;
    const auto found=releases_.find(actor);
    return found!=releases_.end() && (found->second.skill.leap || (found->second.skill.weapon && !found->second.skill.weapon->interruptible));
}
size_t System::pendingReleases() const {return state_.pendingReleases();}
bool System::busy(EntityId id,uint64_t tick) const {
    return state_.recovering(id,tick) || state_.busy(id,tick);
}
void System::interrupt(const ActorContext &actor) {
    cancel(actor.player,actor.actor);
    state_.interrupt(actor.actor,actor.tick);
    ports_.movement.execute(actor,{MovementAction::Stop,{},{},{}});
}
namespace {
DomainResult<> applied() { return {DomainStatus::Applied, std::monostate{}}; }
}
void System::cancel(PlayerId player, EntityId actor) {
    auto &pending_=runtime_->pending;
    ports_.companions.cancel(actor);
    pending_.erase(player);
    runtime_->assassinStarts.erase(actor);
    state_.cancel(actor);
    ports_.combat.cancel(actor);
}
DomainResult<> System::attack(const ActorContext &actor, const Request &request,int selectedOverride) {
    const auto *player = ports_.players.find(actor.player);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area || player->persistent.player.hp <= 0)
        return {DomainStatus::InvalidActor, {}};
    const auto &area = ports_.areas.at(player->area);
    if (area.generation != actor.areaGeneration) return {DomainStatus::Unavailable, {}};
    // Persistent -1 is the ordinary Attack alias (native skill 0), also used
    // by an unset hand. Match the selected-skill wire projection on reentry.
    const auto selected = selectedOverride >= 0 ? selectedOverride :
        std::max(0, player->persistent.player.selectedSkills.at(player->persistent.player.weaponSet * 2 + (request.right ? 1 : 0)));
    const auto itemSkills=inventory::itemSkills(*player);
    if(player->rules.skills && player->rules.skills->auras.contains(int(selected)))
        return request.right ? applied() : DomainResult<>{DomainStatus::InvalidRequest,{}};
    if(itemSkills.contains(selected)) {
        if(busy(player->actor,actor.tick)) return {DomainStatus::Conflict,{}};
        if(!request.right || !itemSkills.at(selected)) return {DomainStatus::Unavailable,{}};
        return cast(actor,request,selected);
    }
    return cast(actor, request, selected);
}
DomainResult<> System::execute(const ActorContext &actor, const Request &request) {
    auto &pending_=runtime_->pending;auto &releases_=runtime_->releases;
    const auto *player = ports_.players.find(actor.player);
    const auto *area = ports_.areas.find(actor.area);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area || !area || area->generation != actor.areaGeneration || player->persistent.player.hp <= 0)
        return {DomainStatus::InvalidActor, {}};
    if (request.action == Action::Stop) {
        if(const auto result=ports_.effects.setNativeSkillState(actor.actor,player->rules.skills->formulas->infernoState,false,actor.tick);!result) return result;
        const auto channel=releases_.find(actor.actor);
        if(channel!=releases_.end() && channel->second.skill.effect==SkillBehavior::Inferno) {releases_.erase(channel);state_.interrupt(actor.actor,actor.tick);}
        // Original Rcv0x12 clears STATE_INFERNO only. A released mouse must
        // not cancel an ordinary attack's approach or pending action.
        return applied();
    }
    if (request.action == Action::Select || request.action == Action::Bind) {
        if (!player->rules.character) return {DomainStatus::Unavailable, {}};
        const bool clear = request.action == Action::Bind && (request.skill == 0x7FFF || request.skill == 0x0FFF);
        const auto learned = player->totals.skillRanks.find(request.skill);
        const auto source = resolveSkillUseSource(request.skill, request.owner,
            player->rules.character->innateSkills.contains(request.skill),
            learned == player->totals.skillRanks.end() ? 0 : learned->second, player->totals.chargedSkills);
        if(!source) return {DomainStatus::InvalidRequest,{}};
        const bool charge=source->charged.has_value();
        const auto quantities=inventory::itemSkills(*player);
        if(quantities.contains(request.skill) && (!request.right || !quantities.at(request.skill))) return {DomainStatus::Unavailable,{}};
        const bool itemSkill=request.right && quantities.contains(request.skill) && quantities.at(request.skill)>0;
        const auto rule = player->rules.character->learning.find(request.skill);
        if (!clear && !itemSkill && (rule == player->rules.character->learning.end() || !rule->second.selectable || (!request.right && !rule->second.leftAllowed)))
            return {DomainStatus::InvalidRequest, {}};
        if (!clear && !itemSkill && !charge && source->rank <= 0) return {DomainStatus::InvalidRequest, {}};
        auto record = player->persistent.player;
        if (request.action == Action::Bind) {
            if (!request.hotkey || *request.hotkey >= record.skillHotkeys.size()) return {DomainStatus::InvalidRequest, {}};
            record.skillHotkeys.at(*request.hotkey) = {clear ? -2 : request.skill == 0 ? -1 : int(request.skill), request.right, clear?UINT32_MAX:request.owner};
        } else {const auto slot=record.weaponSet * 2 + (request.right ? 1 : 0);record.selectedSkills.at(slot)=request.skill;record.selectedSkillOwners.at(slot)=request.owner;}
        transactions::CharacterEdit selection{actor,player->inventoryRevision,player->characterRevision,std::move(record)};
        if(request.action==Action::Select) selection.selectedHand=request.right;
        auto plan = ports_.transactions.prepare(std::move(selection));
        if (!plan) return {plan.status, {}};
        auto result = ports_.transactions.commit(std::move(*plan.value));
        if (result && request.action == Action::Select) {
            const auto channel=releases_.find(actor.actor);
            if(channel!=releases_.end() && channel->second.skill.effect==SkillBehavior::Inferno && channel->second.skill.sourceId!=request.skill) cancel(actor.player,actor.actor);
            if(pending_.erase(actor.player)) ports_.movement.execute(actor, {MovementAction::Stop, {}, false});
        }
        return result;
    }
    if (request.action != Action::Cast) return {};
    if(state_.recovering(actor.actor,actor.tick)) return {DomainStatus::Conflict,{}};
    const auto result = attack(actor, request);
    if (result.status == DomainStatus::Conflict) { pending_[actor.player] = {actor, request}; return applied(); }
    if (pending_.erase(actor.player) && !result) ports_.movement.execute(actor, {MovementAction::Stop, {}, false});
    return result;
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    auto &pending_=runtime_->pending;
    std::erase_if(runtime_->assassinStarts,[&](const auto &entry) {
        const auto *player=ports_.players.findActor(entry.first);const auto *monster=ports_.monsters.find(entry.first);
        const auto *area=ports_.areas.find(entry.second.actor.area);
        const bool alive=area && area->generation==entry.second.actor.areaGeneration && (player?
            player->entered && player->persistent.player.hp>0 && player->area==entry.second.actor.area:
            monster && monster->alive() && monster->area==entry.second.actor.area);
        if(!alive && entry.second.assassinKick && entry.second.assassinKick->contact) entry.second.assassinKick->contact->cancelled=true;
        return !alive;
    });
    const auto playerStatus = release(tick);
    const auto monsterStatus = releaseMonsters(tick);
    const auto status = playerStatus == StepStatus::Blocked || monsterStatus == StepStatus::Blocked ? StepStatus::Blocked : StepStatus::Complete;
    state_.expire(tick.tick);
    state_.pruneUsedSkills(ports_.players);
    state_.pruneSkillParameters([&](EntityId id){return ports_.players.findActor(id) || ports_.monsters.find(id);});
    for (auto it = pending_.begin(); it != pending_.end();) {
        auto actor = it->second.actor; actor.tick = tick.tick;
        const auto result = attack(actor, it->second.request);
        if (result.status == DomainStatus::Conflict || result.status == DomainStatus::Capacity) ++it;
        else {
            if (!result) ports_.movement.execute(actor, {MovementAction::Stop, {}, false});
            it = pending_.erase(it);
        }
    }
    return status;
}
}
