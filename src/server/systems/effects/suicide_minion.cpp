#include "system.hpp"
#include "native_area.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/combat/requests.hpp"
#include "gameplay/monsters/mode_stats.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server::effects {
DomainResult<> System::suicideModeChange(EntityId id,uint64_t tick) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    const auto *unit=ports_.monsters.find(id);if(!unit) return done();
    const auto contains=[](const auto &values){return std::ranges::find(values,33)!=values.end();};
    if(!contains(unit->rule.nativeModifiers) && !(unit->rule.enchantment && contains(unit->rule.enchantment->ids))) return done();
    auto &cycle=uniqueCycles_[id];
    if(unit->alive() && unit->reactionMode==3 && unit->reactionUntil>tick && cycle.suicideHit!=unit->hitOccurrence) {
        cycle.suicideHit=unit->hitOccurrence;cycle.suicideHitComplete=false;
        cycle.suicideTimer=std::make_shared<FrameEventQueue::PreparedInsertion>(ports_.frameEvents.prepareInsertion(
            {{},{FrameEventUnit::Monster,id},FrameEventType::MonsterModifier,tick+4,false,monsterModifierTimerHandler}));
    }
    if(!cycle.suicideHit || cycle.suicideHitComplete) return done();
    const auto killed=ports_.monsters.scriptDeath(id,tick);if(!killed) return killed;
    // Retail UMod33's GH branch nests DT's mode callback, then schedules its
    // own MONUMOD after that call. Both original +4 timers must survive.
    ports_.frameEvents.publish(std::move(*cycle.suicideTimer));cycle.suicideTimer.reset();cycle.suicideHitComplete=true;
    return done();
}
DomainResult<> System::suicideDeathEffect(EntityId id,uint64_t tick,ModifierExecution &execution) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    const auto *unit=ports_.monsters.find(id);if(!unit) return done();
    const auto &rule=unit->rule;const auto &area=ports_.areas.at(unit->area);
    if(!execution.damagePrepared) {
        const auto sampled=unitStatValues(id,tick);if(!sampled || !rule.stats) return {DomainStatus::Unavailable,{}};
        const int level=logicalStatValue(*sampled,*rule.stats,"level");
        const auto table=rule.modeTables.at(size_t(rule.difficulty));if(!table) return {DomainStatus::Unavailable,{}};
        const auto bounds=monsterModePhysicalBounds(*table,0,level,rule.difficulty,rule.statGameType);
        const auto expected=unit->combatRandom;auto random=expected;
        const int damage=nativeShiftLeft(nativeAdd(bounds.minimum,int32_t(limitedRandom(random,uint32_t(std::max(0,nativeSubtract(bounds.maximum,bounds.minimum)))))),8);
        NativeAreaSpec spec;spec.source=id;spec.area=unit->area;spec.center=unit->position;
        spec.radius=rule.ai.params[3];spec.filter=0x581;spec.resultFlags=8;spec.rank=1;spec.radialBarrier=true;
        spec.damage.channels[0]=damage;
        if(rule.ai.params[0]==1) spec.damage.channels[2]=damage;
        else if(rule.ai.params[0]>=2) {spec.damage.channels[4]=damage;spec.damage.coldLength=rule.ai.params[0];}
        execution.area=std::make_shared<NativeAreaPulse>();execution.area->spec=std::move(spec);
        const auto consumed=ports_.monsters.consumeRandom(id,expected,random);if(!consumed) return consumed;
        execution.damagePrepared=true;
    }
    if(!execution.missile) {
        const auto definition=ports_.missiles.nativeDefinition(rule.ai.params[0]==1?"suicidefireexplode":rule.ai.params[0]<2?"suicidecorpseexplode":"suicideiceexplode");
        if(!definition) return {DomainStatus::Unavailable,{}};
        missiles::NativeSpawn request{id,unit->area,area.generation,tick,unit->position,unit->position,*definition,0,1};
        request.copyMonsterAttackBonus=true;
        const auto born=ports_.missiles.spawnNative(request);
        if(!born) return isDeferred(born.status) || born.status==DomainStatus::NotImplemented?DomainResult<>{born.status,{}}:done();
        execution.missile=*born.value;
    }
    if(!execution.area->spec.missileContact) {
        const auto contact=ports_.missiles.nativeAreaContact(execution.missile);if(!contact) return {DomainStatus::Unavailable,{}};
        execution.area->spec.missileContact=std::make_shared<combat::SpellImpact>(*contact);
    }
    return nativeArea(execution.area->spec,tick,execution.area);
}
}
