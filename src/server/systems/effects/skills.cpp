#include "system.hpp"
#include "server/systems/skills/shape.hpp"
#include "gameplay/actions/shape_timing.hpp"
#include "damage_absorption.hpp"
#include "core/random.hpp"
#include "gameplay/units/direction.hpp"
#include "attribute_projection.hpp"
#include "gameplay/combat/shield.hpp"
#include "gameplay/effects/native_status.hpp"
#include "gameplay/combat/reception.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/actions/recovery.hpp"
#include <array>
#include "gameplay/combat/life.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/skills/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::effects {
namespace {
std::optional<AnimationTiming> reactionTiming(const PlayerState &player,std::string_view mode) {
    if(!player.rules.skills) return {};
    if(const auto *shape=skills::activeShape(*player.rules.skills,player.transient.states)) {
        const auto nativeMode=shapeMonsterMode(mode=="bl"?9:mode=="kb"?19:4,shape->enabledModes);
        const auto animation=shape->animations.find(nativeMode);if(animation==shape->animations.end()) return {};
        return recoveryAnimationTiming({{animation->second.frames,animation->second.speed},
            mode=="kb"?RecoveryMode::Knockback:mode=="bl"?RecoveryMode::Block:RecoveryMode::GetHit,
            player.totals.character.combat.fasterHitRecovery,player.totals.character.combat.fasterBlock,213,false});
    }
    const auto found=player.rules.skills->recoveryAnimations.find(std::string(mode=="kb"?"gh":mode)+player.totals.equipment.animationClass);
    if(found==player.rules.skills->recoveryAnimations.end()) return {};
    const bool block=mode=="bl";
    const bool holyShield=block && player.transient.states.contains(player.rules.skills->holyShield.id);
    return recoveryAnimationTiming({found->second,
        mode=="kb"?RecoveryMode::Knockback:block?RecoveryMode::Block:RecoveryMode::GetHit,
        player.totals.character.combat.fasterHitRecovery,player.totals.character.combat.fasterBlock,213,holyShield});
}
}
bool System::recovering(EntityId id,uint64_t tick) const {
    return ports_.actions.recovering(id,tick);
}
DomainResult<> System::block(const ActorContext &actor) {
    const auto *player=ports_.players.find(actor.player);
    const auto *area=ports_.areas.find(actor.area);
    if(!player || !area || !player->entered || player->actor!=actor.actor || player->area!=actor.area ||
        area->generation!=actor.areaGeneration || player->persistent.player.hp<=0) return {DomainStatus::InvalidActor,{}};
    const auto timing=reactionTiming(*player,"bl");
    if(!timing || ports_.skills.uninterruptible(actor.actor)) return {DomainStatus::Applied,std::monostate{}};
    auto next=ports_.actions.prepareRecovery(actor.actor);
    auto &recovery=next.mapped();
    // SUNITDMG: repeated successful blocks suppress BL, never the block itself.
    const uint64_t delay=uint64_t(std::max(0,player->totals.character.combat.fasterBlock/8+15));
    if(actor.tick<=recovery.lastBlockTick || actor.tick-recovery.lastBlockTick<=delay) return {DomainStatus::Applied,std::monostate{}};
    const auto ended=endMode(actor.actor,actor.tick);if(!ended) return ended;
    HitFact fact{actor.actor,0,actor.area,playerLifePercentage(int64_t(player->persistent.player.hp.raw()),player->totals.character.maxLifeFixed.raw()),false,player->position};
    fact.playerMode=18;
    recovery.lastBlockTick=actor.tick;
    recovery.until=actor.tick+std::min<uint64_t>(timing->durationTicks(),UINT64_MAX-actor.tick);
    const auto result=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},{fact}});
    if(!result) return {result.status,{}};
    ports_.actions.commitRecovery(std::move(next));
    ports_.skills.interrupt(actor);
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::missileReaction(EntityId target,EntityId source,bool getHit,bool knockback,uint8_t hitClass,uint64_t tick) {
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto recipient=participants.find(target),attacker=participants.find(source);
    if(recipient.monster) return ports_.monsters.missileReaction(target,source,getHit,knockback,hitClass,tick);
    if(!recipient.player || !attacker.id()) return {DomainStatus::Unavailable,{}};
    const auto &player=*recipient.player;const auto &area=ports_.areas.at(player.area);
    if(!recipient.aliveIn(player.area) || ports_.skills.uninterruptible(target)) return {DomainStatus::Applied,std::monostate{}};
    // Failed area damage has no total damage and never executes HP/events.
    // Its explicit reaction still applies to a stunned unit or knockback.
    const auto *states=unitEffects(target);
    const bool stunned=states && states->hasState(player.rules.skills->stunned.id,tick);
    if(!knockback && (!getHit || !stunned)) return {DomainStatus::Applied,std::monostate{}};
    const auto timing=reactionTiming(player,knockback?"kb":"gh");
    if(!timing) return {DomainStatus::Applied,std::monostate{}};
    const auto ended=endMode(target,tick);if(!ended) return ended;
    auto recovery=ports_.actions.prepareRecovery(target);
    recovery.mapped().until=tick+std::min<uint64_t>(timing->durationTicks(),UINT64_MAX-tick);
    HitFact fact{target,0,player.area,playerLifePercentage(player.persistent.player.hp.raw(),player.totals.character.maxLifeFixed.raw()),false,player.position,hitClass};
    fact.playerMode=knockback?20:6;
    if(knockback) {
        const auto attackerArea=attacker.player?attacker.player->area:attacker.monster->area;
        const auto point=attacker.position()+ports_.areas.at(attackerArea).definition.origin-area.definition.origin;
        const auto destination=knockbackDestination(player.position,point,3);
        transactions::CharacterEdit edit{{player.player,target,player.area,area.generation,0,tick},player.inventoryRevision,player.characterRevision,player.persistent.player};
        if(area.definition.collision.nativeMovementSegment(player.position,destination,playerMovement)) {
            edit.knockback=PointTarget{player.area,area.generation,destination};fact.knockback=destination;
        }
        edit.publicFacts.emplace_back(fact);
        auto prepared=ports_.transactions.prepare(std::move(edit));if(!prepared) return {prepared.status,{}};
        const auto committed=ports_.transactions.commit(std::move(*prepared.value));if(!committed) return committed;
    } else {
        const auto published=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},player.area},{fact}});
        if(!published) return {published.status,{}};
    }
    ports_.actions.commitRecovery(std::move(recovery));
    ports_.skills.interrupt({player.player,target,player.area,area.generation,0,tick});
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::skill(const ActorContext &actor, const SkillCastSpec &skill, std::optional<PlayerId> recipient,std::span<const DomainFact> facts) {
    const auto *caster=ports_.players.find(actor.player);const auto *target=ports_.players.find(recipient.value_or(actor.player));
    if(!caster || !target || !caster->entered || !target->entered || caster->actor!=actor.actor || caster->area!=actor.area ||
        ports_.areas.at(target->area).definition.act!=ports_.areas.at(actor.area).definition.act || caster->persistent.player.hp<=0 || target->persistent.player.hp<=0 || !skill.appliedEffect ||
        caster->persistent.player.mana<skill.manaCost) return {DomainStatus::InvalidActor,{}};
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    // SrvDo025: an unaligned Enchant target falls back to the caster.
    if(skill.effect==SkillBehavior::Enchant && !participants.auraAligned({caster,nullptr},{target,nullptr})) target=caster;
    auto next=state_;auto &recovery=next.players[target->actor];
    auto spec=*skill.appliedEffect;spec.source={CombatEffectSource::Skill,caster->actor,skill.sourceId,skill.rank};
    PlayerResourceTrace resources(*target,actor.tick,target==caster?skill.manaCost:Fixed8{},ports_.areas.statTime(target->area));
    const auto application=applySkillState(recovery.states,skill,spec,*caster->rules.skills->stats,*target->rules.character->states,actor.tick,[&]{resources.sample(recovery.states,true);});
    if(!application.accepted) return {DomainStatus::Conflict,{}};
    const auto *installed=recovery.states.find(application.handle);
    if(!installed) throw std::runtime_error("Missing installed native player skill state");
    const auto &area=ports_.areas.at(target->area);
    const ActorContext targetActor{target->player,target->actor,target->area,area.generation,0,actor.tick};
    transactions::CharacterEdit edit{targetActor,target->inventoryRevision,target->characterRevision,resources.record()};
    edit.transient=projectEffectAttributes(recovery.states,actor.tick);
    edit.resourceBase=resources.maximum();
    for(const int state:application.clearedStates) if(!recovery.states.hasState(state,actor.tick)) edit.publicFacts.emplace_back(StateFact{target->actor,0,target->area,state,false});
    for(const auto &removed:application.removed) if(!recovery.states.hasState(removed.effect.spec.state.id,actor.tick))
        edit.publicFacts.emplace_back(StateFact{target->actor,0,target->area,removed.effect.spec.state.id,false});
    if(skill.effect!=SkillBehavior::BoneArmor) {
        auto stats=nativeEffectStats(*caster->rules.skills,installed->spec);
        if(!installed->spec.nativeValues) {
            stats.emplace_back(caster->rules.skills->nativeStats.at("modifierlist_skill"),skill.sourceId);
            stats.emplace_back(caster->rules.skills->nativeStats.at("modifierlist_level"),skill.rank);
        }
        edit.publicFacts.emplace_back(StateFact{target->actor,0,target->area,skill.appliedEffect->state.id,true,std::move(stats)});
    }
    if(skill.effect==SkillBehavior::BoneArmor) edit.publicFacts.emplace_back(StateFact{target->actor,0,target->area,skill.appliedEffect->state.id,true,
        {{caster->rules.skills->nativeStats.at("bonearmor"),installed->spec.physicalShield},
         {caster->rules.skills->nativeStats.at("bonearmormax"),installed->spec.physicalShieldMaximum}}});
    if(skill.effect==SkillBehavior::BoneArmor && skill.castOverlayId>=0) edit.publicFacts.emplace_back(OverlayFact{target->actor,0,target->area,skill.castOverlayId});
    std::vector<transactions::Change> changesToPrepare;
    if(target==caster) {edit.charge=skill.charge;edit.publicFacts.insert(edit.publicFacts.end(),facts.begin(),facts.end());}
    else {
        transactions::CharacterEdit debit{actor,caster->inventoryRevision,caster->characterRevision,caster->persistent.player};
        debit.player.mana-=skill.manaCost;debit.charge=skill.charge;
        debit.publicFacts.insert(debit.publicFacts.end(),facts.begin(),facts.end());
        changesToPrepare.emplace_back(std::move(debit));
    }
    changesToPrepare.emplace_back(std::move(edit));
    auto prepared=ports_.transactions.prepareGroup(std::move(changesToPrepare));if(!prepared) return {prepared.status,{}};
    auto plans=std::move(*prepared.value);
    const auto result=plans.size()==1?ports_.transactions.commit(std::move(plans.front())):ports_.transactions.commitCharacters(std::move(plans));if(result) publishPlayers(next.players);
    return result;
}
DomainResult<float> System::receive(const ActorContext &actor, int64_t raw, DamageType type) {
    MonsterHit hit; hit.channels[size_t(type)] = raw;
    const auto result=receiveMonster(actor, {}, hit, {});
    return {result.status,result?std::optional{result.value->dealt}:std::nullopt};
}
DomainResult<PlayerReceptionPlan> System::preparePlayerReception(const ActorContext &actor,EntityId source,const MonsterHit &hit,const PlayerHitAdjustment &adjustment) const {
    const auto *p = ports_.players.find(actor.player);
    if (!p || !p->entered || p->actor != actor.actor || p->area != actor.area || p->persistent.player.hp <= 0)
        return {DomainStatus::InvalidActor,{}};
    if(hit.lifeDrain<INT32_MIN || hit.lifeDrain>INT32_MAX || hit.mana<INT32_MIN || hit.mana>INT32_MAX || hit.stamina<INT32_MIN || hit.stamina>INT32_MAX ||
        std::any_of(hit.channels.begin(),hit.channels.end(),[](int64_t amount){return amount<INT32_MIN || amount>INT32_MAX;}))
        return {DomainStatus::InvalidRequest,{}};
    PlayerReceptionPlan plan;plan.actor=actor;plan.inventoryRevision=p->inventoryRevision;plan.characterRevision=p->characterRevision;
    if(const auto found=state_.players.find(actor.actor);found!=state_.players.end()) plan.recovery=found->second;
    auto &recovery=plan.recovery;auto &states=recovery.states;
    plan.character=p->persistent.player;
    PlayerResourceTrace resources(*p,actor.tick,{},ports_.areas.statTime(p->area));
    const auto observe=[&]{resources.sample(states,true);};
    auto defenses=p->totals.character;
    const auto attacker=combat::Participants{ports_.players,ports_.monsters,ports_.social}.find(source);
    auto sourceModifiers=attacker.player?attacker.player->totals.character.combat:attacker.monster?attacker.monster->petStats.attributes.combat:CombatModifiers{};
    if(attacker.monster) mergeCombatModifiers(sourceModifiers,unitModifiers(attacker.monster->id,actor.tick).combat);
    if(!defenses.rawResistances) {
        std::array<int,6> raw{};for(size_t channel=0;channel<raw.size();++channel) raw[channel]=rawResistance(defenses,DamageType(channel));
        defenses.rawResistances=raw;
    }
    defenses.rawResistances->at(2)=nativeSubtract(defenses.rawResistances->at(2),sourceModifiers.firePierce);
    defenses.rawResistances->at(3)=nativeSubtract(defenses.rawResistances->at(3),sourceModifiers.lightningPierce);
    defenses.rawResistances->at(4)=nativeSubtract(defenses.rawResistances->at(4),adjustment.coldPierce?adjustment.coldPierce:sourceModifiers.coldPierce);
    const auto poisonPierce=adjustment.poisonPierce?adjustment.poisonPierce:sourceModifiers.poisonPierce;
    defenses.rawResistances->at(5)=nativeSubtract(defenses.rawResistances->at(5),poisonPierce);
    defenses.coldResist=damageMitigation(DamageType::Cold,defenses).resistance;
    defenses.poisonResist=damageMitigation(DamageType::Poison,defenses).resistance;
    DamageReception absorbed;absorbed.mana=plan.character.mana.raw();
    const bool shieldDrain=!source || (attacker.monster && !attacker.monster->hireling);
    absorbed.drain={std::max<int64_t>(0,hit.mana),std::max<int64_t>(0,hit.stamina),shieldDrain,std::max<int64_t>(0,hit.lifeDrain)};
    auto channels=hit.channels;std::set<int> changed;
    auto random=p->combatRandom;
    const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
        if(low>=high) return low;
        return nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
    };
    const auto formula=[&](int id,int rank,std::string_view field) {
        auto input=skills::formulaInput(*p,resources.record(),resources.totals(),draw);
        SkillFormulaValues values(*p->rules.skills->formulas,input);
        const auto value=values.calculation(FormulaDomain::Skill,id,rank,field);
        if(!value) throw std::logic_error("Missing native absorption formula");
        return *value;
    };
    const auto writeMana=[&](int32_t mana) {
        const auto record=resources.record();resources.seedResources(record.hp,Fixed8::fromRaw(mana),record.stamina);
    };
    const auto readMana=[&]{return resources.record().mana.raw();};
    const auto absorption=applyDamageAbsorption(states,channels,absorbed,*p->rules.skills->stats,*p->rules.skills->formulas,
        actor.tick,formula,observe,readMana,writeMana,changed);
    if(!absorption) return {absorption.status,{}};
    if(random!=p->combatRandom) plan.random=std::pair{p->combatRandom,random};
    const auto reception=receiveDamageChannels(channels,defenses,std::move(absorbed));
    if(attacker.id()) {
        const auto region=attacker.player?attacker.player->area:attacker.monster->area;
        const auto target=p->position+ports_.areas.at(p->area).definition.origin;
        const auto origin=attacker.position()+ports_.areas.at(region).definition.origin;
        const auto direction=nativeDirectionVector(int64_t(std::floor(origin.x))-int64_t(std::floor(target.x)),int64_t(std::floor(origin.y))-int64_t(std::floor(target.y))).index;
        for(const auto base:reception.directionalOverlays) plan.facts.emplace_back(OverlayFact{actor.actor,0,actor.area,base+nativeOverlayDirection(direction)});
    }
    plan.character=resources.record();
    for(const auto state:changed) {
        const auto *entry=states.findState(state);
        if(!entry) plan.facts.emplace_back(StateFact{actor.actor,0,actor.area,state,false});
        else plan.facts.emplace_back(StateFact{actor.actor,0,actor.area,state,true,nativeEffectStats(*p->rules.skills->stats,entry->spec)});
    }
    plan.character.mana=Fixed8::fromRaw(nativeChecked(reception.mana));
    plan.transient=projectEffectAttributes(states,actor.tick);plan.maximum=resources.maximum();
    plan.damage=reception;plan.damage.poisonPierce=poisonPierce;plan.defenses=std::move(defenses);plan.poisonPierce=poisonPierce;plan.totals=resources.totals();
    return {DomainStatus::Applied,std::move(plan)};
}
DomainResult<> System::commitPlayerReception(PlayerReceptionPlan &&plan) {
    const auto *player=ports_.players.find(plan.actor.player);
    if(!player || player->actor!=plan.actor.actor || player->inventoryRevision!=plan.inventoryRevision || player->characterRevision!=plan.characterRevision)
        return {DomainStatus::Stale,{}};
    const auto current=state_.players.find(plan.actor.actor);
    if(plan.facts.empty() && plan.character.hp==player->persistent.player.hp && plan.character.mana==player->persistent.player.mana &&
       plan.character.stamina==player->persistent.player.stamina && !plan.random &&
       (current==state_.players.end()?plan.recovery.states.size()==0:plan.recovery.states.sharesSnapshot(current->second.states)))
        return {DomainStatus::Applied,std::monostate{}};
    transactions::CharacterEdit edit{plan.actor,plan.inventoryRevision,plan.characterRevision,plan.character};
    edit.transient=std::move(plan.transient);edit.resourceBase=plan.maximum;edit.publicFacts=std::move(plan.facts);
    if(plan.random) edit.random=transactions::UnitRandomEdit{plan.random->first,plan.random->second};
    auto next=state_;next.players.insert_or_assign(plan.actor.actor,std::move(plan.recovery));
    auto prepared=ports_.transactions.prepare(std::move(edit));if(!prepared) return {prepared.status,{}};
    const auto result=ports_.transactions.commit(std::move(*prepared.value));if(result) publishPlayers(next.players);
    return result;
}
DomainResult<> System::nativeMeleeReaction(EntityId target,EntityId source,const MonsterHit &hit,int64_t amount,uint64_t tick) {
    const auto *player=ports_.players.findActor(target);if(!player || !player->entered) return {DomainStatus::Unavailable,{}};
    const auto &area=ports_.areas.at(player->area);
    if(player->persistent.player.hp<=0) {
        const auto output=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},player->area},
            {HitFact{target,0,player->area,0,true,player->position,hit.hitClass}}});
        return {output.status,output?std::optional{std::monostate{}}:std::nullopt};
    }
    const bool interruptible=!ports_.skills.uninterruptible(target);
    const auto clock=reactionTiming(*player,"gh"),knockbackClock=hit.knockback?reactionTiming(*player,"kb"):std::optional<AnimationTiming>{};
    const bool knockback=source && amount>0 && knockbackClock && interruptible;
    auto random=player->combatRandom;const auto states=unitEffects(target);
    const bool getHit=source && hit.getHit.value_or(!hit.softHit) && (amount>0 || hit.stunFrames) && !hit.knockback && clock && interruptible &&
        monsterHitRecovery(amount,player->totals.character.maxLifeFixed.raw(),hit.hitClass,false,
            hit.channels[5]>0 && std::all_of(hit.channels.begin(),hit.channels.begin()+5,[](auto n){return n==0;}),true,random,
            player->rules.skills && states && states->hasState(player->rules.skills->stunned.id,tick));
    if(!getHit && !knockback && random==player->combatRandom) return {DomainStatus::Applied,std::monostate{}};
    if(getHit || knockback) {const auto ended=endMode(target,tick);if(!ended) return ended;}
    const ActorContext actor{player->player,target,player->area,area.generation,0,tick};
    transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,player->persistent.player};
    edit.random=transactions::UnitRandomEdit{player->combatRandom,random};
    actions::State::PreparedRecovery recovery;
    if(getHit || knockback) {
        recovery=ports_.actions.prepareRecovery(target);
        recovery.mapped().until=tick+std::min<uint64_t>((knockback?knockbackClock:clock)->durationTicks(),UINT64_MAX-tick);
        HitFact fact{target,0,player->area,playerLifePercentage(player->persistent.player.hp.raw(),player->totals.character.maxLifeFixed.raw()),false,player->position,hit.hitClass};
        fact.playerMode=knockback?20:6;
        if(knockback) {
            const auto attacker=combat::Participants{ports_.players,ports_.monsters,ports_.social}.find(source);
            if(attacker.id()) {
                const auto region=attacker.player?attacker.player->area:attacker.monster->area;
                const auto point=attacker.position()+ports_.areas.at(region).definition.origin-area.definition.origin;
                const auto destination=knockbackDestination(player->position,point,3);
                if(area.definition.collision.nativeMovementSegment(player->position,destination,playerMovement)) {
                    edit.knockback=PointTarget{player->area,area.generation,destination};fact.knockback=destination;
                }
            }
        }
        edit.publicFacts.emplace_back(fact);
    }
    auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
    const auto committed=ports_.transactions.commit(std::move(*plan.value));if(!committed) return committed;
    if(getHit || knockback) {ports_.actions.commitRecovery(std::move(recovery));ports_.skills.interrupt(actor);}
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<ReceivedDamage> System::receiveMonster(const ActorContext &actor, EntityId source, const MonsterHit &hit, const MonsterHitStates &definitions,std::optional<ItemHandle> wear,const PlayerHitAdjustment &adjustment,const DamageReception *preparedReception,bool deferAction,bool successfulHit) {
    const auto *p=ports_.players.find(actor.player);
    if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || p->persistent.player.hp<=0) return {DomainStatus::InvalidActor,{}};
    auto next=state_;auto &recovery=next.players[actor.actor];auto &states=recovery.states;
    transactions::CharacterEdit edit{actor,p->inventoryRevision,p->characterRevision,p->persistent.player};
    PlayerResourceTrace resources(*p,actor.tick,{},ports_.areas.statTime(p->area));
    auto random=p->combatRandom;
    DamageReception reception;auto defenses=p->totals.character;
    int poisonPierce=adjustment.poisonPierce;
    if(preparedReception) {reception=*preparedReception;defenses.coldResist=reception.coldResistance;poisonPierce=reception.poisonPierce;}
    else {
        auto prepared=preparePlayerReception(actor,source,hit,adjustment);if(!prepared) return {prepared.status,{}};
        auto &plan=*prepared.value;recovery=std::move(plan.recovery);edit.player=plan.character;edit.publicFacts=std::move(plan.facts);
        if(plan.random) random=plan.random->second;
        reception=std::move(plan.damage);defenses=std::move(plan.defenses);poisonPierce=plan.poisonPierce;
        resources.inheritPrepared(edit.player,std::move(plan.totals),states);
    }
    const auto observe=[&]{resources.sample(states,true);};
    const auto dealtFixed=reception.dealt,absorbedFixed=reception.absorbed;
    const auto physical=preparedReception?std::min<int64_t>(reception.channels[0].dealt,edit.player.hp.raw()):reception.channels[0].dealt;
    const float dealt=float(std::max<int64_t>(0,dealtFixed))/256.f;
    const int64_t maximumLife=resources.maximum().life.raw();
    const auto restoredLife=std::min(maximumLife,int64_t(edit.player.hp.raw())+std::max<int64_t>(0,absorbedFixed));
    const auto life=subtractContactResource(std::max<int64_t>(0,restoredLife-adjustment.crushing),dealtFixed);
    edit.player.hp = Fixed8::fromRaw(nativeChecked(life));
    edit.player.mana = Fixed8::fromRaw(nativeChecked(subtractContactResource(preparedReception?edit.player.mana.raw():reception.mana,reception.drain.mana)));
    edit.player.stamina = Fixed8::fromRaw(nativeChecked(subtractContactResource(edit.player.stamina.raw(),reception.drain.stamina)));
    if(edit.player.hp>0 && hit.stunFrames && p->rules.skills && p->rules.skills->stunned.id>=0) {
        const auto &definition=p->rules.skills->stunned;
        applyNativeStatus(states,definition,{CombatEffectSource::NativeState,source,0,0},
            actor.tick+std::min<uint64_t>(250,hit.stunFrames),actor.tick,*p->rules.skills->stats,{}, {},observe);
    }
    if(edit.player.hp>0 && hit.slowFrames && definitions.slow.id>=0) {
        CombatEffectSpec slow;slow.state=definitions.slow;slow.duration=hit.slowFrames;
        slow.source={CombatEffectSource::Monster,source,definitions.slow.id,0};slow.modifiers.velocityPercent=hit.slowPercent;
        states.apply(std::move(slow),actor.tick);
    }
    const auto applyCold=[&](uint64_t rawLength) {
        if(edit.player.hp<=0 || !rawLength || p->totals.character.combat.cannotBeFrozen || definitions.cold.id<0) return;
        auto duration=coldHitDuration(rawLength,defenses.coldResist,0,false,p->totals.character.combat.halfFreezeDuration);
        if(!duration) return;
        for(const auto &effect:states.entries()) if(effect.spec.state.id==definitions.cold.id && effect.expiresAt && *effect.expiresAt>actor.tick)
            duration=std::max(duration,*effect.expiresAt-actor.tick);
        const auto &rules=*p->rules.skills->stats;
        const std::array<StatValue,3> writes{{{rules.names.at("velocitypercent"),-50},{rules.names.at("attackrate"),-50},{rules.names.at("other_animrate"),-50}}};
        applyNativeStatus(states,definitions.cold,{CombatEffectSource::NativeState,source,0,0},
            actor.tick+duration,actor.tick,rules,writes,{NativeStateRemoval::Cold,p->rules.skills->shatter.id},observe);
        // ApplyFreezeState routes a player through ApplyColdState too. Its
        // second call consumes a second shatter draw, even without a player bit.
        rollRandom(random);
    };
    applyCold(hit.coldFrames);applyCold(hit.freezeFrames);
    if (edit.player.hp > 0 && hit.poisonFrames && hit.channels[5] && !p->totals.character.combat.preventPoison && definitions.poison.id >= 0) {
        const int64_t rate=reception.channels[5].dealt;
        const int lengthResist = std::clamp(nativeSubtract(nativeAdd(p->totals.character.combat.poisonLengthResist,p->rules.character?p->rules.character->resistancePenalty:0),poisonPierce),-100,75);
        const auto duration=resistedDuration(hit.poisonFrames,lengthResist);
        if (duration && replacesPoison(recovery.poison && states.hasState(definitions.poison.id,actor.tick)?recovery.poison->damage.rate:0,rate)) {
            const auto poisonOwner=recovery.poison?recovery.poison->source:source;
            const std::array<StatValue,1> writes{{{p->rules.skills->stats->names.at("hpregen"),nativeSubtract(0,nativeChecked(rate))}}};
            applyNativeStatus(states,definitions.poison,{CombatEffectSource::NativeState,poisonOwner,0,0},
                actor.tick+duration,actor.tick,*p->rules.skills->stats,writes,{},observe);
            if(recovery.poison) {
                recovery.poison->damage.rate=rate;recovery.poison->damage.frames=duration;
                recovery.poison->until=actor.tick+duration;
                recovery.poison->nativeExpireFrame.reset();recovery.poison->removalDue.reset();
            } else recovery.poison=PoisonStatus{{rate,duration,definitions.poison.id},source,actor.tick+duration,actor.tick+1};
        }
    }
    if(edit.player.hp>0 && hit.burn>0 && hit.burnFrames && p->rules.skills &&
       !states.hasState(p->rules.skills->fireShrine.id,actor.tick)) {
        const auto &definition=p->rules.skills->burning;const auto &rules=*p->rules.skills->stats;
        const auto existing=std::find_if(states.entries().begin(),states.entries().end(),[&](const auto &entry){return entry.spec.state.id==definition.id;});
        const auto key=StatKey{rules.names.at("hpregen"),0};
        const auto current=existing!=states.entries().end() && existing->spec.nativeValues?existing->spec.nativeValues->find(key):std::map<StatKey,int32_t>::const_iterator{};
        const auto rate=existing!=states.entries().end() && existing->spec.nativeValues && current!=existing->spec.nativeValues->end()?nativeSubtract(0,current->second):0;
        if(hit.burn>=rate) {
            const std::array<StatValue,1> writes{{{key,nativeSubtract(0,nativeChecked(hit.burn))}}};
            applyNativeStatus(states,definition,{CombatEffectSource::NativeState,source,0,0},actor.tick+hit.burnFrames,
                actor.tick,rules,writes,{},observe);
        }
    }
    if(edit.player.hp<=0) {
        recovery.killer=adjustment.credit?adjustment.credit:source;
        if(const auto *killer=ports_.players.findActor(recovery.killer);killer && killer->actor!=actor.actor) {
            recovery.killerName=killer->persistent.player.name;recovery.killerClass=killer->persistent.player.characterClass;
        }
    }
    if(edit.player.hp>0 && adjustment.wound && p->rules.skills && p->rules.skills->openWounds.id>=0) {
        const auto &definition=p->rules.skills->openWounds;
        const auto rate=recovery.wound?recovery.wound->rate:adjustment.wound->rate;
        const std::array<StatValue,1> writes{{{p->rules.skills->stats->names.at("hpregen"),nativeSubtract(0,nativeChecked(rate))}}};
        applyNativeStatus(states,definition,{CombatEffectSource::NativeState,source,0,1},adjustment.wound->until,
            actor.tick,*p->rules.skills->stats,writes,{},observe);
        if(recovery.wound && recovery.wound->until>actor.tick) recovery.wound->until=adjustment.wound->until;
        else recovery.wound=adjustment.wound;
    }

    resources.seedResources(edit.player.hp,edit.player.mana,edit.player.stamina);resources.sample(states,true);
    edit.player=resources.record();edit.resourceBase=resources.maximum();
    edit.transient = projectEffectAttributes(states,actor.tick);
    const auto clock=reactionTiming(*p,"gh");
    const bool getHit=!deferAction && source && hit.getHit.value_or(!hit.softHit) && (dealt>0 || hit.stunFrames) && edit.player.hp>0 && !hit.knockback && clock &&
        !ports_.skills.uninterruptible(actor.actor) &&
        monsterHitRecovery(dealtFixed,p->totals.character.maxLifeFixed.raw(),hit.hitClass,false,
            hit.channels[5]>0 && std::all_of(hit.channels.begin(),hit.channels.begin()+5,[](auto n){return n==0;}),true,random,
            p->rules.skills && states.hasState(p->rules.skills->stunned.id,actor.tick));
    const auto knockbackClock=hit.knockback?reactionTiming(*p,"kb"):std::optional<AnimationTiming>{};
    const bool knockback=!deferAction && source && dealt>0 && edit.player.hp>0 && knockbackClock && !ports_.skills.uninterruptible(actor.actor);
    if(getHit || knockback) {
        const auto removed=states.endModeEffects({},observe);
        for(const auto &entry:removed) if(entry.effect.spec.state.id>=0 && !states.hasState(entry.effect.spec.state.id,actor.tick))
            edit.publicFacts.emplace_back(StateFact{actor.actor,0,actor.area,entry.effect.spec.state.id,false});
        edit.player=resources.record();edit.resourceBase=resources.maximum();edit.transient=projectEffectAttributes(states,actor.tick);
    }
    actions::State::PreparedRecovery recoveryAction;
    if(getHit || knockback) {
        recoveryAction=ports_.actions.prepareRecovery(actor.actor);
        recoveryAction.mapped().until=actor.tick+std::min<uint64_t>((knockback?knockbackClock:clock)->durationTicks(),UINT64_MAX-actor.tick);
    }
    const auto percent = playerLifePercentage(int64_t(edit.player.hp.raw()),p->totals.character.maxLifeFixed.raw());
    if(dealt>0 || adjustment.crushing>0 || hit.mana>0 || hit.stamina>0 || getHit) edit.publicFacts.emplace_back(HitFact{actor.actor,0,actor.area,percent,!deferAction && edit.player.hp<=0,p->position,hit.hitClass});
    if(getHit || knockback) for(auto &fact:edit.publicFacts) if(auto *damage=std::get_if<HitFact>(&fact)) damage->playerMode=knockback?20:6;
    if(knockback && source) {
        const auto attacker=combat::Participants{ports_.players,ports_.monsters,ports_.social}.find(source);
        const auto &area=ports_.areas.at(actor.area);const Vec destination=knockbackDestination(p->position,attacker.position(),3);
        if(area.definition.collision.nativeMovementSegment(p->position,destination,playerMovement)) {
            edit.knockback=PointTarget{actor.area,area.generation,destination};
            for(auto &fact:edit.publicFacts) if(auto *damage=std::get_if<HitFact>(&fact)) damage->knockback=destination;
        }
    }
    edit.random=transactions::UnitRandomEdit{p->combatRandom,random};
    transactions::Change change=std::move(edit);
    if(wear) {
        const auto &resource=std::get<transactions::CharacterEdit>(change);
        transactions::InventoryEdit inventory{actor,p->inventoryRevision,p->characterRevision,p->persistent.inventory,{},p->persistent.player.weaponSet};
        const auto found=inventory.inventory.items.find(wear->id);
        if(found==inventory.inventory.items.end() || found->second.revision!=wear->revision || found->second.revision==UINT64_MAX || !found->second.durability) return {DomainStatus::Stale,{}};
        auto &item=found->second;--item.durability;++item.revision;
        inventory.changes.push_back({item.id,item.revision,ItemChangeKind::DurabilityChanged,item.location,item.location,item.quantity});
        inventory.character=resource.player;inventory.transient=resource.transient;inventory.publicFacts=resource.publicFacts;inventory.knockback=resource.knockback;
        inventory.resourceMaximums=resource.resourceMaximums;
        inventory.resourceBase=resource.resourceBase;
        inventory.random=resource.random;
        change=std::move(inventory);
    }
    auto itemEvents=itemTriggers_.prepare(actor,successfulHit?std::initializer_list{ItemSkillEvent::GetHit,ItemSkillEvent::Death}:
        std::initializer_list{ItemSkillEvent::Death},source,source?combat::Participants{ports_.players,ports_.monsters,ports_.social}.find(source).position():p->position);
    if(!itemEvents) return {itemEvents.status,{}};
    auto plan = ports_.transactions.prepare(std::move(change)); if (!plan) return {plan.status,{}};
    auto result = ports_.transactions.commit(std::move(*plan.value)); if (result) {
        publishPlayers(next.players);
        ports_.actions.commitRecovery(std::move(recoveryAction));
        if(getHit || knockback) ports_.skills.interrupt(actor);
        if(dealt>0) itemTriggers_.commit(std::move(*itemEvents.value),p->persistent.player.hp<=0);
    }
    return {result.status,result?std::optional{ReceivedDamage{dealt,physical}}:std::nullopt};
}
bool System::missileHitAllowed(EntityId id,uint64_t tick) const {
    const auto gate=[&](const CombatEffectSet &states) {
        return std::any_of(states.entries().begin(),states.entries().end(),[&](const auto &effect) {
            return effect.spec.nativeMissileHitGate && states.hasState(effect.spec.state.id,tick);
        });
    };
    if(const auto it=state_.players.find(id);it!=state_.players.end()) return !gate(it->second.states) && it->second.nextMissileHit<=tick;
    if(const auto it=units_.find(id);it!=units_.end() && gate(it->second.states)) return false;
    const auto *monster=ports_.monsters.find(id);return !monster || monster->nextHitTick<=tick;
}
void System::missileHitDelay(EntityId id,uint64_t until) {state_.players[id].nextMissileHit=until;}

}
