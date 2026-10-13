#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "core/random.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "gameplay/combat/life.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include "gameplay/rewards/experience.hpp"
#include "gameplay/monsters/melee_decision.hpp"
#include "server/systems/effects/system.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::monsters {
int movementPercent(const Actor &actor,uint64_t tick) {
    const int64_t value=int64_t(actor.velocityPercent)+actor.effectVelocity+actor.hirelingMovementPercent+
        (actor.necroPet?actor.necroPet->velocityPercent:0)+(actor.rule.enchantment?actor.rule.enchantment->velocityPercent:0)+
        (actor.chilledUntil>tick?actor.rule.coldEffect:0)+(actor.slowed?actor.slowed->percent:0);
    return int(std::clamp<int64_t>(value,25,INT16_MAX));
}
namespace {
int recoveryTicks(const Actor &actor,const effects::System &effects,uint64_t tick,bool block) {
    const auto &clock=block?actor.rule.blockAnimation:actor.rule.hitRecoveryAnimation;
    if(!clock) return block?actor.rule.blockTicks:actor.rule.hitRecoveryTicks;
    auto modifiers=effects.unitModifiers(actor.id,tick).combat;
    if(actor.combatCompanion()) mergeCombatModifiers(modifiers,actor.petStats.attributes.combat);
    const int speed=effectiveRecoverySpeed(clock->speed,block?modifiers.fasterBlock:modifiers.fasterHitRecovery);
    return monsterAnimationTicks({clock->frames,speed});
}
void playerCountBonus(MonsterRule &rule,unsigned count) {
    const unsigned scale=100+50*(count-1);
    rule.experienceBase=rule.experienceBase*scale/100;
    for(size_t level=1;level<rule.experience.size() && level<rule.experienceRatios.size();++level)
        rule.experience[level]=monsterExperienceGain(rule.experienceBase,1,rule.experienceLevel,int(level),rule.experienceRatios[level],rule.experienceShift);
    if(rule.difficulty==0 || count==1) return;
    // D2MOO MonsterMode::sub_6FC627B0 uses 8*(count-1)/128 for
    // expansion Nightmare/Hell attack damage, to-hit and native element duration.
    const auto scaled=[&](int value){return int(std::min(int64_t(INT32_MAX),int64_t(value)+int64_t(value)*8*(count-1)/128));};
    rule.minimumDamage=scaled(rule.minimumDamage);rule.maximumDamage=scaled(rule.maximumDamage);rule.attackRating=scaled(rule.attackRating);
    const auto attackBonus=[&](MonsterAttackRule &attack) {
        attack.minimum=scaled(attack.minimum);attack.maximum=scaled(attack.maximum);attack.rating=scaled(attack.rating);
        for(auto &element:attack.elements) {
            element.minimum=scaled(element.minimum);element.maximum=scaled(element.maximum);element.durationFrames=scaled(element.durationFrames);
        }
    };
    for(auto &[mode,attack]:rule.attacks) {(void)mode;attackBonus(attack);}
    for(auto &[skill,attack]:rule.skillActions) {(void)skill;attackBonus(attack);}
}
}
void System::withdrawQuestGroup(RegionId region,std::string_view name) {
    const auto key="quest."+std::string(name);
    std::erase_if(state_.actors,[&](const auto &entry){const auto &m=entry.second;return m.area==region && !m.owner && m.identity.origin==SpawnOrigin::Preset && (m.identity.spawnKey==key || m.identity.ownerSpawnKey==key);});
    std::erase_if(state_.deathCascades,[&](const auto &entry){return !state_.actors.contains(entry.first) || !state_.actors.contains(entry.second.source);});
}
DomainResult<EntityId> System::admit(const Admission &request) {
    const auto *area = ports_.areas.find(request.area);
    if (!area || area->definition.town || !request.hostile || !request.rule || (!request.implementation && request.rule->trap==MonsterRule::Trap::None) || request.identity.spawnKey.empty())
        return {DomainStatus::InvalidRequest, {}};
    const auto &rule = *request.rule;
    if (rule.nativeClass < 0 || rule.nativeClass > UINT16_MAX || rule.minimumLife <= 0 || rule.maximumLife < rule.minimumLife ||
        rule.nativeVelocity < 0 || rule.nativeVelocity > 255 || rule.difficulty < 0 || rule.difficulty > 2 || rule.alignment < 0 || rule.alignment > 2 ||
        rule.level <= 0 || (rule.attacks.empty() && rule.skillActions.empty()) ||
        !area->definition.collision.walkable(request.position, rule.spawnCollision)) return {DomainStatus::InvalidRequest, {}};
    if (state_.actors.size() >= 65536 || ports_.ids.cursor() >= UINT32_MAX) return {DomainStatus::Capacity, {}};
    for (const auto &[id, actor] : state_.actors)
        if (actor.area == request.area && actor.identity.spawnKey == request.identity.spawnKey) return {DomainStatus::Applied, id};
    Actor actor;
    actor.hostile=request.hostile;
    const auto living=unsigned(std::count_if(ports_.players.all().begin(),ports_.players.all().end(),[](const auto &v){return v.second.entered && v.second.persistent.player.hp>0;}));
    actor.admittedPlayerCount=std::clamp(std::max(living,ports_.settings.virtualPlayers),1u,8u);
    actor.id = ports_.ids.allocate(); actor.identity = request.identity; actor.implementation = request.implementation;
    actor.area = request.area; actor.position = actor.home = request.position; actor.skillPositions=request.skillPositions; actor.revision = 1; actor.rule = rule;
    actor.life = actor.maximumLife = (int64_t(rule.minimumLife) + limitedRandom(ports_.random, uint32_t(rule.maximumLife - rule.minimumLife + 1))) * 256;
    // D2MOO MONSTER_GetHpBonus/GetExperienceBonus: +50% per additional
    // living/virtual player, captured at admission; existing monsters stay unchanged.
    const unsigned scale=100+50*(actor.admittedPlayerCount-1);
    actor.life=actor.maximumLife=actor.maximumLife*scale/100;
    playerCountBonus(actor.rule,actor.admittedPlayerCount);
    actor.combatRandom = childRandom(ports_.random);
    // MONSTERREGION::sub_6FC67FA0 lazily adds out-of-pool identities when
    // TotalPieces > 2, up to thirteen class entries. Never borrow another area.
    auto &palettes=state_.componentPalettes.try_emplace(request.area,area->definition.componentPalettes).first->second;
    auto palette=palettes.find(rule.nativeClass);
    if(palette==palettes.end() && rule.totalPieces>2 && palettes.size()<13)
        palette=palettes.emplace(rule.nativeClass,monsterComponentPalette(rule.componentCounts,actor.combatRandom)).first;
    actor.components=chooseMonsterComponents(rule.componentCounts,
        palette==palettes.end()?MonsterComponentPalette{}:palette->second,actor.combatRandom);
    actor.shield=actor.components[7]<rule.shieldChoices.size() && rule.shieldChoices[actor.components[7]];
    const auto id = actor.id;
    state_.actors.emplace(id, std::move(actor));
    return {DomainStatus::Applied, id};
}
DomainResult<> System::finishTrap(EntityId id,uint64_t tick) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end() || !found->second.rule.environmentController) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;
    auto result=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},{HitFact{id,1,actor.area,0,true,actor.position,0,0}}});
    if(!result) return {result.status,{}};
    actor.life=0;actor.rewardComplete=true;actor.deathTick=tick;actor.route.clear();actor.busyUntil=tick+uint64_t(actor.rule.deathTicks);++actor.revision;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::reduceShrineLife(EntityId id,int64_t amount) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end() || !found->second.damageable() || found->second.life<=0 || amount<0 || amount>=found->second.life) return {DomainStatus::InvalidActor,{}};
    found->second.life-=amount;++found->second.revision;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::upgradeShrine(EntityId id,uint64_t tick) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end()) return {DomainStatus::Stale,{}};
    auto &actor=found->second;
    if(!actor.enemyTarget() || actor.life<=0 || actor.identity.rank!=MonsterRank::Normal || !actor.rule.shrineUpgrade || actor.moving || actor.busyUntil>tick || actor.revision==UINT64_MAX) return {DomainStatus::Conflict,{}};
    auto rule=*actor.rule.shrineUpgrade;
    playerCountBonus(rule,actor.admittedPlayerCount);
    const auto minions=actor.rule.shrineMinions;
    const auto enchantment=rule.enchantment;if(!enchantment) return {DomainStatus::Unavailable,{}};
    actor.maximumLife=actor.maximumLife*(100+enchantment->lifePercent)/100*enchantment->lifeScalePercent/100;
    actor.life=actor.maximumLife;actor.rule=std::move(rule);actor.identity.rank=MonsterRank::Unique;actor.identity.ownerSpawnKey.clear();++actor.revision;actor.combatRandom=childRandom(actor.combatRandom);
    for(auto &[otherId,minion]:state_.actors) {
        if(otherId==id || minion.area!=actor.area || minion.life<=0 || !minion.enemyTarget() || minion.identity.ownerSpawnKey!=actor.identity.spawnKey) continue;
        const auto inherited=minions.find(minion.identity.monster);if(inherited==minions.end()) continue;
        const auto &next=*inherited->second;const auto &mods=*next.enchantment;
        minion.maximumLife=minion.maximumLife*(100+mods.lifePercent)/100*mods.lifeScalePercent/100;minion.life=minion.maximumLife;minion.rule=next;playerCountBonus(minion.rule,minion.admittedPlayerCount);minion.identity.rank=MonsterRank::Minion;++minion.revision;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
std::optional<std::pair<Vec,int>> System::targetPosition(EntityId id,RegionId area) const {
    for(const auto &[key,p]:ports_.players.all()) {
        (void)key;if(p.actor==id && p.entered && p.area==area && p.persistent.player.hp>0) return std::pair{p.position,2};
    }
    const auto *m=find(id);
    if(m && m->area==area && m->life>0 && m->damageable()) return std::pair{m->position,m->rule.size};
    return {};
}
bool System::recacheHirelingRoom(Actor &actor) {
    if(!actor.hireling) return true;
    constexpr Vec center{.5f,.5f};
    const auto &source=ports_.areas.at(actor.area);
    if(source.definition.activation.room(actor.position+center)) return true;
    Vec navigationOrigin;
    const auto navigation=source.definition.collision.linkedNavigation(navigationOrigin);
    const AreaState *destination=nullptr;Vec offset;
    for(const auto &[region,candidate]:ports_.areas.all()) {
        (void)region;
        if(candidate.definition.act!=source.definition.act ||
            std::none_of(navigation.navigationSources.begin(),navigation.navigationSources.end(),
                [&](const auto &entry){return entry.grid==&candidate.definition.collision;})) continue;
        const Vec translation=source.definition.origin-candidate.definition.origin;
        if(!candidate.definition.activation.room(actor.position+translation+center)) continue;
        if(destination) return false;
        destination=&candidate;offset=translation;
    }
    if(!destination) return false;
    // PATH_RecacheRoomAtCoordIfNeeded moves room ownership without restarting
    // the path or treating a continuous level boundary as a pet teleport.
    actor.area=destination->definition.id;
    actor.position=actor.pathStep.translate(actor.position+center,offset)-center;
    actor.movementGoal=actor.movementGoal+offset;
    for(auto &point:actor.route) point=point+offset;
    return true;
}
DomainResult<> System::requestMove(const MoveRequest &request) {
    const auto it = state_.actors.find(request.actor);
    if (it == state_.actors.end() || it->second.life <= 0) return {DomainStatus::InvalidActor, {}};
    auto &actor = it->second;
    const auto &area = ports_.areas.at(actor.area);
    if (request.destination.area != actor.area || request.destination.generation != area.generation)
        return {DomainStatus::Stale, {}};
    if (actor.rule.nativeVelocity <= 0 || (request.target && !targetPosition(request.target,actor.area)) || request.stopDistance<0 || request.stopDistance>255 ||
        request.velocityPercent<25 || request.velocityPercent>INT16_MAX) return {DomainStatus::InvalidRequest,{}};
    const auto collision=actor.airborne?MovementCollisionRule{0,1}:actor.rule.collision;
    uint8_t pathType=request.circleTurn?1:request.pathType;
    // MonsterMode's default obstacle-search distance is 5; pet tactics supply
    // their own 40/100 bounds. Circling is
    // published as AStar to its computed last point by SCmd, not as pursuit.
    const uint8_t pathDistance=request.pathDistance;
    if((pathType!=1 && pathType!=2 && pathType!=7 && pathType!=13 && pathType!=15) || !pathDistance)
        return {DomainStatus::InvalidRequest,{}};
    const Vec center{.5f,.5f};
    auto route = request.circleTurn?area.definition.collision.nativeMonsterCirclePath(actor.position+center,request.destination.position+center,request.circleDistance,request.circleTurn,collision):
        area.definition.collision.nativeUnitPath(actor.position+center,request.destination.position+center,pathType,pathDistance,collision);
    if(route.empty() && !request.circleTurn && (pathType==2 || pathType==7 || pathType==13)) {
        // MonsterMode retries failed Toward paths with WallFollow. The
        // solver raises its search bound to 40 for a live unit target.
        pathType=15;
        route=area.definition.collision.nativeUnitPath(actor.position+center,request.destination.position+center,pathType,pathDistance,collision,bool(request.target));
    }
    for(auto &point:route) point=point-center;
    if (route.empty()) return {DomainStatus::Unavailable, {}};
    if(request.pathSteps<0 || request.pathSteps>255) return {DomainStatus::InvalidRequest,{}};
    // PATH_SetStepNum stores steps - 1 as a unit-distance arrival bound.
    // It never limits the number of route points (D2Common PathMisc).
    const auto goal=request.circleTurn?route.back():request.destination.position;
    actor.route = std::move(route); actor.movementTarget = request.circleTurn?EntityId{}:request.target;
    actor.pathStep.reset();
    actor.pathSteps=uint8_t(request.pathSteps);
    actor.pathType=pathType;actor.pathDistance=pathDistance;
    actor.movementGoal = goal;
    actor.stopDistance = request.pathSteps>0?request.pathSteps-1:request.stopDistance;
    actor.velocityPercent = request.velocityPercent; actor.running = request.running;
    if(!actor.airborne) {actor.aiPose=1;actor.holdPose=false;actor.poseUntil=0;}
    ++actor.revision;
    return {DomainStatus::Applied, std::monostate{}};
}
void System::stop(EntityId id) {
    if (auto it = state_.actors.find(id); it != state_.actors.end()) {
        auto &actor = it->second;
        // Step::PATH_ResetMovement clears points and velocity, not pTargetUnit.
        if (!actor.route.empty() || actor.moving || actor.running) { actor.route.clear(); actor.moving = false; actor.running = false; ++actor.revision; }
    }
}
DomainResult<> System::beginAttack(EntityId id, uint64_t until, bool returnToNeutral, EntityId target) {
    auto it = state_.actors.find(id);
    if (it == state_.actors.end() || it->second.life <= 0) return {DomainStatus::InvalidActor, {}};
    auto &actor = it->second;
    actor.aiPose=1;actor.holdPose=false;actor.poseUntil=0;
    actor.neutralAt=returnToNeutral?std::optional{until}:std::nullopt;
    // MonsterMode's mode change installs the action target on the same path.
    // BL must retain that identity after an attacking actor stops walking.
    actor.busyUntil = until; actor.route.clear(); actor.moving = false; actor.running = false; actor.movementTarget = target; ++actor.revision;
    return {DomainStatus::Applied, std::monostate{}};
}
uint64_t System::resumeAi(EntityId id,uint64_t tick,uint64_t scheduled) {
    auto found=state_.actors.find(id);
    if(found==state_.actors.end()) return scheduled;
    auto &actor=found->second;
    if(actor.life<=0) {actor.neutralAt.reset();return scheduled;}
    if(!actor.neutralAt || tick<*actor.neutralAt || actor.frozenUntil>tick) return scheduled;
    const auto ended=std::max(*actor.neutralAt,actor.frozenUntil);
    actor.neutralAt.reset();
    // MonsterMode::sub_6FC64310 preserves an already scheduled future AI
    // event. Default aidel starts at ENDANIM, not at attack start, and is
    // independent of busyUntil so the neutral pause cannot extend animation.
    if(scheduled<=ended) {
        const uint64_t delay=actor.stunnedUntil>ended?45:uint64_t(actor.rule.neutralDelayTicks>0?actor.rule.neutralDelayTicks:15);
        scheduled=ended+std::min(delay,UINT64_MAX-ended);
    }
    return scheduled;
}
DomainResult<> System::damage(EntityId id, EntityId source, int64_t amount, uint64_t tick, uint64_t coldFrames, bool freeze, uint8_t hitClass,std::optional<PoisonApplication> poison,bool poisonOnly,uint64_t stunFrames,std::optional<OpenWoundsApplication> wound,int64_t absorbed,bool clearStun,int64_t crushing,int coldPierce) {
    auto it = state_.actors.find(id);
    if (it == state_.actors.end() || it->second.life <= 0 || !it->second.damageable() || amount < 0 || crushing < 0) return {DomainStatus::InvalidActor, {}};
    auto &actor = it->second;
    // A refresh cannot overwrite an unpaid poison interval after backpressure.
    if(poison && actor.poison && tick>=actor.poison->next && actor.poison->next<actor.poison->until)
        return {DomainStatus::Capacity,{}};
    // Crushing Blow changes current life before the ordinary damage channel;
    // it must not inflate the hit-recovery damage threshold.
    // The poison rate contributes once to the contact damage as well as to
    // HPREGEN (SUnitDmg total-damage channel). Duration immunity does not erase it.
    if(poison) amount+=std::max<int64_t>(0,poison->rate);
    const auto life = std::max(actor.hireling && poisonOnly && !poison && ports_.areas.at(actor.area).definition.town?int64_t(256):int64_t(0), std::min(actor.maximumLife,actor.life+std::max<int64_t>(0,absorbed)) - crushing - amount);
    const uint8_t percent = monsterLifeRatio(life,actor.maximumLife);
    auto random=actor.combatRandom;
    auto modifiers=ports_.effects.unitModifiers(id,tick).combat;
    if(actor.combatCompanion()) mergeCombatModifiers(modifiers,actor.petStats.attributes.combat);
    auto chilled=actor.chilledUntil, frozen=actor.frozenUntil;
    auto shatterActive=actor.shatterActive;
    const auto states=ports_.effects.unitStates(id,tick);
    const bool uninterruptable=states.contains(actor.rule.uninterruptableState) || actor.aiStates.contains(actor.rule.uninterruptableState);
    const bool freezable=!actor.hireling && !actor.rule.boss &&
        (actor.identity.rank==MonsterRank::Normal || actor.identity.rank==MonsterRank::Minion);
    coldFrames=coldHitDuration(coldFrames,ports_.effects.unitResistance(id,DamageType::Cold,tick),coldPierce,
        modifiers.cannotBeFrozen,modifiers.halfFreezeDuration);
    bool appliedFreeze=false;
    if(coldFrames && !(freeze && uninterruptable)) {
        if(freeze && freezable && actor.rule.coldEffect<0) {
            // A positive input can truncate to zero after MonsterFreezeDivisor;
            // the state still participates in this hit's death transition.
            appliedFreeze=true;
            const auto duration=coldFrames/unsigned(actor.rule.freezeDivisor);
            frozen=std::max(frozen,tick+std::min(duration,UINT64_MAX-tick));
        } else if((!freeze || !freezable) && actor.rule.coldEffect!=0) {
            const auto duration=std::max<uint64_t>(1,actor.rule.coldEffect<0?coldFrames/unsigned(actor.rule.coldDivisor):coldFrames);
            chilled=std::max(chilled,tick+std::min(duration,UINT64_MAX-tick));
            // Original 1.13c D2Game RVA DC8F8: <20 and !MonStats2.deadCol.
            // D2MOO's corresponding reconstructed condition is reversed.
            shatterActive=limitedRandom(random,100)<20 && !actor.rule.deadCollision;
        }
    }
    auto deathStates=actor.deathStates;
    bool corpseUnavailable=actor.corpseUnavailable;
    if(!life) {
        const auto retain=[&](const CombatStateDefinition &state,bool enabled) {
            const auto kind=actor.rule.boss?EffectUnitKind::Boss:EffectUnitKind::Monster;
            if(enabled && state.id>=0 && state.stayOnDeath[size_t(kind)]) {
                deathStates.insert(state.id);
                corpseUnavailable|=state.hideOnDeath || state.shatterOnDeath || state.corpseUnselectable;
            }
        };
        retain(actor.rule.frozenState,appliedFreeze || frozen>tick);
        retain(actor.rule.shatterState,shatterActive ||
            (actor.rule.enchantment && actor.rule.enchantment->has(35)));
        chilled=frozen=0;shatterActive=false;
    }
    auto poisoned=actor.poison;
    if(poison) {
        const int lengthResistance=actor.hireling?
            std::clamp(modifiers.poisonLengthResist-poison->lengthPierce+actor.rule.difficultyResistancePenalty,-100,75):
            piercedResistance(modifiers.poisonLengthResist,poison->lengthPierce);
        poison->frames=modifiers.preventPoison?0:resistedDuration(poison->frames,lengthResistance);
        if(life && poison->frames && poison->state>=0 && replacesPoison(poisoned?poisoned->damage.rate:0,poison->rate)) {
            const auto until=tick+std::min(poison->frames,UINT64_MAX-tick);
            if(poisoned) {
                // Native refresh changes rate/deadline, retaining the stat-list owner.
                poisoned->damage.rate=poison->rate;poisoned->damage.frames=poison->frames;poisoned->until=until;
            } else poisoned=PoisonStatus{*poison,source,until,tick+1};
        }
    }
    if(!life) poisoned.reset();
    auto stunned=actor.stunnedUntil;
    if(clearStun) stunned=0;
    if(life && stunFrames && actor.rule.stunState>=0 && !actor.rule.boss && actor.rule.nativeVelocity &&
        (actor.identity.rank==MonsterRank::Normal || actor.identity.rank==MonsterRank::Minion || limitedRandom(random,100)>=90)) stunned=tick+std::min<uint64_t>(actor.hireling?13:250,stunFrames);
    if(!life) stunned=0;
    const bool recovery=life && !poisonOnly && monsterHitRecovery(amount,actor.maximumLife,hitClass,frozen>tick,poisonOnly,actor.rule.hitRecoveryTicks>0,random,stunned>tick);
    const int reactionTicks=recovery?recoveryTicks(actor,ports_.effects,tick,false):0;
    std::vector<DomainFact> facts;
    if(bool(chilled)!=bool(actor.chilledUntil)) facts.emplace_back(StateFact{id,1,actor.area,actor.rule.coldState,bool(chilled)});
    const bool freezeState=bool(frozen) || deathStates.contains(actor.rule.frozenState.id);
    if(freezeState!=bool(actor.frozenUntil)) facts.emplace_back(StateFact{id,1,actor.area,actor.rule.frozenState.id,freezeState});
    const bool wasShatter=actor.shatterActive;
    const bool shatter=shatterActive || deathStates.contains(actor.rule.shatterState.id);
    if((shatter!=wasShatter || (!life && shatter)) && actor.rule.shatterState.id>=0) facts.emplace_back(StateFact{id,1,actor.area,actor.rule.shatterState.id,shatter});
    // Publish original states before the death action, including a lethal first freeze.
    facts.emplace_back(HitFact{id,1,actor.area,percent,!life,actor.position,hitClass,uint8_t(recovery?3:0),{},actor.lightningReady});
    if(bool(stunned)!=bool(actor.stunnedUntil)) facts.emplace_back(StateFact{id,1,actor.area,actor.rule.stunState,bool(stunned)});
    if(bool(poisoned)!=bool(actor.poison)) facts.emplace_back(StateFact{id,1,actor.area,poisoned?poisoned->damage.state:actor.poison->damage.state,bool(poisoned)});
    if(actor.rule.openWoundsState>=0 && ((!life && actor.wound) || (life && wound && !actor.wound))) facts.emplace_back(StateFact{id,1,actor.area,actor.rule.openWoundsState,bool(life && wound)});
    auto event = ports_.events.publish({0, tick, {}, {AudienceKind::Area, {}, actor.area}, std::move(facts)});
    if (!event) return {event.status, {}};
    actor.life = life; ++actor.revision; if(amount>0 && !poisonOnly) ++actor.damageOccurrence;
    actor.combatRandom=random;
    if (recovery) ++actor.hitOccurrence;
    actor.chilledUntil = chilled; actor.frozenUntil = frozen;
    actor.shatterActive=shatterActive;actor.deathStates=std::move(deathStates);
    actor.stunnedUntil=stunned;
    actor.poison=poisoned;
    if(!life) actor.wound.reset();
    else if(wound) {
        // Native same-state/same-skill refresh extends the existing stat list;
        // its original rate and source survive instead of stacking wounds.
        if(actor.wound) actor.wound->until=wound->until;
        else actor.wound=std::move(wound);
    }
    if(recovery || frozen>tick || stunned>tick || !life) {
        ++actor.interruption;stop(id);
        if(recovery) {actor.reactionMode=3;actor.reactionUntil=tick+std::min<uint64_t>(reactionTicks,UINT64_MAX-tick);actor.busyUntil=actor.reactionUntil;actor.neutralAt=actor.reactionUntil;}
    }
    if (!life) {
        // Fn043 marks an exhausted nest NOTC before its own death action.
        if(source==id && (actor.rule.ai.kind==MonsterAiKind::FoulCrowNest || actor.rule.ai.kind==MonsterAiKind::Sarcophagus)) actor.rule.noTreasure=true;
        actor.corpseUnavailable=corpseUnavailable;
        actor.deathTick = tick; actor.deathOccurrence = *event.value; actor.killer = source;
        actor.route.clear(); actor.moving = false; actor.running = false; actor.movementTarget = {}; actor.busyUntil = tick + uint64_t(actor.rule.deathTicks);
        if(actor.rule.deathSweep) {
            const auto &sweep=*actor.rule.deathSweep;auto random=actor.combatRandom;
            const auto &activation=ports_.areas.at(actor.area).definition.activation;
            for(const auto &[key,other]:state_.actors) if(key!=id && !other.owner && other.area==actor.area && other.life>0 && (!sweep.undeadOnly || other.rule.undead) && (other.position-actor.position).length()<=sweep.radius && activation.nearby(actor.position,other.position))
                state_.deathCascades.try_emplace(key,State::DeathCascade{id,tick+uint64_t(sweep.minimumDelay)+limitedRandom(random,unsigned(sweep.maximumDelay-sweep.minimumDelay))});
            actor.combatRandom=random;
        }
    }
    return {DomainStatus::Applied, std::monostate{}};
}
DomainResult<> System::block(EntityId id,uint64_t tick) {
    auto it=state_.actors.find(id);if(it==state_.actors.end() || it->second.life<=0) return {DomainStatus::InvalidActor,{}};
    auto &actor=it->second;
    if(actor.rule.blockTicks>0) {
        const int reactionTicks=recoveryTicks(actor,ports_.effects,tick,true);
        const uint8_t percent=monsterLifeRatio(actor.life,actor.maximumLife);
        HitFact hit{id,1,actor.area,percent,false,actor.position,0,6};
        if(actor.movementTarget && targetPosition(actor.movementTarget,actor.area))
            hit.actionTarget=MotionTarget{actor.movementTarget,uint8_t(ports_.players.findActor(actor.movementTarget)?0:1)};
        auto result=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},{hit}});
        if(!result) return {result.status,{}};
        stop(id);++actor.interruption;actor.reactionMode=6;actor.reactionUntil=tick+std::min<uint64_t>(reactionTicks,UINT64_MAX-tick);actor.busyUntil=actor.reactionUntil;actor.neutralAt=actor.reactionUntil;++actor.revision;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
