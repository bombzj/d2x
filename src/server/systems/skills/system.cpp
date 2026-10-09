#include "system.hpp"
#include "gameplay/skills/behavior.hpp"
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
namespace {
DomainResult<> applied() { return {DomainStatus::Applied, std::monostate{}}; }
}
void System::cancel(PlayerId player, EntityId actor) {
    ports_.companions.cancel(actor);
    pending_.erase(player);
    releases_.erase(actor);
    if (auto it = state_.casts.find(actor); it != state_.casts.end()) it->second.interrupted = true;
    ports_.combat.cancel(actor);
}
DomainResult<> System::attack(const ActorContext &actor, const Request &request,int selectedOverride) {
    const auto *player = ports_.players.find(actor.player);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area || player->persistent.player.hp <= 0)
        return {DomainStatus::InvalidActor, {}};
    const auto &area = ports_.areas.at(player->area);
    if (area.generation != actor.areaGeneration) return {DomainStatus::Unavailable, {}};
    const auto selected = selectedOverride>=0?unsigned(selectedOverride):player->persistent.player.selectedSkills.at(player->persistent.player.weaponSet * 2 + (request.right ? 1 : 0));
    const auto itemSkills=inventory::itemSkills(*player);
    if(itemSkills.contains(selected)) {
        if(busy(player->actor,actor.tick)) return {DomainStatus::Conflict,{}};
        if(!request.right || !itemSkills.at(selected)) return {DomainStatus::Unavailable,{}};
        return cast(actor,request,selected);
    }
    return cast(actor, request, selected);
}
DomainResult<> System::execute(const ActorContext &actor, const Request &request) {
    const auto *player = ports_.players.find(actor.player);
    const auto *area = ports_.areas.find(actor.area);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area || !area || area->generation != actor.areaGeneration || player->persistent.player.hp <= 0)
        return {DomainStatus::InvalidActor, {}};
    if (request.action == Action::Stop) {
        const auto channel=releases_.find(actor.actor);
        if(channel!=releases_.end() && channel->second.skill.effect==SkillBehavior::Inferno) {releases_.erase(channel);if(auto cast=state_.casts.find(actor.actor);cast!=state_.casts.end()) {cast->second.interrupted=true;cast->second.until=actor.tick;}}
        // Original Rcv0x12 clears STATE_INFERNO only. A released mouse must
        // not cancel an ordinary attack's approach or pending action.
        return applied();
    }
    if (request.action == Action::Select || request.action == Action::Bind) {
        if (!player->rules.character) return {DomainStatus::Unavailable, {}};
        const bool clear = request.action == Action::Bind && (request.skill == 0x7FFF || request.skill == 0x0FFF);
        const auto charged=std::find_if(player->totals.chargedSkills.begin(),player->totals.chargedSkills.end(),[&](const auto &c){return c.item.id.value==request.owner && c.skill==request.skill && c.charges>0;});
        const bool charge=request.owner!=UINT32_MAX && charged!=player->totals.chargedSkills.end();
        if(request.owner!=UINT32_MAX && !charge) return {DomainStatus::InvalidRequest,{}};
        const auto quantities=inventory::itemSkills(*player);
        if(quantities.contains(request.skill) && (!request.right || !quantities.at(request.skill))) return {DomainStatus::Unavailable,{}};
        const bool itemSkill=request.right && quantities.contains(request.skill) && quantities.at(request.skill)>0;
        const auto rule = player->rules.character->learning.find(request.skill);
        if (!clear && !itemSkill && (rule == player->rules.character->learning.end() || !rule->second.selectable || (!request.right && !rule->second.leftAllowed)))
            return {DomainStatus::InvalidRequest, {}};
        if (!clear && !player->rules.character->innateSkills.contains(request.skill) && !itemSkill && !charge) {
            const auto learned = player->totals.skillRanks.find(request.skill);
            if (learned == player->totals.skillRanks.end() || learned->second <= 0) return {DomainStatus::InvalidRequest, {}};
        }
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
    const auto result = attack(actor, request);
    if (result.status == DomainStatus::Conflict) { pending_[actor.player] = {actor, request}; return applied(); }
    if (pending_.erase(actor.player) && !result) ports_.movement.execute(actor, {MovementAction::Stop, {}, false});
    return result;
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    const auto playerStatus = release(tick);
    const auto monsterStatus = releaseMonsters(tick);
    const auto status = playerStatus == StepStatus::Blocked || monsterStatus == StepStatus::Blocked ? StepStatus::Blocked : StepStatus::Complete;
    std::erase_if(state_.casts, [&](const auto &entry) { return !releases_.contains(entry.first) && std::max(entry.second.until, entry.second.cooldownUntil) <= tick.tick; });
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
