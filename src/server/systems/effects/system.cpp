#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/units/resources.hpp"
#include "server/systems/attributes/calculation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
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
    const auto *area = ports_.areas.find(actor.area);
    if (!player || player->actor != actor.actor || !player->entered || player->area != actor.area || !area || area->generation != actor.areaGeneration || player->persistent.player.hp <= 0) return {DomainStatus::InvalidActor, {}};
    PotionPlan plan{state_, player->persistent.player, {}};
    auto &recovery = plan.next.players[actor.actor];
    const auto &attributes = player->totals.character;
    const float amount = potionRestorationAmount(potion, player->definition.code);
    auto duration = potion.durationFrames;
    const bool timedRecovery = potion.kind == PotionKind::Healing || potion.kind == PotionKind::Mana;
    switch (potion.kind) {
    case PotionKind::Healing:
    case PotionKind::Mana: {
        if (potion.state.id < 0 || potion.state.id > 255 || !duration) return {DomainStatus::Unavailable, {}};
        auto &queue = potion.kind == PotionKind::Healing ? recovery.healing : recovery.mana;
        const auto previous = queue.find(potion.state.id);
        const bool active = recovery.states.hasState(potion.state.id, actor.tick);
        auto random = ports_.random;
        const auto value = rollPotionRestoration(potion, player->definition.code,
            potion.kind == PotionKind::Healing ? attributes.vitality : attributes.energy, random);
        if (value <= 0) return {DomainStatus::Unavailable, {}};
        const auto combined = combineRestoration(active && previous != queue.end() ? &previous->second : nullptr,
                                                  actor.tick, duration, value);
        if (!combined) return {DomainStatus::Capacity, {}};
        queue.insert_or_assign(potion.state.id, *combined);
        duration = combined->until - actor.tick; plan.random = random; break;
    }
    case PotionKind::Rejuvenation: {
        if (!std::isfinite(amount) || amount <= 0 || amount > 1) return {DomainStatus::Unavailable, {}};
        const auto percent = int64_t(std::lround(amount * 100.f));
        plan.character.hp = std::min(float(attributes.maxLife), plan.character.hp + float(int64_t(attributes.maxLife)*256*percent/100)/256.f);
        plan.character.mana = std::min(float(attributes.maxMana), plan.character.mana + float(int64_t(attributes.maxMana)*256*percent/100)/256.f); break;
    }
    case PotionKind::Stamina: plan.character.stamina = float(attributes.maxStamina); break;
    case PotionKind::Remedy: break;
    }
    if (potion.state.id >= 0) {
        if (potion.state.id > 255 || !potion.durationFrames) return {DomainStatus::Unavailable, {}};
        if (recovery.states.size() >= 128 && !recovery.states.hasState(potion.state.id,actor.tick)) return {DomainStatus::Capacity, {}};
        for (const auto state : potion.cureStates) if (state >= 0) recovery.states.removeState(state);
        for (const auto &effect : recovery.states.entries())
            if (!timedRecovery && effect.spec.state.id == potion.state.id && effect.expiresAt && *effect.expiresAt > actor.tick) {
                if (*effect.expiresAt - actor.tick > UINT64_MAX - duration) return {DomainStatus::Capacity, {}};
                duration += *effect.expiresAt - actor.tick;
            }
        CombatEffectSpec spec; spec.state = potion.state; spec.source = {CombatEffectSource::Item, actor.actor, potion.state.id, 0};
        spec.duration = duration; spec.modifiers = potion.modifiers;
        if (duration > UINT64_MAX - actor.tick) return {DomainStatus::Capacity, {}};
        try { if (!recovery.states.apply(std::move(spec), actor.tick).accepted) return {DomainStatus::Conflict, {}}; }
        catch (const std::overflow_error &) { return {DomainStatus::Capacity, {}}; }
    }
    plan.transient = projection(recovery.states, actor.tick);
    if(recovery.poison && !recovery.states.hasState(recovery.poison->damage.state,actor.tick)) recovery.poison.reset();
    return {DomainStatus::Applied, std::move(plan)};
}
DomainResult<PotionPlan> System::heal(const ActorContext &actor) const {
    const auto *player = ports_.players.find(actor.player);
    const auto *area = ports_.areas.find(actor.area);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area ||
        !area || area->generation != actor.areaGeneration || player->persistent.player.hp <= 0)
        return {DomainStatus::InvalidActor, {}};
    if (!player->rules.character) return {DomainStatus::Unavailable, {}};
    PotionPlan plan{state_, player->persistent.player, {}};
    auto &recovery = plan.next.players[actor.actor];
    for (const auto state : player->rules.character->healerCureStates) recovery.states.removeState(state);
    if (recovery.poison && !recovery.states.hasState(recovery.poison->damage.state, actor.tick)) recovery.poison.reset();
    plan.character.hp = float(player->totals.character.maxLife);
    plan.character.mana = float(player->totals.character.maxMana);
    plan.character.stamina = float(player->totals.character.maxStamina);
    plan.transient = projection(recovery.states, actor.tick);
    for(const auto &[id,m]:ports_.monsters.read().actors) if(m.hireling && m.owner==actor.player && m.life>0 && m.area==actor.area) {
        plan.healedHirelings.push_back(id);
        if(m.poison) plan.publicFacts.emplace_back(StateFact{id,1,m.area,m.poison->damage.state,false});
        if(m.chilledUntil || m.frozenUntil) {
            plan.publicFacts.emplace_back(StateFact{id,1,m.area,m.rule.coldState,false});
            plan.publicFacts.emplace_back(StateFact{id,1,m.area,m.rule.frozenState,false});
        }
    }
    return {DomainStatus::Applied, std::move(plan)};
}
void System::commit(PotionPlan &&plan) noexcept {
    state_.players.swap(plan.next.players);if(plan.random) ports_.random=*plan.random;
    for(const auto id:plan.healedHirelings) ports_.monsters.healHireling(id);
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
    bool blocked = advancePaladinAuras(tick.tick)==StepStatus::Blocked;
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
        if (dead) {
            if (!removed.empty()) {
                transactions::CharacterEdit edit{actor,player.inventoryRevision,player.characterRevision,player.persistent.player};
                restoreStaminaOnEffectRemoval(edit.player.stamina,float(player.totals.character.maxStamina),removed);
                edit.transient=projection(next.states,tick.tick);
                auto plan=ports_.transactions.prepare(std::move(edit));
                if (!plan || !ports_.transactions.commit(std::move(*plan.value))) { blocked=true; continue; }
            }
            std::swap(entry->second,next); continue;
        }
        auto record = player.persistent.player;
        std::optional<CharacterAttributes> changedAttributes;
        if (!removed.empty()) {
            const auto transient=projection(next.states,tick.tick);
            try { changedAttributes=attributes::calculate(player.definition,player.persistent,*player.rules.items,
                *player.rules.equipment,*player.rules.character,{},transient.modifiers,transient.states).character; }
            catch (const std::runtime_error &) { blocked=true; continue; }
            catch (const std::out_of_range &) { blocked=true; continue; }
        }
        const auto &a = changedAttributes ? *changedAttributes : player.totals.character;
        restoreStaminaOnEffectRemoval(record.stamina,float(a.maxStamina),removed);
        const auto prune = [&](auto &queue) {
            // Expiration may have occurred while output was blocked. Settle the
            // accepted frames before removing the timer, in the same transaction.
            std::erase_if(queue, [&](const auto &effect) { return !entry->second.states.hasState(effect.first,effect.second.next); });
        };
        prune(next.healing); prune(next.mana);
        int64_t healing = 0, mana = 0;
        for (auto &[state, effect] : next.healing) { (void)state; healing += advanceRestorationFrame(effect,tick.tick); }
        for (auto &[state, effect] : next.mana) { (void)state; mana += advanceRestorationFrame(effect,tick.tick); }
        int64_t lifeDelta = a.combat.replenishLife + healing;
        if (next.poison && !entry->second.states.hasState(next.poison->damage.state,next.poison->next)) next.poison.reset();
        if (next.poison && tick.tick >= next.poison->next && next.poison->next < next.poison->until) {
            const auto last=std::min(tick.tick,next.poison->until-1);
            lifeDelta -= int64_t(last-next.poison->next+1) * next.poison->damage.rate;
            next.poison->next=last+1;
        }
        if (next.poison && next.poison->next >= next.poison->until) next.poison.reset();
        const int64_t life = int64_t(record.hp * 256) + lifeDelta;
        const bool suppressMana = player.rules.character && next.states.hasState(player.rules.character->noManaRegenState,tick.tick);
        const int64_t manaDelta = mana + (suppressMana ? 0 : int64_t(std::lround(a.manaRegen * 256.f / 25.f)));
        bool stateChanged = !removed.empty();
        const auto stop = [&](auto &queue) {
            for (const auto &[state, effect] : queue) { (void)effect; next.states.removeState(state); }
            stateChanged |= !queue.empty(); queue.clear();
        };
        // PlrModes: life overflow removes Healthpot; mana removes Manapot only
        // when it was already full before this frame's positive recovery.
        if (life > int64_t(a.maxLife) * 256) stop(next.healing);
        if (record.mana >= a.maxMana && manaDelta > 0) stop(next.mana);
        record.hp = float(std::clamp<int64_t>(life,lifeDelta ? 256 : 1,int64_t(a.maxLife)*256)) / 256.f;
        record.mana = float(std::clamp<int64_t>(int64_t(record.mana*256)+manaDelta,0,int64_t(a.maxMana)*256)) / 256.f;
        advanceStamina(record.stamina, {a.maxStamina, a.staminaDrain, a.staminaRecoveryBonus},
            {player.moving, player.runningNow, !player.moving && !ports_.skills.busy(player.actor, tick.tick), area.definition.town}, TickContext::seconds);
        DomainResult<> result;
        std::erase_if(next.healing,[&](const auto &effect){return effect.second.next >= effect.second.until;});
        std::erase_if(next.mana,[&](const auto &effect){return effect.second.next >= effect.second.until;});
        if (stateChanged) {
            transactions::CharacterEdit edit{actor,player.inventoryRevision,player.characterRevision,record};
            edit.transient = projection(next.states,tick.tick);
            auto plan = ports_.transactions.prepare(std::move(edit));
            result = plan ? ports_.transactions.commit(std::move(*plan.value)) : DomainResult<>{plan.status,{}};
        } else result = ports_.transactions.resources(actor,player.characterRevision,record.hp,record.mana,record.stamina);
        if (result) {
            if (advanceSkills(actor, next) == StepStatus::Blocked) blocked = true;
            std::swap(entry->second, next);
        } else blocked = true;
    }
    if (advanceItemTriggers(tick.tick) == StepStatus::Blocked) blocked=true;
    if (advanceReactions(tick.tick) == StepStatus::Blocked) blocked = true;
    if (advanceUnits(tick.tick) == StepStatus::Blocked) blocked = true;
    if (advanceMonsterSkills(tick.tick) == StepStatus::Blocked) blocked = true;
    if (advanceMonsterEnchantments(tick.tick) == StepStatus::Blocked) blocked = true;
    if (advanceHirelings(tick.tick) == StepStatus::Blocked) blocked = true;
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}
