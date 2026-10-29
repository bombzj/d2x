#include "system.hpp"
#include "attribute_projection.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <stdexcept>
namespace d2x::server::effects {
DomainResult<> System::nativeMissileHitDelay(EntityId target,const CombatStateDefinition &definition,uint64_t duration,uint64_t tick) {
    if(definition.id<0) return {DomainStatus::InvalidRequest,{}};
    CombatEffectSpec effect;effect.state=definition;
    effect.source={CombatEffectSource::Environment,target,definition.id,0};
    effect.duration=duration;effect.nativeState=true;effect.nativeMissileHitGate=true;
    effect.nativeValues=std::map<StatKey,int32_t>{};effect.stacking=EffectStacking::Independent;
    effect.nativeExpiration=NativeStateExpiration::EvenZeroDuration;
    if(const auto *player=ports_.players.findActor(target)) {
        const auto *area=ports_.areas.find(player->area);if(!player->entered || !area) return {DomainStatus::InvalidActor,{}};
        auto states=state_.players.contains(target)?state_.players.at(target).states:CombatEffectSet{};
        PlayerResourceTrace resources(*player,tick,{},ports_.areas.statTime(player->area));
        states.apply(effect,tick,[&]{resources.sample(states,true);});resources.sample(states);
        transactions::CharacterEdit edit{{player->player,player->actor,player->area,area->generation,0,tick},
            player->inventoryRevision,player->characterRevision,resources.record()};
        edit.transient=projectEffectAttributes(states,tick);edit.resourceBase=resources.maximum();edit.petLimitChanges=resources.petLimits();
        if(!definition.noSend) edit.publicFacts.emplace_back(StateFact{target,0,player->area,definition.id,true});
        auto prepared=ports_.transactions.prepare(std::move(edit));
        return prepared?ports_.transactions.commit(std::move(*prepared.value)):DomainResult<>{prepared.status,{}};
    }
    const auto *monster=ports_.monsters.find(target);if(!monster) return {DomainStatus::InvalidActor,{}};
    auto unit=units_.contains(target)?units_.at(target):UnitEffect{};unit.area=monster->area;
    MonsterResourceTrace resources(*monster,tick,&unit.states);
    unit.states.apply(effect,tick,[&]{resources.sample(unit.states,true);});resources.sample(unit.states);
    AuraUnitPublication plan;
    if(!definition.noSend) plan.facts.emplace_back(StateFact{target,1,monster->area,definition.id,true});
    prepareUnitEffects(plan,target,unit,resources.take(),tick);
    if(!validateAuraUnits(plan)) return {DomainStatus::Stale,{}};
    if(!plan.facts.empty() && !ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},monster->area},std::move(plan.facts)})) return {DomainStatus::Capacity,{}};
    publishAuraUnits(plan);return {DomainStatus::Applied,std::monostate{}};
}
}
