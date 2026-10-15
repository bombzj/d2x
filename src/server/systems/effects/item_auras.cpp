#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/skills/aura_resolve.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <utility>

namespace d2x::server::effects {
DomainResult<> System::itemAuraEvent(const FrameEvent &event,uint64_t tick) {
    const auto id=event.owner.unit;
    const bool monster=event.owner.type==FrameEventUnit::Monster;
    const auto *player=monster?nullptr:ports_.players.findActor(id);
    const auto *unit=monster?ports_.monsters.find(id):nullptr;
    if((!player || !player->entered) && !unit) {
        ports_.frameEvents.cancelOwner(event.owner);return {DomainStatus::InvalidActor,{}};
    }
    const EntityId source{uint32_t(event.customId)};
    const auto key=std::pair{id,source};
    auto &cycle=itemAuraCycles_[key];
    if(cycle.eventOrder!=event.payload) {cycle=PaladinCycle{};cycle.eventOrder=event.payload;}
    if(!cycle.itemTimer) {
        const auto &timers=player?player->totals.itemAuraTimers:unit->itemAuraTimers;
        const auto found=std::find_if(timers.begin(),timers.end(),[&](const auto &timer) {
            return timer.source==source && timer.skill==event.customParameter && timer.order==event.payload;
        });
        if(found==timers.end()) {ports_.frameEvents.cancel(event.handle);itemAuraCycles_.erase(key);return {DomainStatus::Applied,std::monostate{}};}
        cycle.itemTimer=*found;
    }
    const auto &timer=*cycle.itemTimer;
    const auto region=player?player->area:unit->area;
    const ActorContext actor{player?player->player:unit->owner.value_or(PlayerId{}),id,region,ports_.areas.at(region).generation,0,tick};
    const auto finish=[&](std::optional<ItemAuraTimer> replacement)->DomainResult<> {
        if(player) {
            transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,player->persistent.player};
            edit.itemAuraTimer=transactions::ItemAuraTimerCompletion{timer.source,std::move(replacement)};
            auto plan=ports_.transactions.prepare(std::move(edit));
            return plan?ports_.transactions.commit(std::move(*plan.value)):DomainResult<>{plan.status,{}};
        }
        auto next=unit->itemAuraTimers;
        std::erase_if(next,[&](const auto &current){return current.source==timer.source;});
        if(replacement) next.push_back(*replacement);
        monsters::LifeChange change{id,unit->revision,Fixed8::fromRaw(nativeChecked(unit->life)),
            Fixed8::fromRaw(nativeChecked(unit->maximumLife)),unit->nativeStats,true};
        change.itemAuraTimers=std::move(next);
        change.periodicEvents=prepareItemAuraEvents(event.owner,*change.itemAuraTimers);
        return ports_.monsters.commitLife(std::move(change))?DomainResult<>{DomainStatus::Applied,std::monostate{}}:DomainResult<>{DomainStatus::Stale,{}};
    };
    if(!cycle.itemDecisionPrepared) {
        // The default monster event table suppresses PeriodicStats while
        // living/frozen. Timed callbacks are consumed; continuous ones wait.
        if(unit && unit->alive() && ports_.monsters.frozen(unit->id,tick)) {
            if(event.continuous) return {DomainStatus::Applied,std::monostate{}};
            cycle.itemCancel=true;
        } else {
            int rank=0;
            if(player && player->persistent.player.hp>0 && player->rules.character && player->rules.character->stats)
                rank=itemAuraTimerRank(*player->rules.character->stats,player->totals.nativeStats,timer);
            if(unit && unit->alive() && unit->rule.stats) {
                const auto stats=unitStatValues(id,tick);
                if(stats) rank=itemAuraTimerRank(*unit->rule.stats,*stats,timer);
            }
            cycle.itemCancel=rank<=0 || timer.state<0;
            if(!cycle.itemCancel) {
                if((player && (!player->rules.skills || !player->rules.skills->auras.contains(timer.skill))) ||
                   (unit && (!unit->rule.auraPrograms || !unit->rule.auraPrograms->contains(timer.skill)))) {
                    state_.itemAuraDeferred="Original item aura program is not implemented: "+std::to_string(timer.skill);
                    return {DomainStatus::NotImplemented,{}};
                }
                cycle.skill=timer.skill;cycle.rank=rank;cycle.area=region;cycle.next=timer.next;
                if(player) cycle.random=player->combatRandom;
            }
        }
        cycle.itemDecisionPrepared=true;
    }
    if(cycle.itemCancel) {
        const auto result=finish({});if(!result) return result;
        itemAuraCycles_.erase(key);return result;
    }
    // Forced periodic execution preserves used/left/right selections. Each
    // source GUID owns its callback even when several sources share a layer.
    const auto status=player?advanceAuraCycle(actor,*player,player->rules.skills->auras.at(cycle.skill).spec,cycle,tick):
        advanceUnitAuraCycle(actor,id,cycle,tick);
    if(status==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
    if(!cycle.itemReplacementPrepared) {
        auto replacement=timer;
        int32_t period{};
        if(player) {
            auto input=skills::formulaInput(*player);SkillFormulaValues values(*player->rules.skills->formulas,input);
            const auto delay=values.calculation(FormulaDomain::Skill,cycle.skill,cycle.rank,"perdelay");
            if(!delay) return {DomainStatus::Unavailable,{}};
            period=*delay;
        } else period=evaluateUnitAura(id,cycle.skill,cycle.rank,tick).periodFrames;
        replacement.next=skillPeriodicTick(tick,period);
        replacement.order=reserveAuraEventOrder();replacement.continuous=skillPeriodicFrame(nativeNarrow(uint32_t(tick)),period)==-1;
        cycle.itemReplacement=replacement;cycle.itemReplacementPrepared=true;
    }
    const bool alive=player?player->persistent.player.hp>0:unit->alive();
    const auto result=finish(alive?cycle.itemReplacement:std::nullopt);if(!result) return result;
    itemAuraCycles_.erase(key);return result;
}
}
