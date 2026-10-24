#include "corpses.hpp"
#include "execution.hpp"
#include "../system.hpp"
#include "../native_caster.hpp"
#include "server/systems/actions/release.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/missiles/native_birth.hpp"
#include "gameplay/skills/pd2_program.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::skills {
DomainStatus pd2Desecrate(const Ports &ports,actions::SkillRelease &pending,const ActorContext &actor,Vec target) {
    const auto caster=nativeCaster(ports.players,ports.monsters,actor.actor);
    if(!caster || !caster->programs()) return DomainStatus::InvalidActor;
    if(!pending.pd2Execution) pending.pd2Execution=std::make_shared<Pd2CastExecution>();
    auto &retained=pending.pd2Execution->desecrate;
    const auto &area=ports.areas.at(actor.area).definition;
    if(!retained) {
        auto input=caster->formulaInput(ports.effects,actor.tick);SkillFormulaValues values(*caster->programs()->formulas,input);
        const auto count=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"calc1");
        if(!count) throw std::logic_error("Missing PD2 Desecrate corpse count");
        if(area.pd2CorpsePools.empty()) return DomainStatus::NotImplemented;
        Pd2CastExecution::Desecrate execution;execution.position=target;execution.count=std::max(*count,0);
        const auto populated=[](const auto &pool){return std::any_of(pool.begin(),pool.end(),[](int id){return id>=0;});};
        size_t pool=0;
        if(!populated(area.pd2CorpsePools.front())) {
            if(std::none_of(area.pd2CorpsePools.begin()+1,area.pd2CorpsePools.end(),populated)) return DomainStatus::Unavailable;
            do {pool=1+pd2CrtRandom(ports.pd2Random)%uint32_t(area.pd2CorpsePools.size()-1);} while(!populated(area.pd2CorpsePools[pool]));
        }
        execution.pool=area.pd2CorpsePools[pool];retained=std::move(execution);
    }
    auto &execution=*retained;pending.nativeCallbackStarted=true;
    while(execution.next<execution.count) {
        if(!execution.definition) {
            int definition=-1;
            do {definition=execution.pool[pd2CrtRandom(ports.pd2Random)%25];} while(definition<0);
            execution.definition=definition;
            if(execution.next==1) execution.position=execution.position+Vec{2,2};
            else if(execution.next==2) execution.position=execution.position+Vec{-2,2};
            else if(execution.next>2) execution.position=execution.position+Vec{float(int(pd2CrtRandom(ports.pd2Random)&1)-2),float(int(pd2CrtRandom(ports.pd2Random)&1)-2)};
        }
        if(!execution.corpse) {
            const auto prototype=area.nativeMonsterClasses.find(*execution.definition);
            if(prototype==area.nativeMonsterClasses.end()) return DomainStatus::NotImplemented;
            auto identity=prototype->second.identity;identity.spawnKey="pd2-corpse."+std::to_string(actor.actor.value)+"."+std::to_string(actor.tick)+"."+std::to_string(execution.next);
            identity.ownerSpawnKey.clear();identity.origin=SpawnOrigin::Summoned;
            auto rule=prototype->second.rule;rule.noExperience=rule.noTreasure=true;rule.enchantment.reset();
            monsters::Admission request{identity,prototype->second.implementation,actor.area,execution.position,true,std::move(rule),{},actor.tick};
            request.deadMode=request.uncounted=true;
            const auto born=ports.monsters.admit(request);if(!born) return born.status;
            execution.corpse=*born.value;
        }
        const auto *body=ports.monsters.find(execution.corpse);if(!body) return DomainStatus::Stale;
        auto corpse=*body;corpse.nativeOwner=actor.actor;++corpse.revision;
        auto publication=ports.monsters.prepareActorPublication({{corpse.id,std::move(corpse)}},0);
        auto timer=ports.frameEvents.prepareInsertion({{}, {FrameEventUnit::Monster,body->id},FrameEventType::MonsterModifier,
            actor.tick+400,false,pd2CorpseTimerHandler});
        if(!ports.monsters.validateActors(publication)) return DomainStatus::Stale;
        ports.monsters.publishActors(publication);ports.frameEvents.publish(std::move(timer));
        ++execution.next;execution.definition.reset();execution.corpse={};
    }
    if(!execution.cloud) {
        const auto missile=pending.skill.pd2->missiles[0];if(missile<0) return DomainStatus::Unavailable;
        std::vector<missiles::NativeSpawn> cloud;
        for(int direction=0;direction<64;direction+=8)
            cloud.push_back({actor.actor,actor.area,actor.areaGeneration,actor.tick,target,target+missileRingDirection(direction),missile,
                pending.skill.sourceId,pending.skill.rank});
        auto birth=ports.missiles.prepareNativeBirth(cloud);if(!birth) return birth.status;
        if(caster->player && !pending.manaPaid) {
            const auto &owner=*ports.players.find(actor.player);
            transactions::CharacterEdit payment{actor,owner.inventoryRevision,owner.characterRevision,owner.persistent.player};
            payment.player.mana-=pending.skill.manaCost;
            payment.skillPayment=transactions::SkillManaPayment{pending.skill.manaCost,pending.skill.sourceId};payment.charge=pending.skill.charge;
            auto plan=ports.transactions.prepare(std::move(payment));if(!plan) return plan.status;
            plan.value->missileBirth=std::move(*birth.value);
            const auto committed=ports.transactions.commit(std::move(*plan.value));if(!committed) return committed.status;
            pending.manaPaid=true;
        } else {const auto committed=ports.missiles.commitNativeBirth(*birth.value);if(!committed) return committed.status;}
        execution.cloud=true;
    }
    pending.nativeCallbackStarted=false;return DomainStatus::Applied;
}
DomainResult<> pd2CorpseEvent(const Ports &ports,const FrameEvent &event,uint64_t tick) {
    if(ports.monsters.selectableCorpse(event.owner.unit,tick)) {
        const auto hidden=ports.effects.setNativeSkillState(event.owner.unit,104,true,tick);if(!hidden) return hidden;
        ports.monsters.consumeCorpse(event.owner.unit);
    }
    ports.frameEvents.cancel(event.handle);return {DomainStatus::Applied,std::monostate{}};
}
}