void System::holyFreeze(EntityId id,uint64_t until) {
    if(auto it=state_.actors.find(id);it!=state_.actors.end() && it->second.life>0) {
        auto &actor=it->second;actor.holyFreezeUntil=until;
        actor.shatterActive=limitedRandom(actor.combatRandom,100)<20;++actor.revision;
    }
}
void System::hitDelay(EntityId id, uint64_t until) { if (auto it = state_.actors.find(id); it != state_.actors.end()) it->second.nextHitTick = std::max(it->second.nextHitTick, until); }
void System::knockback(EntityId id, Vec source, uint64_t tick) {
    auto it=state_.actors.find(id);if(it==state_.actors.end() || it->second.life<=0 || it->second.owner || it->second.rule.knockbackTicks<=0) return;
    auto &m=it->second;stop(id);m.knockbackSource=source;m.knockbackGoal=knockbackDestination(m.position,source,3);
    m.knockedUntil=tick+uint64_t(m.rule.knockbackTicks);m.busyUntil=m.knockedUntil;m.neutralAt.reset();++m.hitOccurrence;++m.interruption;++m.revision;
}
void System::rewardComplete(EntityId id) { if (auto it = state_.actors.find(id); it != state_.actors.end()) it->second.rewardComplete = true; }
DomainResult<> System::remove(EntityId id) {
    auto it=state_.actors.find(id);if(it==state_.actors.end() || !it->second.owner) return {DomainStatus::InvalidActor,{}};
    state_.actors.erase(it);return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    bool blocked = false;
    for(auto it=state_.deathCascades.begin();it!=state_.deathCascades.end();) {
        if(it->second.due>tick.tick) {++it;continue;}
        const auto *target=find(it->first);
        if(target && target->life>0) {
            const auto result=damage(it->first,{},target->life,tick.tick);
            if(result.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
        }
        it=state_.deathCascades.erase(it);
    }
    for (auto &[id, actor] : state_.actors) {
        if (!ports_.areas.loaded(actor.area, actor.position+Vec{.5f,.5f})) {
            actor.moving = false; actor.route.clear(); actor.movementTarget = {};
            continue;
        }
        if(actor.curseControl.ai!=CurseAi::None && actor.curseControl.until<=tick.tick) actor.curseControl={};
        if(actor.attractionUntil<=tick.tick) actor.attractionTarget={};
        (void)id; actor.moving = false;
        if(actor.conversion && (tick.tick>=actor.conversion->until || actor.life<=0 || !actor.owner || !ports_.players.find(*actor.owner) || !ports_.players.find(*actor.owner)->entered)) {
            const auto saved=*actor.conversion;
            if(!ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},actor.area},
                {StateFact{id,1,actor.area,saved.state,false},StateFact{id,1,actor.area,saved.alignment,true,{{saved.stat,0}}}}})) {blocked=true;continue;}
            if(actor.life>0) actor.life=std::min(saved.maximum,std::max<int64_t>(256,actor.life*saved.maximum/actor.maximumLife));
            actor.maximumLife=saved.maximum;actor.rule.level=saved.level;actor.owner.reset();actor.conversion.reset();++actor.allegianceRevision;++actor.interruption;stop(id);++actor.revision;
        }
        if(actor.hireling) {
            actor.potionEffects.expire(tick.tick);
            if(actor.life<=0) {actor.healing.clear();actor.potionEffects.clear();}
            else for(auto healing=actor.healing.begin();healing!=actor.healing.end();) {
                const auto amount=advanceRestorationFrame(healing->second,tick.tick);
                if(amount>0) {actor.life=std::min(actor.maximumLife,actor.life+amount);++actor.revision;}
                if(tick.tick>=healing->second.until || actor.life>=actor.maximumLife) {
                    actor.potionEffects.removeState(healing->first);healing=actor.healing.erase(healing);
                } else ++healing;
            }
        }
        if(actor.wound && actor.life>0) {
            if(tick.tick>=actor.wound->next && actor.wound->next<=actor.wound->until) {
                const auto wound=*actor.wound;
                const auto last=std::min(tick.tick,wound.until);
                const auto frames=last-wound.next+1;
                // Retain every unpaid pulse across output backpressure, including
                // the last frame, before retiring the state. Normal ticks owe one.
                const auto result=damage(id,wound.credit,wound.rate*int64_t(frames),tick.tick,0,false,0,{},true);
                if(result.status==DomainStatus::Capacity) {blocked=true;continue;}
                if(result) {if(actor.wound) actor.wound->next=last+1;recordHirelingKill(id,wound.source);}
            }
            if(actor.wound && tick.tick>=actor.wound->until) {
                if(!ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},actor.area},{StateFact{id,1,actor.area,actor.rule.openWoundsState,false}}})) {blocked=true;continue;}
                actor.wound.reset();
            }
        }
        if (!actor.owner && actor.life > 0 && !actor.poison && !actor.wound && actor.rule.damageRegen > 0 && actor.life < actor.maximumLife) {
            actor.life = std::min(actor.maximumLife, actor.life + actor.maximumLife * actor.rule.damageRegen / 4096 + actor.aiRegenerationBonus); ++actor.revision;
        }
        if((actor.amazonPet || actor.necroSummon || actor.hireling) && actor.life>0 && !actor.poison && !actor.wound && actor.petStats.damageRegen>0 && actor.life<actor.maximumLife) {
            actor.life=std::min(actor.maximumLife,actor.life+actor.maximumLife*actor.petStats.damageRegen/4096+actor.aiRegenerationBonus);++actor.revision;
        }
        if(actor.hireling && actor.life>0 && !actor.poison && !actor.wound && actor.life<actor.maximumLife) {
            actor.life=std::clamp(actor.life+actor.rule.hirelingRegeneration+actor.petStats.attributes.combat.replenishLife,int64_t(1),actor.maximumLife);++actor.revision;
        }
        if(actor.poison && actor.life>0) {
            if(tick.tick>=actor.poison->next && actor.poison->next<actor.poison->until) {
                const auto poison=*actor.poison;
                const auto last=std::min(tick.tick,poison.until-1);
                const auto result=damage(id,poison.source,poison.damage.rate*int64_t(last-poison.next+1),tick.tick,0,false,0,{},true);
                if(result.status==DomainStatus::Capacity) {blocked=true;continue;}
                if(result) {
                    recordHirelingKill(id,poison.damage.actualSource);
                    if(actor.poison) actor.poison->next=last+1;
                }
            }
            if(actor.poison && tick.tick>=actor.poison->until) {
                if(!ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},actor.area},{StateFact{id,1,actor.area,actor.poison->damage.state,false}}})) {blocked=true;continue;}
                actor.poison.reset();++actor.revision;
            }
        }
        if(actor.knockbackGoal && actor.life>0) {
            const auto &grid=ports_.areas.at(actor.area).definition.collision;
            const Vec delta=*actor.knockbackGoal-actor.position;
            const Vec next=actor.position+delta.unit()*std::min(delta.length(),25.f*TickContext::seconds);
            if(grid.segment(actor.position,next,{},actor.rule.collision)) actor.position=next;
            else actor.knockbackGoal=actor.position;
            ++actor.revision;
            if(tick.tick<actor.knockedUntil) continue;
            actor.knockbackGoal.reset();actor.knockedUntil=0;
        }
        std::vector<DomainFact> expired;
        const bool thaw = actor.frozenUntil && actor.frozenUntil <= tick.tick;
        const bool warm = actor.chilledUntil && actor.chilledUntil <= tick.tick;
        const bool unstun=actor.stunnedUntil && actor.stunnedUntil<=tick.tick;
        const bool holyWarm=actor.holyFreezeUntil && actor.holyFreezeUntil<=tick.tick;
        if (thaw) expired.emplace_back(StateFact{id, 1, actor.area, actor.rule.frozenState.id, false});
        if (warm) {
            expired.emplace_back(StateFact{id,1,actor.area,actor.rule.coldState,false});
        }
        if(actor.life>0 && (warm || holyWarm) && actor.shatterActive && actor.rule.shatterState.id>=0)
            expired.emplace_back(StateFact{id,1,actor.area,actor.rule.shatterState.id,false});
        if(unstun) expired.emplace_back(StateFact{id,1,actor.area,actor.rule.stunState,false});
        if (!expired.empty()) {
            if (!ports_.events.publish({0, tick.tick, {}, {AudienceKind::Area, {}, actor.area}, std::move(expired)})) { blocked = true; continue; }
            if (thaw) actor.frozenUntil = 0;
            if(warm) actor.chilledUntil=0;
            if(actor.life>0 && (warm || holyWarm)) actor.shatterActive=false;
            if(unstun) actor.stunnedUntil=0;
            ++actor.revision;
        }
        if(holyWarm) actor.holyFreezeUntil=0;
        if(actor.webUntil && actor.webUntil<=tick.tick) {
            if(!ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},actor.area},{StateFact{id,1,actor.area,actor.rule.web->aura.id,false}}})) {blocked=true;continue;}
            actor.webUntil=0;++actor.revision;
        }
        if(actor.slowed && (actor.life<=0 || actor.slowed->until<=tick.tick)) {
            if(!ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},actor.area},{StateFact{id,1,actor.area,actor.slowed->state,false}}})) {blocked=true;continue;}
            actor.slowed.reset();++actor.revision;
        }
        if(actor.skillMotion && advanceSkillMotion(actor,tick.tick)) continue;
        if (actor.frozenUntil > tick.tick || actor.stunnedUntil>tick.tick) continue;
        if (actor.life <= 0 || tick.tick < actor.busyUntil) continue;
        if (actor.route.empty()) continue;
        const auto target=actor.movementTarget ? targetPosition(actor.movementTarget,actor.area) : std::optional(std::pair{actor.movementGoal, 0});
        if(!target) {stop(id);continue;}
        const auto &grid=ports_.areas.at(actor.area).definition.collision;
        auto arrived=[&](Vec position) {
            // Coordinate paths finish by consuming their last precise point,
            // not by an extra floating stopping radius.
            if (!actor.movementTarget) return false;
            return meleeDistance(position,actor.rule.size,target->first,target->second)<=actor.stopDistance &&
                (actor.pathSteps || grid.segment(position,target->first));
        };
        if (arrived(actor.position)) { stop(id); continue; }
        const int speed = movementPercent(actor,tick.tick);
        float remaining = monsterMovementSpeed(actor.rule.nativeVelocity, speed) * TickContext::seconds;
        while (!actor.route.empty() && remaining > 0) {
            const Vec delta = actor.route.front() - actor.position;
            const float distance = delta.length();
            if (distance == 0) { actor.route.pop_front(); continue; }
            constexpr Vec center{.5f,.5f};
            const auto previousStep=actor.pathStep;
            const auto step=actor.pathStep.advance(grid,actor.position+center,actor.route.front()+center,
                remaining,actor.airborne?MovementCollisionRule{0,1}:actor.rule.collision);
            const Vec next=step.position-center;
            const bool atGoal=arrived(next);
            bool barrier=false;
            if(!actor.airborne) for(const auto &[other,body]:state_.actors) if(other!=id && body.life>0 && body.area==actor.area && body.implementation==MonsterKind::BoneWall && missileUnitIntersection(actor.position,next,actor.rule.size,body.position,body.rule.size)) {barrier=true;break;}
            if(barrier) {actor.route.clear();actor.moving=false;break;}
            const auto previous=actor.position;actor.position=next;
            if(!recacheHirelingRoom(actor)) {actor.position=previous;actor.pathStep=previousStep;break;}
            actor.moving=actor.moving || step.travelled>0;++actor.revision;
            if(step.blocked) {actor.route.clear();break;}
            if (atGoal) { actor.route.clear(); break; }
            if(!step.reached) break;
            remaining-=step.travelled;actor.route.pop_front();
            if(step.travelled>0) break; // D2Common_10226: one nonzero path point per frame.
        }
        // Path exhaustion/collision returns FALSE from D2Common_10226 and
        // leaves WALK/RUN in this frame; do not publish a stale chase target.
        if(actor.route.empty()) {actor.moving=false;actor.running=false;}
    }
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}
