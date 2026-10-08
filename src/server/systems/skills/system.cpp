#include "system.hpp"
#include "gameplay/skills/behavior.hpp"
#include "server/player_store.hpp"
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
DomainResult<> System::requestCast(const CastRequest &request) {
    if (request.skill != 0) return {};
    const auto *monster = ports_.monsters.find(request.actor);
    const auto *target = std::get_if<UnitTarget>(&request.target);
    if (!monster || monster->life <= 0 || !target || request.tick < monster->busyUntil || monster->frozenUntil > request.tick || (monster->owner && (!monster->amazonPet || monster->amazonPet->decoy))) return {DomainStatus::InvalidActor, {}};
    const auto destination=ports_.monsters.targetPosition(target->id,monster->area);
    const auto *victim=ports_.monsters.find(target->id);
    const bool petAttack=monster->amazonPet && !monster->amazonPet->decoy;
    if(!destination || (petAttack?target->type!=1 || !victim || victim->owner:(target->type==1 && (!victim || !victim->amazonPet))) || target->type>1) return {DomainStatus::InvalidActor,{}};
    const auto &area=ports_.areas.at(monster->area);
    if(area.definition.town || meleeDistance(monster->position,monster->rule.size,destination->first,destination->second)>monster->rule.meleeRange ||
        !area.definition.collision.segment(monster->position,destination->first)) return {DomainStatus::Unavailable,{}};
    if (!ports_.events.hasCapacity(1)) return {DomainStatus::Capacity, {}};
    const auto &rule = monster->rule;
    const int speed = monster->chilledUntil > request.tick ? std::max(15, 100 + rule.coldEffect) : 100;
    const auto scaled = [&](int frames) { return uint64_t((int64_t(frames) * 100 + speed - 1) / speed); };
    DamageType type = DamageType::Physical;
    combat::Damage damage{monster->id, target->id, type, int64_t(rule.minimumDamage) * 256,
        int64_t(rule.maximumDamage) * 256, request.tick + 1, monster->area, request.tick + scaled(rule.impactTick), rule.attackRating, rule.level, rule.meleeRange, rule.size, petAttack?monster->petWeapon:std::optional<WeaponDamage>{}, 0, 0};
    if(petAttack && damage.weapon) {
        const auto buffs=ports_.effects.unitModifiers(monster->id,request.tick).combat;
        damage.attackModifiers=monster->petStats.attributes.combat;mergeCombatModifiers(*damage.attackModifiers,buffs);
        damage.weapon->attackRatingPercent+=buffs.attackRatingPercent;damage.weapon->damagePercent+=buffs.damagePercent;
    }
    auto queued=ports_.combat.enqueue(damage);
    if (!queued) return queued;
    auto event = ports_.events.publish({0, request.tick, {}, {AudienceKind::Area, {}, monster->area},
        {AttackFact{monster->id, target->id, 1, target->type, monster->area, monster->position, destination->first, request.tick + 1}}});
    if (!event) { ports_.combat.cancel(monster->id); return {event.status, {}}; }
    return ports_.monsters.beginAttack(monster->id, request.tick + scaled(rule.attackTicks));
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
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area || player->persistent.player.hp <= 0)
        return {DomainStatus::InvalidActor, {}};
    if (request.action == Action::Stop) {
        const auto channel=releases_.find(actor.actor);
        if(channel!=releases_.end() && channel->second.skill.effect==SkillBehavior::Inferno) {releases_.erase(channel);if(auto cast=state_.casts.find(actor.actor);cast!=state_.casts.end()) {cast->second.interrupted=true;cast->second.until=actor.tick;}}
        if (pending_.erase(actor.player)) ports_.movement.execute(actor, {MovementAction::Stop, {}, false});
        return applied();
    }
    if (request.action == Action::Select || request.action == Action::Bind) {
        if (!player->rules.character) return {DomainStatus::Unavailable, {}};
        const auto quantities=inventory::itemSkills(*player);
        if(quantities.contains(request.skill) && (!request.right || !quantities.at(request.skill))) return {DomainStatus::Unavailable,{}};
        const bool itemSkill=request.right && quantities.contains(request.skill) && quantities.at(request.skill)>0;
        const auto rule = player->rules.character->learning.find(request.skill);
        if (!itemSkill && (rule == player->rules.character->learning.end() || !rule->second.selectable || (!request.right && !rule->second.leftAllowed)))
            return {DomainStatus::InvalidRequest, {}};
        if (!player->rules.character->innateSkills.contains(request.skill) && !itemSkill) {
            const auto learned = player->totals.skillRanks.find(request.skill);
            if (learned == player->totals.skillRanks.end() || learned->second <= 0) return {DomainStatus::InvalidRequest, {}};
        }
        auto record = player->persistent.player;
        if (request.action == Action::Bind) {
            if (!request.hotkey || *request.hotkey >= record.skillHotkeys.size()) return {DomainStatus::InvalidRequest, {}};
            record.skillHotkeys.at(*request.hotkey) = {request.skill == 0 ? -1 : int(request.skill), request.right};
        } else record.selectedSkills.at(record.weaponSet * 2 + (request.right ? 1 : 0)) = request.skill;
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
    const auto status = release(tick);
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
