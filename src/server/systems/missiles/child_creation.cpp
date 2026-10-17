#include "system.hpp"
#include "runtime.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/effects/system.hpp"
#include "core/random.hpp"
#include <utility>
#include <stdexcept>

namespace d2x::server::missiles {
Missile &System::planChild(Advance &plan,Spawn source,Vec origin,Vec direction,int definition,int frames,float speed,Program program,
    bool refreshSkill,ChildSetup setup) const {
    // A blueprint carries geometry and explicit script parameters. It does
    // not allocate a native unit or consume any live/parent seed.
    source.sourceOwner=plan.next.owner;source.preparedRules=plan.next.rules;source.actor.actor=plan.next.playerActor;
    uint64_t unused{};
    auto prototype=make(source,origin,direction,definition,frames,speed,program,unused,true);
    source.cost.reset();source.sourceOwner=plan.next.owner;source.preparedRules=plan.next.rules;
    const auto parent=plan.next;const auto parentRolls=plan.damageRolls;
    plan.addChild(std::move(prototype),[this,source=std::move(source),origin,direction,definition,frames,speed,program,refreshSkill,
        setup=std::move(setup),parent,parentRolls](const Missile &parameters,Advance::ChildBuildContext &context)->std::optional<Missile> {
        const auto tick=context.tick;auto &gameRandom=context.gameRandom;
        const auto *owner=ports_.players.find(source.actor.player);
        const bool playerOwner=owner && owner->entered && owner->actor==source.actor.actor && source.sourceOwner==owner->actor;
        const auto *unit=ports_.monsters.find(source.sourceOwner);
        if(!playerOwner && !unit) return {};
        auto request=source;request.actor.tick=tick;
        if(refreshSkill && playerOwner) request.skill=skills::evaluate(*owner,source.skill.sourceId,source.skill.rank);
        const auto expected=unit?unit->combatRandom:owner->combatRandom;auto ownerRandom=expected;
        const auto originExpected=parentRolls?parentRolls->random:parent.random;auto originRandom=originExpected;
        auto child=make(request,origin,direction,definition,frames,speed,program,gameRandom,false,&ownerRandom,&parent,&originRandom);
        if(ownerRandom!=expected) context.ownerSeed=Advance::OwnerSeedLoan{source.sourceOwner,expected,ownerRandom};
        if(originRandom!=originExpected) {
            if(!parentRolls) throw std::logic_error("Native child requires a retained origin seed");
            context.originSeed=Advance::OriginSeedLoan{parentRolls,originExpected,originRandom};
        }
        // These are script-owned trajectory/chain parameters, independent of
        // the sampled damage inputs and the newly allocated child's seed.
        child.remainingHits=parameters.remainingHits;child.lastHit=parameters.lastHit;child.directionIndex=parameters.directionIndex;
        child.parameters=parameters.parameters;child.turnTarget=parameters.turnTarget;
        child.pathTarget=parameters.pathTarget;
        if(child.pathInitialization.kind==MissilePathInitialization::Kind::None) child.path=parameters.path;
        child.guidance=parameters.guidance;
        if(setup) setup(child,request.skill);
        return child;
    });
    return plan.children.back();
}
Missile &System::planMonsterChild(Advance &plan,Missile prototype) const {
    // Every native child constructor calculates its own ranges. The parent is
    // an explicit origin stat/seed source only when SrcMissDmg requests it.
    const auto origin=plan.next;const auto originRolls=plan.damageRolls;
    prototype.nativeResources={};
    plan.addChild(std::move(prototype),[this,origin,originRolls](const Missile &parameters,Advance::ChildBuildContext &context)->std::optional<Missile> {
        const auto tick=context.tick;auto &gameRandom=context.gameRandom;
        auto child=parameters;
        auto sampled=sampleMonsterRule(child.enemy->rule,child.owner,tick,{},child.skill.rank);
        if(!sampled) throw std::runtime_error("Unavailable native monster child missile inputs");
        child.enemy->rule=std::move(*sampled.value);const auto &rule=child.enemy->rule;
        child.random=childRandom(gameRandom);child.acceleration=rule.acceleration;child.maximumVelocity=rule.maximumVelocity;
        child.motion.reset();child.pathRevision=0;
        child.created=child.executionTick=tick;
        if(!child.nativeProgram) child.initializeFrames(child.lifetimeFrames,rule.activate);
        if(child.nativeProgram && rule.canSlow) {
            const auto slow=ports_.effects.combatModifiers(child.owner,tick).slowMissiles;
            if(slow) child.velocity=child.velocity*(float(slow)/100.f);
        }
        initializePath(child);
        const auto *monster=ports_.monsters.find(child.owner);const auto *player=ports_.players.findActor(child.owner);
        if(!monster && !player) return {};
        const auto expected=monster?monster->combatRandom:player?player->combatRandom:uint64_t{};
        auto ownerRandom=expected;bool ownerDrawn{};
        const auto originExpected=originRolls?originRolls->random:origin.random;auto originRandom=originExpected;
        NativeDamage prepared;prepared.rule=rule;prepared.attackBonus=child.enemy->rating;
        prepared.data=prepareDamage(child,rule,{},ownerRandom,ownerDrawn,&origin,std::ref(originRandom),&prepared.monsterTypeBonuses);
        child.nativeDamage=std::move(prepared);initializePierce(child);
        child.enemy->criticalChance=monster?monster->rule.criticalChance:0;
        if(ownerDrawn) context.ownerSeed=Advance::OwnerSeedLoan{child.owner,expected,ownerRandom};
        if(originRandom!=originExpected) {
            if(!originRolls) throw std::runtime_error("Native origin missile requires retained damage seed");
            context.originSeed=Advance::OriginSeedLoan{originRolls,originExpected,originRandom};
        }
        child.enemy->hit={};
        child.enemy->weapon.reset();
        return child;
    });
    return plan.children.back();
}

}
