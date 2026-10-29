#include "system.hpp"
#include "attribute_projection.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/system.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/event_frame.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include <algorithm>
#include <vector>
#include <stdexcept>

namespace d2x::server::effects {
void System::registerPlayer(EntityId id,uint64_t tick) {
    const auto *player=ports_.players.findActor(id);if(!player) throw std::invalid_argument("Missing player regeneration owner");
    auto [current,fresh]=state_.players.try_emplace(id);
    if(fresh && player->transient.effects) current->second.states=*player->transient.effects;
    const auto due=nativeEventDeadline(tick,nativeAdd(nativeNarrow(uint32_t(tick)),1));
    const FrameEventOwner owner{FrameEventUnit::Player,id};
    const std::vector<FrameEvent> events{FrameEvent{{},owner,FrameEventType::StatRegeneration,due.due,due.continuous}};
    auto edit=ports_.frameEvents.prepareEdit(owner,FrameEventType::StatRegeneration,events);ports_.frameEvents.publish(std::move(edit));
}
DomainResult<> System::regeneratePlayer(EntityId id,uint64_t tick) {
    const auto *player=ports_.players.findActor(id);const FrameEventOwner owner{FrameEventUnit::Player,id};
    if(!player) {ports_.frameEvents.cancelOwner(owner);return {DomainStatus::Applied,std::monostate{}};}
    const auto due=nativeEventDeadline(tick,nativeAdd(nativeNarrow(uint32_t(tick)),1));
    const std::vector<FrameEvent> events{FrameEvent{{},owner,FrameEventType::StatRegeneration,due.due,due.continuous}};
    auto timer=ports_.frameEvents.prepareEdit(owner,FrameEventType::StatRegeneration,events);
    if(!player->entered || player->persistent.player.hp<=0) {
        ports_.frameEvents.publish(std::move(timer));return {DomainStatus::Applied,std::monostate{}};
    }
    const auto current=state_.players.find(id);if(current==state_.players.end()) throw std::logic_error("Missing admitted player effects");
    const auto &posted=player->transient.effects?*player->transient.effects:current->second.states;
    const auto &statRules=*player->rules.character->stats;
    const auto &record=player->persistent.player;
    const auto &values=player->totals.character;
    const auto hpRateNow=nativeStatValue(player->totals.nativeStats,statRules,"hpregen");
    const auto manaRateNow=nativeAdd(nativeStatValue(player->totals.nativeStats,statRules,"manarecovery"),
        posted.hasState(player->rules.character->noManaRegenState,tick)?0:values.manaRegenPerFrame);
    const bool withdrawLife=hpRateNow && nativeAdd(record.hp.raw(),hpRateNow)>values.maxLifeFixed.raw() &&
        (posted.findState(player->rules.skills->healthPotion.id) || posted.hasState(player->rules.skills->healthPotion.id,tick));
    const bool withdrawMana=record.mana>=values.maxManaFixed && manaRateNow>0 &&
        (posted.findState(player->rules.skills->manaPotion.id) || posted.hasState(player->rules.skills->manaPotion.id,tick));
    if(!withdrawLife && !withdrawMana) {
        // D2MOO PlrModes::EVENTS_{Hp,Stamina,Mana}Regen consume posted FullStats.
        // Resource writes do not
        // repost equipment/passives or rebuild the immutable stat-source graph.
        auto life=record.hp,stamina=record.stamina;
        if(hpRateNow) life=Fixed8::fromRaw(std::max(256,std::min(nativeAdd(life.raw(),hpRateNow),values.maxLifeFixed.raw())));
        advanceStamina(stamina,{values.maxStaminaFixed,values.staminaDrainPerFrame,values.staminaRecoveryBonus},
            {player->moving,player->runningNow,!player->moving && !ports_.skills.busy(id,tick),ports_.areas.at(player->area).definition.town});
        // Native ManaRegen clamps high first, then low, using the sampled
        // mana/max pair. Do not require ordered bounds via std::clamp.
        const auto manaRate=std::max(std::min(manaRateNow,nativeSubtract(values.maxManaFixed.raw(),record.mana.raw())),
            nativeSubtract(0,record.mana.raw()));
        const auto mana=Fixed8::fromRaw(nativeAdd(record.mana.raw(),manaRate));
        if(!ports_.frameEvents.validate(timer)) return {DomainStatus::Conflict,{}};
        const auto &area=ports_.areas.at(player->area);
        const auto result=ports_.transactions.resources({player->player,id,player->area,area.generation,0,tick},
            player->characterRevision,life,mana,stamina);
        if(result) ports_.frameEvents.publish(std::move(timer));
        return result;
    }
    auto next=current->second;if(player->transient.effects) next.states=*player->transient.effects;
    auto &states=next.states;const auto &rules=*player->rules.character->stats;
    PlayerResourceTrace resources(*player,tick,{},ports_.areas.statTime(player->area));
    std::vector<RemovedCombatEffect> removed;
    const auto remove=[&](int state) {
        auto entries=states.removeState(state,[&]{resources.sample(states,true);});
        removed.insert(removed.end(),entries.begin(),entries.end());
    };
    // PlrModes::StatRegen invokes HP, stamina, then mana. Each withdrawal
    // runs the native source/passive callbacks before the following consumer.
    const auto hpRate=nativeStatValue(resources.totals().nativeStats,rules,"hpregen");
    if(hpRate) {
        const auto before=resources.record();const auto maximum=resources.maximum().life.raw();
        const auto raw=nativeAdd(before.hp.raw(),hpRate);
        // HpRegen caches the result before withdrawing HEALTHPOT, but writes
        // HITPOINTS after its callbacks. Preserve their other resource writes.
        if(raw>maximum) remove(player->rules.skills->healthPotion.id);
        const auto record=resources.record();
        resources.seedResources(Fixed8::fromRaw(std::max(256,std::min(raw,maximum))),record.mana,record.stamina);
    }
    {
        auto record=resources.record();const auto &values=resources.totals().character;
        advanceStamina(record.stamina,{values.maxStaminaFixed,values.staminaDrainPerFrame,values.staminaRecoveryBonus},
            {player->moving,player->runningNow,!player->moving && !ports_.skills.busy(id,tick),ports_.areas.at(player->area).definition.town});
        resources.seedResources(record.hp,record.mana,record.stamina);
    }
    {
        const auto before=resources.record();const auto maximum=resources.maximum().mana.raw();
        const auto recovery=nativeStatValue(resources.totals().nativeStats,rules,"manarecovery");
        const bool suppress=states.hasState(player->rules.character->noManaRegenState,tick);
        auto rate=nativeAdd(recovery,suppress?0:resources.totals().character.manaRegenPerFrame);
        if(before.mana.raw()>=maximum && rate>0) remove(player->rules.skills->manaPotion.id);
        rate=std::max(std::min(rate,nativeSubtract(maximum,before.mana.raw())),nativeSubtract(0,before.mana.raw()));
        // ManaRegen adds its cached/clamped delta to the current MANA after
        // MANAPOT callbacks; it does not overwrite their result with old mana.
        if(rate) {
            const auto record=resources.record();
            resources.seedResources(record.hp,Fixed8::fromRaw(nativeAdd(record.mana.raw(),rate)),record.stamina);
        }
    }
    resources.sample(states);const auto &area=ports_.areas.at(player->area);
    transactions::CharacterEdit edit{{player->player,id,player->area,area.generation,0,tick},player->inventoryRevision,player->characterRevision,resources.record()};
    edit.transient=projectEffectAttributes(states,tick);edit.resourceBase=resources.maximum();edit.petLimitChanges=resources.petLimits();
    for(const auto &effect:removed) if(!states.hasState(effect.effect.spec.state.id,tick))
        edit.publicFacts.emplace_back(StateFact{id,0,player->area,effect.effect.spec.state.id,false});
    if(!ports_.frameEvents.validate(timer)) return {DomainStatus::Conflict,{}};
    auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));if(!result) return result;
    if(player->transient.effects) next.states=*player->transient.effects;
    if(current->second.poisonRevision>next.poisonRevision) {next.poison=current->second.poison;next.poisonRevision=current->second.poisonRevision;}
    std::swap(current->second,next);ports_.frameEvents.publish(std::move(timer));return result;
}
}
