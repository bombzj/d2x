#include "system.hpp"
#include "gameplay/skills/behavior.hpp"
#include "server/player_store.hpp"
#include "server/movement.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/inventory/item_skills.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include <algorithm>
namespace d2x::server::skills {
namespace {
DomainResult<> applied() { return {DomainStatus::Applied, std::monostate{}}; }
template<class Modifiers> bool needsAttackEffects(const Modifiers &value) {
    return value.lightningMinimum || value.lightningMaximum ||
        value.coldMinimum || value.coldMaximum || value.magicMinimum || value.magicMaximum ||
        value.poisonMinimum || value.poisonMaximum || value.lifeLeech || value.manaLeech ||
        value.crushingBlow || value.openWounds;
}
}
void System::cancel(PlayerId player, EntityId actor) {
    pending_.erase(player);
    releases_.erase(actor);
    if (auto it = state_.casts.find(actor); it != state_.casts.end()) it->second.interrupted = true;
    ports_.combat.cancel(actor);
}
DomainResult<> System::requestCast(const CastRequest &request) {
    if (request.skill != 0) return {};
    const auto *monster = ports_.monsters.find(request.actor);
    const auto *target = std::get_if<UnitTarget>(&request.target);
    if (!monster || monster->life <= 0 || !target || request.tick < monster->busyUntil || monster->frozenUntil > request.tick || monster->owner) return {DomainStatus::InvalidActor, {}};
    const PlayerState *player = nullptr;
    for (const auto &[id, candidate] : ports_.players.all()) {
        (void)id; if (candidate.actor == target->id) { player = &candidate; break; }
    }
    if (!player || !player->entered || player->persistent.player.hp <= 0 || player->area != monster->area) return {DomainStatus::InvalidActor, {}};
    const auto &area = ports_.areas.at(monster->area);
    if (area.definition.town || meleeDistance(monster->position, monster->rule.size, player->position, 2) > monster->rule.meleeRange ||
        !area.definition.collision.segment(monster->position, player->position)) return {DomainStatus::Unavailable, {}};
    if (!ports_.events.hasCapacity(1)) return {DomainStatus::Capacity, {}};
    const auto &rule = monster->rule;
    const int speed = monster->chilledUntil > request.tick ? std::max(15, 100 + rule.coldEffect) : 100;
    const auto scaled = [&](int frames) { return uint64_t((int64_t(frames) * 100 + speed - 1) / speed); };
    DamageType type = DamageType::Physical;
    auto queued = ports_.combat.enqueue({monster->id, player->actor, type, int64_t(rule.minimumDamage) * 256,
        int64_t(rule.maximumDamage) * 256, request.tick + 1, monster->area, request.tick + scaled(rule.impactTick), rule.attackRating, rule.level, rule.meleeRange, rule.size, {}, 0, 0});
    if (!queued) return queued;
    auto event = ports_.events.publish({0, request.tick, {}, {AudienceKind::Area, {}, monster->area},
        {AttackFact{monster->id, player->actor, 1, 0, monster->area, monster->position, player->position, request.tick + 1}}});
    if (!event) { ports_.combat.cancel(monster->id); return {event.status, {}}; }
    return ports_.monsters.beginAttack(monster->id, request.tick + scaled(rule.attackTicks));
}
DomainResult<> System::attack(const ActorContext &actor, const Request &request) {
    const auto *player = ports_.players.find(actor.player);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area || player->persistent.player.hp <= 0)
        return {DomainStatus::InvalidActor, {}};
    const auto &area = ports_.areas.at(player->area);
    if (area.generation != actor.areaGeneration) return {DomainStatus::Unavailable, {}};
    const auto selected = player->persistent.player.selectedSkills.at(player->persistent.player.weaponSet * 2 + (request.right ? 1 : 0));
    const auto itemSkills=inventory::itemSkills(*player);
    if(request.right && itemSkills.contains(selected)) {
        if(busy(player->actor,actor.tick)) return {DomainStatus::Conflict,{}};
        return ports_.travel.createPortal(actor,{},selected);
    }
    if (selected != 0) return cast(actor, request, selected);
    if (area.definition.town) return {DomainStatus::Unavailable, {}};
    if (!player->rules.melee) return {DomainStatus::Unavailable, {}};
    if (busy(player->actor, actor.tick)) return {DomainStatus::Conflict, {}};
    const auto &weapon = player->totals.equipment.weapons[0];
    const auto &modifiers = player->totals.character.combat;
    if (weapon.ranged || weapon.potion || player->totals.equipment.weaponCount > 1 || needsAttackEffects(modifiers)) return {};
    if (auto own = modifiers.weapons.find(weapon.item); own != modifiers.weapons.end() && needsAttackEffects(own->second)) return {};
    // Effects/dual-wield are separate slices; never silently drop item damage.
    const auto timing = player->rules.melee->animations.find(player->totals.equipment.animationClass);
    if (timing == player->rules.melee->animations.end()) return {DomainStatus::Unavailable, {}};
    Vec destination;
    EntityId target;
    if (!request.target) return {DomainStatus::InvalidRequest, {}};
    if (const auto *unit = std::get_if<UnitTarget>(&*request.target)) {
        const auto *monster = ports_.monsters.find(unit->id);
        if (!monster || monster->area != player->area || monster->life <= 0) return {DomainStatus::InvalidRequest, {}};
        destination = monster->position; target = monster->id;
        if ((destination - player->position).length() > 50) return {DomainStatus::InvalidRequest, {}};
        if (meleeDistance(player->position, 2, destination, monster->rule.size) > weapon.rangeAdder + 1 ||
            !area.definition.collision.segment(player->position, destination)) {
            if (request.stationary) return {DomainStatus::Unavailable, {}};
            auto status = ports_.movement.execute(actor, {MovementAction::Move, destination, false});
            return {status == CommandStatus::Applied ? DomainStatus::Conflict : DomainStatus::Unavailable, {}};
        }
    } else {
        const auto &point = std::get<PointTarget>(*request.target);
        if (point.area != actor.area || point.generation != area.generation || !std::isfinite(point.position.x) || !std::isfinite(point.position.y))
            return {DomainStatus::InvalidRequest, {}};
        destination = point.position;
    }
    const auto &animation = timing->second;
    if (weapon.fasterAttack <= -120) return {DomainStatus::Unavailable, {}};
    const WeaponAttackTiming attackTiming{"a1", animation.frames,
        effectiveAttackSpeed(animation.speed, weapon.fasterAttack, weapon.baseSpeed, player->totals.character.combat.attackRate),
        animation.actionFrame, animation.startFrame};
    const int duration = attackTiming.durationTicks(), impact = attackTiming.actionTick();
    if (!ports_.events.hasCapacity(1)) return {DomainStatus::Capacity, {}};
    if (target) {
        combat::Damage damage{player->actor, target, DamageType::Physical, weapon.minimum, weapon.maximum,
            actor.sequence, actor.area, actor.tick + uint64_t(impact), weapon.attackRating, player->persistent.player.level, weapon.rangeAdder + 1, 2, {}, 0, 0};
        damage.weapon = weapon;
        damage.criticalChance = player->totals.character.combat.criticalStrike;
        damage.deadlyChance = player->totals.character.combat.deadlyStrike;
        if (auto own = player->totals.character.combat.weapons.find(weapon.item); own != player->totals.character.combat.weapons.end())
            damage.deadlyChance += own->second.deadlyStrike;
        auto queued = ports_.combat.enqueue(damage);
        if (!queued) return queued;
    }
    auto event = ports_.events.publish({0, actor.tick, {}, {AudienceKind::Area, {}, actor.area},
        {AttackFact{player->actor, target, 0, 1, actor.area, player->position, destination, actor.sequence}}});
    if (!event) { ports_.combat.cancel(player->actor); return {event.status, {}}; }
    ports_.movement.execute(actor, {MovementAction::Stop, {}, false});
    const auto previous=state_.casts.find(player->actor);const uint64_t cooldown=previous==state_.casts.end()?0:previous->second.cooldownUntil;
    state_.casts[player->actor] = {player->actor, 0, actor.tick, actor.sequence, actor.tick + uint64_t(duration), actor.area, target};
    state_.casts.at(player->actor).cooldownUntil=cooldown;
    return applied();
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
        const auto quantities=inventory::itemSkills(*player); const bool itemSkill=request.right && quantities.contains(request.skill) && quantities.at(request.skill)>0;
        const auto rule = player->rules.character->learning.find(request.skill);
        if (!itemSkill && (rule == player->rules.character->learning.end() || !rule->second.selectable || (!request.right && !rule->second.leftAllowed)))
            return {DomainStatus::InvalidRequest, {}};
        if (request.skill != 0 && !itemSkill) {
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
