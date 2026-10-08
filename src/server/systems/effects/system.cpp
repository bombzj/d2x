#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/units/resources.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::effects {
namespace {
TransientAttributes projection(const CombatEffectSet &effects, uint64_t tick) {
    TransientAttributes result; result.modifiers = effects.modifiers(tick);
    for (const auto &effect : effects.entries()) if (effect.activeAt(tick) && effect.spec.state.id >= 0) result.states.insert(effect.spec.state.id);
    return result;
}
}
DomainResult<PotionPlan> System::potion(const ActorContext &actor, const PotionDefinition &potion) const {
    const auto *player = ports_.players.find(actor.player);
    if (!player || player->actor != actor.actor || !player->entered || player->persistent.player.hp <= 0) return {DomainStatus::InvalidActor, {}};
    PotionPlan plan{state_, player->persistent.player, {}};
    auto &recovery = plan.next.players[actor.actor];
    const auto &attributes = player->totals.character;
    const float amount = potionRestorationAmount(potion, player->definition.code);
    switch (potion.kind) {
    case PotionKind::Healing:
    case PotionKind::Mana: {
        if (!std::isfinite(amount) || amount <= 0 || !std::isfinite(potion.seconds) || potion.seconds <= 0) return {DomainStatus::Unavailable, {}};
        auto &queue = potion.kind == PotionKind::Healing ? recovery.healing : recovery.mana;
        if (queue.size() >= 256) return {DomainStatus::Capacity, {}};
        queue.push_back({amount, amount / potion.seconds}); break;
    }
    case PotionKind::Rejuvenation:
        plan.character.hp = std::min(float(attributes.maxLife), plan.character.hp + attributes.maxLife * amount);
        plan.character.mana = std::min(float(attributes.maxMana), plan.character.mana + attributes.maxMana * amount); break;
    case PotionKind::Stamina: plan.character.stamina = float(attributes.maxStamina); break;
    case PotionKind::Remedy: break;
    }
    if (potion.state.id >= 0) {
        if (potion.state.id > 255 || !potion.durationFrames || recovery.states.size() >= 128) return {DomainStatus::Unavailable, {}};
        for (const auto state : potion.cureStates) if (state >= 0) recovery.states.removeState(state);
        auto duration = potion.durationFrames;
        for (const auto &effect : recovery.states.entries())
            if (effect.spec.state.id == potion.state.id && effect.expiresAt && *effect.expiresAt > actor.tick) {
                if (*effect.expiresAt - actor.tick > UINT64_MAX - duration) return {DomainStatus::Capacity, {}};
                duration += *effect.expiresAt - actor.tick;
            }
        CombatEffectSpec spec; spec.state = potion.state; spec.source = {CombatEffectSource::Item, actor.actor, potion.state.id, 0};
        spec.duration = duration; spec.modifiers = potion.modifiers;
        recovery.states.apply(std::move(spec), actor.tick);
    }
    plan.transient = projection(recovery.states, actor.tick);
    if(recovery.poison && !recovery.states.hasState(recovery.poison->damage.state,actor.tick)) recovery.poison.reset();
    return {DomainStatus::Applied, std::move(plan)};
}
DomainResult<> System::apply(const ActorContext &actor, CombatEffectSpec spec, bool restoreStamina) {
    const auto *player = ports_.players.find(actor.player);
    if (!player || player->actor != actor.actor || !player->entered || player->persistent.player.hp <= 0) return {DomainStatus::InvalidActor, {}};
    if (spec.state.id < 0 || spec.state.id > 255 || !spec.reactions.empty() || spec.physicalShield || spec.curseAi != CurseAi::None)
        return {DomainStatus::Unavailable, {}}; // Those execution hooks are not yet installed.
    auto next = state_; auto &states = next.players[actor.actor].states;
    if (states.size() >= 128) return {DomainStatus::Capacity, {}};
    if (!states.apply(std::move(spec), actor.tick).accepted) return {DomainStatus::Conflict, {}};
    transactions::CharacterEdit edit{actor, player->inventoryRevision, player->characterRevision, player->persistent.player};
    if (restoreStamina) edit.player.stamina = float(player->totals.character.maxStamina);
    edit.transient = projection(states, actor.tick);
    auto plan = ports_.transactions.prepare(std::move(edit));
    if (!plan) return {plan.status, {}};
    const auto result = ports_.transactions.commit(std::move(*plan.value));
    if (result) state_.players.swap(next.players);
    return result;
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    bool blocked = false;
    std::erase_if(state_.players, [&](const auto &entry) {
        for (const auto &[id, player] : ports_.players.all()) { (void)id; if (player.actor == entry.first) return false; }
        return true;
    });
    for (const auto &[id, player] : ports_.players.all()) {
        if (!player.entered) continue;
        const auto &area = ports_.areas.at(player.area);
        const ActorContext actor{id, player.actor, player.area, area.generation, 0, tick.tick};
        auto [entry, inserted] = state_.players.try_emplace(player.actor); (void)inserted;
        auto next = entry->second;
        const bool dead = player.persistent.player.hp <= 0;
        const auto removed = dead ? next.states.onDeath(EffectUnitKind::Player) : next.states.expire(tick.tick);
        if (dead) { next.healing.clear(); next.mana.clear(); next.poison.reset(); }
        if (!removed.empty()) {
            transactions::CharacterEdit edit{actor, player.inventoryRevision, player.characterRevision, player.persistent.player};
            restoreStaminaOnEffectRemoval(edit.player.stamina, float(player.totals.character.maxStamina), removed);
            edit.transient = projection(next.states, tick.tick);
            auto plan = ports_.transactions.prepare(std::move(edit));
            if (!plan || !ports_.transactions.commit(std::move(*plan.value))) { blocked = true; continue; }
            entry->second=next; // Expiry has already committed even if resource output waits.
        }
        if (dead) { std::swap(entry->second, next); continue; }
        if (next.poison && !next.states.hasState(next.poison->damage.state,tick.tick)) next.poison.reset();
        auto record = player.persistent.player;
        if (next.poison && tick.tick >= next.poison->next && tick.tick < next.poison->until) {
            record.hp -= float(next.poison->damage.rate)/256.f;
            next.poison->next = tick.tick+1;
        }
        const auto &a = player.totals.character;
        advanceResourceRecovery(record.hp, record.mana, {a.maxLife, a.maxMana, a.manaRegen, a.combat.replenishLife, bool(next.poison)}, TickContext::seconds);
        advanceStamina(record.stamina, {a.maxStamina, a.staminaDrain, a.staminaRecoveryBonus},
            {player.moving, player.runningNow, !player.moving && !ports_.skills.busy(player.actor, tick.tick), area.definition.town}, TickContext::seconds);
        restoreResource(next.healing, record.hp, float(a.maxLife), TickContext::seconds);
        restoreResource(next.mana, record.mana, float(a.maxMana), TickContext::seconds);
        // PlrModes::EVENTS_HpRegen clamps the combined poison/restoration tick
        // at one life; poison alone cannot kill a player.
        if(next.poison) record.hp=std::max(1.f,record.hp);
        const auto result = ports_.transactions.resources(actor, player.characterRevision, record.hp, record.mana, record.stamina);
        if (result) {
            if (advanceSkills(actor, next) == StepStatus::Blocked) blocked = true;
            std::swap(entry->second, next);
        } else blocked = true;
    }
    if (advanceReactions(tick.tick) == StepStatus::Blocked) blocked = true;
    if (advanceUnits(tick.tick) == StepStatus::Blocked) blocked = true;
    if (advanceMonsterSkills(tick.tick) == StepStatus::Blocked) blocked = true;
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}
