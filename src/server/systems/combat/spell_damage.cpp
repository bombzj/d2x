#include "system.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include "player_contact.hpp"
#include "weapon_hit.hpp"
#include "participants.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include "gameplay/combat/accuracy.hpp"
#include "gameplay/combat/avoidance.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/scaling.hpp"
#include "gameplay/combat/weapon_hit.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/stats/stat_list.hpp"
#include "core/random.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include <stdexcept>
#include <algorithm>
namespace d2x::server::combat {
// Read-only contact planning lets native missile callbacks depend on accuracy
// without committing damage or advancing the live RNG before outbox admission.
bool System::weaponContact(const WeaponSkillDamage &attack,EntityId id,uint64_t tick,uint64_t &random) const {
    if(const auto *player=ports_.players.findActor(id)) {
        if(!player->entered || player->persistent.player.hp<=0) return false;
        if(attack.automatic || (!attack.projectile && player->moving && player->runningNow)) return true;
        const auto &weapon=attack.weapon;
        const auto &stats=*player->rules.character->stats;
        const auto defense=nativeAdd(player->totals.character.defense,player->totals.nativeStats.get(
            {stats.names.at(attack.projectile?"armorclass_vs_missile":"armorclass_vs_hth"),0}));
        return limitedRandom(random,100)<unsigned(weaponHitChance(attack.level,weapon.baseAttackRating,
            weapon.attackRatingPercent,weapon.target,{player->persistent.player.level,defense,false,false,false,true,false},MonsterRank::Normal));
    }
    const auto *target=ports_.monsters.find(id);
    if(!target || target->life<=0 || !target->damageable()) return false;
    if(attack.automatic) return true;
    const auto &weapon=attack.weapon;
    const auto stats=ports_.effects.unitStatValues(id,tick);
    if(!stats || !target->rule.stats) throw std::logic_error("Missing native weapon target defense stats");
    const auto defense=nativeAdd(ports_.effects.unitDefense(id,tick),stats->get(
        {target->rule.stats->names.at(attack.projectile?"armorclass_vs_missile":"armorclass_vs_hth"),0}));
    return limitedRandom(random,100)<unsigned(weaponHitChance(attack.level,weapon.baseAttackRating,
        weapon.attackRatingPercent,weapon.target,{target->rule.level,defense,target->rule.demon,target->rule.undead,target->rule.boss,false,target->hireling},target->identity.rank));
}
DomainResult<SpellPlan> System::prepareSpells(std::vector<SpellImpact> impacts) const {
    SpellPlan plan;
    const Participants participants{ports_.players,ports_.monsters,ports_.social};
    for (auto &impact : impacts) {
        // Weapon contacts must carry the native calculation/allocation cursor.
        // There is no pre-rolled weapon fallback behind this boundary.
        if(impact.weapon && !impact.missileDamage) return {DomainStatus::InvalidRequest,{}};
        if (!impact.projectile || !impact.source || impact.next || impact.phase != ImpactPhase::Contact ||
            impact.damage < INT32_MIN || impact.damage > INT32_MAX ||
            impact.targets.size() > 65536)
            return {DomainStatus::InvalidRequest, {}};
        if(!impact.targetDamage.empty() && (impact.targetDamage.size()!=impact.targets.size() ||
            std::any_of(impact.targetDamage.begin(),impact.targetDamage.end(),[](int64_t n){return n<INT32_MIN || n>INT32_MAX;}))) return {DomainStatus::InvalidRequest,{}};
        if(impact.sourceHeal<0 || impact.sourceHeal>INT32_MAX || (impact.monsterHit &&
            (impact.monsterHit->lifeDrain<INT32_MIN || impact.monsterHit->lifeDrain>INT32_MAX ||
             impact.monsterHit->mana<INT32_MIN || impact.monsterHit->mana>INT32_MAX ||
             impact.monsterHit->stamina<INT32_MIN || impact.monsterHit->stamina>INT32_MAX ||
             std::any_of(impact.monsterHit->channels.begin(),impact.monsterHit->channels.end(),[](auto n){return n<INT32_MIN || n>INT32_MAX;})))) return {DomainStatus::InvalidRequest,{}};
        if (impact.targets.empty()) continue;
        if(!impact.environment && !impact.binding.actor) impact.binding=participants.bind(participants.find(impact.source));
        const auto same = [&](const auto &previous) { return previous.projectile == impact.projectile && previous.occurrence == impact.occurrence &&
            (!(impact.nativeAura && previous.nativeAura) || previous.area==impact.area); };
        if (std::any_of(state_.spells.begin(), state_.spells.end(), same) || std::any_of(plan.spells.begin(), plan.spells.end(), same))
            return {DomainStatus::Stale, {}};
        // A synchronous callback cannot wait for queued contacts belonging to
        // later frame phases. Its privately prepared node is released before
        // dispatch advances; the queued admission limits remain independent.
        const bool synchronous=impact.synchronous || impact.nativeAura || impact.reaction;
        if (!synchronous && (state_.spellTargets+plan.targets+impact.targets.size()>65536 || state_.spells.size()+plan.spells.size()>=256))
            return {DomainStatus::Capacity, {}};
        plan.targets += impact.targets.size(); plan.spells.push_back(std::move(impact));
    }
    return {DomainStatus::Applied, std::move(plan)};
}
void System::commitSpells(SpellPlan &&plan) noexcept {
    state_.spellTargets += plan.targets; state_.spells.splice(state_.spells.end(), plan.spells);
}
DomainResult<> System::enqueue(SpellImpact impact) {
    std::vector<SpellImpact> values; values.push_back(std::move(impact));
    auto plan = prepareSpells(std::move(values)); if (!plan) return {plan.status, {}};
    commitSpells(std::move(*plan.value)); return {DomainStatus::Applied, std::monostate{}};
}
StepStatus System::drainNativeMelee(EntityId source,uint64_t occurrence,TickContext tick) {
    for(auto &impact:state_.spells) if(impact.source==source && impact.occurrence==occurrence) impact.nativeDrainHeld=false;
    return resolveSpells(tick,std::pair{source,occurrence});
}
void System::discardNativeMelee(EntityId source,uint64_t occurrence) {
    for(auto &impact:state_.spells) if(impact.source==source && impact.occurrence==occurrence && impact.nativeDrainHeld && impact.nativeSkill)
        impact.nativeSkill->cancelled=true;
}
StepStatus System::resolveSpells(TickContext tick,std::optional<std::pair<EntityId,uint64_t>> selected,bool allocateOnly) {
    bool blocked = false;
    const Participants participants{ports_.players, ports_.monsters, ports_.social};
    for (auto it = state_.spells.begin(); it != state_.spells.end();) {
        auto &impact = *it;
        if(selected && (impact.projectile!=selected->first || impact.occurrence!=selected->second)) {++it;continue;}
        if(impact.nativeDrainHeld) {
            if(impact.nativeSkill && impact.nativeSkill->cancelled) {state_.spellTargets-=impact.targets.size();it=state_.spells.erase(it);continue;}
            if(!allocateOnly || (impact.nativeSkill && impact.nativeSkill->heldPrepared)) {++it;continue;}
        }
        if(impact.environment && !impact.missileDamage) {
            const auto result=resolveEnvironment(impact,tick);
            if(result==DomainStatus::Capacity || result==DomainStatus::Conflict) return StepStatus::Blocked;
            if(result==DomainStatus::NotImplemented || result==DomainStatus::InvalidRequest)
                throw std::logic_error("Native environment damage has no implemented rule");
            state_.spellTargets-=impact.targets.size();it=state_.spells.erase(it);continue;
        }
        const auto source = participants.find(impact.source);
        if((impact.missileDamage || impact.allocatedMissile) && !source.id() && impact.phase==ImpactPhase::Contact && !impact.playerContact && !impact.monsterContact)
            impact.next=impact.targets.size();
        if(!impact.missileDamage && !impact.allocatedMissile && impact.phase==ImpactPhase::Contact && !impact.playerContact && !impact.monsterContact && participants.bind(source)!=impact.binding) {state_.spellTargets-=impact.targets.size();it=state_.spells.erase(it);continue;}
        const PlayerState *owner = source.player;
        const auto *area = ports_.areas.find(impact.area);
        const auto *monsterSource=source.monster;
        const bool hirelingSource=monsterSource && monsterSource->owner;
        if(hirelingSource) owner=ports_.players.find(*monsterSource->owner);
        if (!impact.missileDamage && !impact.allocatedMissile && impact.phase==ImpactPhase::Contact && !impact.playerContact && !impact.monsterContact && ((!owner || !owner->entered || (!impact.nativeAura && owner->area!=impact.area)) &&
            (!monsterSource || monsterSource->owner || (!impact.nativeAura && monsterSource->area!=impact.area)))) impact.next=impact.targets.size();
        if(!area || area->definition.town) impact.next=impact.targets.size();
        while (impact.next < impact.targets.size()) {
            // Resource credit and conversion follow a committed hit.
            // Backpressure retains this phase without repeating target damage.
            if(impact.phase!=ImpactPhase::Contact) {
                if(finishImpact(impact,owner,area->generation,tick.tick)==DomainStatus::Capacity) {blocked=true;break;}
                continue;
            }
            if(impact.playerContact || impact.monsterContact) {
                const auto retained=participants.find(impact.targets[impact.next]);
                const auto result=impact.playerContact && retained.player?finishPlayerContact(impact,*retained.player,tick):
                    impact.monsterContact && retained.monster?finishMonsterContact(impact,*retained.monster,tick):DomainStatus::Stale;
                if(result==DomainStatus::Capacity) return StepStatus::Blocked;
                if(impact.nativeDrainHeld && impact.nativeSkill && impact.nativeSkill->heldPrepared) return StepStatus::Complete;
                if(result!=DomainStatus::Applied) {impact.playerContact.reset();impact.monsterContact.reset();}
                if(impact.phase==ImpactPhase::Contact) ++impact.next;
                continue;
            }

            if(!impact.missileDamage && (impact.returnFire || impact.allocatedMissile) && !impact.melee && (!impact.weapon || impact.weapon->projectile) && impact.missileEventTarget!=impact.next) {
                if(!impact.missileEvents) impact.missileEvents=std::make_shared<effects::NativeContactBatch>(
                    ports_.effects.prepareMissileEvents(impact.source,impact.targets[impact.next],tick.tick,impact.returnFire));
                const auto advanced=ports_.effects.advanceDamageEvents(*impact.missileEvents,tick.tick);
                if(isDeferred(advanced.status)) return StepStatus::Blocked;
                impact.missileEventTarget=impact.next;impact.missileEvents.reset();
                continue;
            }
            const auto participant = participants.find(impact.targets[impact.next]);
            if(impact.nativeAura) {
                const auto sourceArea=source.player?source.player->area:source.monster?source.monster->area:RegionId{};
                const bool retainedSource=impact.retainDeadSource && ((source.player && source.player->entered) || source.monster);
                if((!source.aliveIn(sourceArea) && !retainedSource) || !ports_.areas.nearby(sourceArea,source.position(),impact.area,participant.position())) {++impact.next;continue;}
            }
            ++state_.targetAttempts;
            if(impact.missileDamage) {
                if(!source.id() || !participant.aliveIn(impact.area) || (!impact.environment && !impact.collideFriend && !participants.canHarm(source,participant)) ||
                    (impact.nativeNextHit && (!impact.missileContact || impact.missileContact->targetIndex!=impact.next) &&
                        !ports_.effects.missileHitAllowed(participant.id(),tick.tick))) {++impact.next;continue;}
                if(!impact.missileContact || impact.missileContact->targetIndex!=impact.next) impact.binding=participants.bind(source);
                const auto prepared=prepareMissileContact(impact,source,participant,area->generation,tick);
                if(prepared==DomainStatus::Capacity) return StepStatus::Blocked;
                auto &contact=*impact.missileContact;
                if(impact.nativeDrainHeld && impact.nativeSkill && !impact.nativeSkill->hit) {
                    impact.nativeSkill->heldPrepared=true;return StepStatus::Complete;
                }
                if(prepared!=DomainStatus::Applied || !contact.hit || contact.blocked || contact.avoidance!=WeaponAvoidance::None) {
                    if(prepared==DomainStatus::Applied && contact.hit) {
                        const auto completed=finishMissileArmor(impact,participants.find(impact.targets[impact.next]),tick);
                        if(completed==DomainStatus::Capacity) return StepStatus::Blocked;
                    }
                    ++impact.next;continue;
                }
                const auto source=participants.find(impact.source),participant=participants.find(impact.targets[impact.next]);
                const auto *monsterSource=source.monster;
                if(!source.id() || !participant.aliveIn(impact.area)) {++impact.next;continue;}
                if(!contact.resultSuccessful) {
                    const auto result=ports_.effects.missileReaction(participant.id(),source.id(),contact.damage.getHit.value_or(false),
                        contact.damage.knockback,contact.damage.hitClass,tick.tick);
                    if(isDeferred(result.status)) return StepStatus::Blocked;
                    ++impact.next;continue;
                }
                auto rolled=contact.damage;
                impact.knockback=rolled.knockback;impact.softHit=rolled.softHit;
                if(!impact.damageScaled) scaleHitDamage(rolled,damageClassPercentage(
                    {bool(source.player),monsterSource && monsterSource->hireling,monsterSource && monsterSource->revived(),monsterSource && monsterSource->rule.boss,monsterSource && monsterSource->rule.primeEvil},
                    {bool(participant.player),participant.monster && participant.monster->hireling,participant.monster && participant.monster->revived(),participant.monster && participant.monster->rule.boss,participant.monster && participant.monster->rule.primeEvil},
                    monsterSource?monsterSource->rule.hirelingBossDamagePercent:100));
                auto hitClass=state_.hitClassCursor;rolled.hitClass=monsterDamageHitClass(rolled,rolled.hitClass,hitClass);
                if(participant.player) {
                    const auto &player=*participant.player;
                    auto execution=std::make_shared<PlayerContactExecution>();execution->targetIndex=impact.next;
                    execution->victim={player.player,player.actor,player.area,area->generation,0,tick.tick};
                    execution->target=player.actor;execution->hit=rolled;execution->states=impact.monsterStates;
                    execution->adjustment.credit=impact.binding.credit;execution->hitClass=hitClass;
                    execution->adjustment.coldPierce=impact.coldPierce;execution->adjustment.poisonPierce=impact.poisonPierce;
                    if(source.player) {
                        const auto *sourceArea=ports_.areas.find(source.player->area);
                        if(sourceArea) execution->attacker={source.player->player,source.id(),source.player->area,sourceArea->generation,0,tick.tick};
                    }
                    execution->adjustment.crushing=contact.playerExtras.crushing;
                    if(contact.playerExtras.woundRate>0) execution->adjustment.wound=OpenWoundsApplication{contact.playerExtras.woundRate,source.id(),impact.binding.credit,tick.tick+200,tick.tick+1};
                    execution->extras=contact.playerExtras;execution->itemEvents=std::move(contact.itemEvents);
                    execution->remainingLife=std::max<int64_t>(0,int64_t(player.persistent.player.hp.raw())-contact.playerExtras.crushing);
                    execution->nativeMissile=execution->hostile=true;execution->missile=!impact.melee;
                    impact.playerContact=std::move(execution);
                } else {
                    const auto &unit=*participant.monster;
                    auto execution=std::make_shared<MonsterContactExecution>();execution->targetIndex=impact.next;execution->target=unit.id;
                    execution->channels=rolled.channels;execution->lifeDrain=rolled.lifeDrain;
                    execution->manaDrain=rolled.mana;execution->staminaDrain=rolled.stamina;
                    execution->cold=rolled.coldFrames;execution->stun=rolled.stunFrames;execution->hitClass=rolled.hitClass;
                    execution->statuses={impact.source,rolled.burn,rolled.burnFrames};
                    execution->freeze=rolled.freezeFrames;execution->getHit=rolled.getHit;
                    execution->nativeMissile=execution->hostile=true;
                    execution->extras=contact.extras;execution->itemEvents=std::move(contact.itemEvents);
                            if(rolled.channels[5]>0) execution->poison=PoisonApplication{rolled.channels[5],rolled.poisonFrames,impact.monsterStates.poison.id};
                    const auto modifiers=ports_.effects.combatModifiers(source.id(),tick.tick);
                    execution->pierce={0,0,modifiers.firePierce,modifiers.lightningPierce,modifiers.coldPierce,modifiers.poisonPierce};
                    const auto &flags=impact.missileRolled->second;
                    execution->bypass=(unit.rule.undead && flags.bypassUndead) || (unit.rule.demon && flags.bypassDemon) || (!unit.rule.undead && !unit.rule.demon && flags.bypassBeasts);
                    impact.monsterContact=std::move(execution);
                }
                // Hit-class selection precedes synchronous damage callbacks.
                // A reflected contact may advance the cursor again; never
                // restore this older cursor after the target HP publication.
                state_.hitClassCursor=hitClass;
                continue;
            }
            if(monsterSource && (!owner || (impact.monsterHit && !impact.weapon))) {
                if(!participants.canHarm(source,participant)) {++impact.next;continue;}
                const PlayerState *player=participant.player;
                const auto *pet=participant.monster;
                if(!participant.aliveIn(impact.area)) {++impact.next;continue;}
                if(pet && !pet->combatCompanion() && (!monsterSource || !participants.canHarm(source,participant))) pet=nullptr;
                if (!player && !pet) {++impact.next;continue;}
                if(impact.nextDelay && ((player && !ports_.effects.missileHitAllowed(player->actor,tick.tick)) || (pet && pet->nextHitTick>tick.tick))) {++impact.next;continue;}
                if(!ports_.effects.reactionCapacity()) {blocked=true;break;}
                if(!ports_.events.hasCapacity(4,2)) {blocked=true;break;}
                const bool stateOnly=impact.monsterHit && impact.monsterHit->slowFrames &&
                    std::all_of(impact.monsterHit->channels.begin(),impact.monsterHit->channels.end(),[](auto n){return n==0;});
                if(!stateOnly) {
                    const auto defended=directDefense(impact,source,participant,tick);
                    if(!defended) {if(isDeferred(defended.status)) return StepStatus::Blocked;++impact.next;continue;}
                    if(!*defended.value) {++impact.next;continue;}
                }
                MonsterHit rolled=impact.monsterHit.value_or(MonsterHit{});
                rolled.coldFrames=std::max(rolled.coldFrames,impact.coldFrames);rolled.poisonFrames=std::max(rolled.poisonFrames,impact.poisonFrames);
                rolled.stunFrames=std::max(rolled.stunFrames,impact.stunFrames);rolled.knockback=rolled.knockback || impact.knockback;
                if(!impact.monsterHit) rolled.channels[size_t(impact.type)]=impact.currentDamage();
                if(!impact.damageScaled) scaleHitDamage(rolled,damageClassPercentage({false,monsterSource->hireling,monsterSource->revived(),monsterSource->rule.boss,monsterSource->rule.primeEvil},
                    {bool(player),pet && pet->hireling,pet && pet->revived(),pet && pet->rule.boss,pet && pet->rule.primeEvil},monsterSource->rule.hirelingBossDamagePercent));
                auto hitClassCursor=state_.hitClassCursor;
                if(!stateOnly) rolled.hitClass=monsterDamageHitClass(rolled,impact.hitClass,hitClassCursor);
                if(stateOnly && pet) {
                    const auto applied=ports_.monsters.slow(pet->id,impact.monsterStates.slow.id,rolled.slowPercent,rolled.slowFrames,tick.tick,impact.source);
                    if(isDeferred(applied.status)) return StepStatus::Blocked;
                    ++impact.next;continue;
                }
                if(player) {
                    const ActorContext actor{player->player,player->actor,player->area,area->generation,0,tick.tick};
                    effects::PlayerHitAdjustment adjustment;adjustment.credit=impact.binding.credit;
                    auto execution=std::make_shared<PlayerContactExecution>();execution->targetIndex=impact.next;
                    execution->victim=actor;execution->target=player->actor;execution->hit=rolled;execution->states=impact.monsterStates;
                    execution->adjustment=adjustment;
                    execution->hitClass=hitClassCursor;execution->missile=!impact.melee;execution->hostile=true;
                    impact.playerContact=std::move(execution);
                } else {
                    const auto received=ports_.effects.unitDamageChannels(pet->id,rolled.channels,tick.tick);
                    auto execution=std::make_shared<MonsterContactExecution>();execution->targetIndex=impact.next;execution->target=pet->id;
                    execution->channels=rolled.channels;execution->lifeDrain=std::max<int64_t>(0,rolled.lifeDrain);
                    execution->absorbed=received.absorbed;execution->physical=received.channels[0].dealt;
                    execution->cold=rolled.coldFrames;execution->stun=rolled.stunFrames;execution->hitClass=rolled.hitClass;
                    execution->statuses={impact.source,rolled.burn,rolled.burnFrames};
                    if(rolled.channels[5]>0) execution->poison=PoisonApplication{received.channels[5].dealt,rolled.poisonFrames,impact.monsterStates.poison.id};
                    execution->callbacks=ports_.effects.prepareDamageEvents(impact.source,pet->id,execution->physical,execution->amount,!impact.melee,impact.returnFire,tick.tick,impact.reaction);
                    execution->hostile=true;impact.monsterContact=std::move(execution);
                }
                continue;
            }
            const auto *target = participant.monster;
            if(impact.healingContact) {
                if(!participant.aliveIn(impact.area) || !participants.petOrPartyAlly(source,participant,ports_.social)) {++impact.next;continue;}
                if(!impact.healingComputed) {
                    auto &rolls=*impact.healingRolls;
                    if(!rolls.formulas) throw std::logic_error("Missing native Holy Bolt formula catalog");
                    const auto sampled=source.monster?ports_.effects.unitStatValues(source.id(),tick.tick):std::optional<StatSnapshot>{};
                    if(source.monster && !sampled) throw std::logic_error("Missing native Holy Bolt owner attributes");
                    auto input=source.player?skills::formulaInput(*source.player):skills::formulaInput(*source.monster,*sampled);
                    SkillFormulaValues values(*rolls.formulas,input);
                    const auto minimum=values.calculation(FormulaDomain::Skill,rolls.skill,rolls.level,"calc1");
                    const auto maximum=values.calculation(FormulaDomain::Skill,rolls.skill,rolls.level,"calc2");
                    if(!minimum || !maximum) throw std::logic_error("Missing native Holy Bolt healing formula");
                    const auto low=nativeShiftLeft(*minimum,8),high=nativeShiftLeft(*maximum,8);
                    impact.healing=nativeAdd(low,int32_t(limitedRandom(rolls.random,uint32_t(nativeSubtract(high,low)))));
                    if(impact.healing<=0) impact.healing=rollMissileDamageValue(rolls.data.physical,0,rolls.random);
                    impact.healingComputed=true;
                }
                if(!impact.healingCommitted) {
                    DomainResult<> healed;
                    if(participant.player) {
                        const auto &patient=*participant.player;
                        const ActorContext actor{patient.player,patient.actor,patient.area,area->generation,0,tick.tick};
                        healed=ports_.transactions.resources(actor,patient.characterRevision,Fixed8::fromRaw(std::min<int64_t>(patient.totals.character.maxLifeFixed.raw(),int64_t(patient.persistent.player.hp.raw())+impact.healing)),patient.persistent.player.mana,patient.persistent.player.stamina);
                    } else healed=ports_.monsters.heal(participant.id(),impact.healing);
                    if(isDeferred(healed.status)) {blocked=true;break;}
                    if(!healed) {++impact.next;continue;}
                    impact.healingCommitted=true;
                }
                if(impact.healingOverlay>0 && !ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},impact.area},
                    {OverlayFact{participant.id(),uint8_t(participant.player?0:1),impact.area,impact.healingOverlay}}})) {blocked=true;break;}
                ++impact.next;continue;
            }
            if(participant.player && owner) {
                const auto result=resolvePlayerImpact(impact,source,*owner,*participant.player,area->generation,tick);
                if(result==DomainStatus::Capacity) {blocked=true;break;}
                if(impact.phase==ImpactPhase::Contact) ++impact.next;
                continue;
            }
            if (!target || !participants.canHarm(source,participant) || target->life <= 0 || target->area != impact.area ||
                (impact.undeadOnly && !target->rule.undead) ||
                (impact.nextDelay && target->nextHitTick > tick.tick)) { ++impact.next; continue; }
            int64_t amount = impact.currentDamage();
            int64_t absorbed=0;
            const int sourcePercent=monsterSource?hirelingDamagePercent(*monsterSource,*target):100;
            const auto scale=[&](int64_t value){return sourcePercent==100 || value<=0?nativeChecked(value):nativePercentage(nativeChecked(value),sourcePercent,100);};
            auto sourceModifiers=hirelingSource?monsterSource->petStats.attributes.combat:owner->totals.character.combat;
            if(monsterSource) mergeCombatModifiers(sourceModifiers,ports_.effects.unitModifiers(monsterSource->id,tick.tick).combat);
            const std::array<int,6> pierce{0,0,sourceModifiers.firePierce,sourceModifiers.lightningPierce,
                impact.coldPierce?impact.coldPierce:sourceModifiers.coldPierce,impact.poisonPierce?impact.poisonPierce:sourceModifiers.poisonPierce};
            int64_t physical=0;std::array<int64_t,6> contactChannels{};
            const auto received=ports_.effects.unitDamageFixed(target->id,scale(amount),impact.type,tick.tick,pierce[size_t(impact.type)]);
            amount=received.dealt;absorbed=received.absorbed;
            contactChannels[size_t(impact.type)]=scale(impact.currentDamage());
            const auto cold=impact.coldFrames;
            std::optional<PoisonApplication> poison;
            const auto rate=impact.type==DamageType::Poison?impact.currentDamage():0;
            const auto frames=impact.poisonFrames;
            if(rate>0) poison=PoisonApplication{ports_.effects.unitDamageFixed(target->id,scale(rate),DamageType::Poison,tick.tick,pierce[5]).dealt,frames,owner->rules.skills->poisonState,{},pierce[5]};
            if(impact.type==DamageType::Poison) amount=0;
            const ActorContext itemActor{owner->player,owner->actor,owner->area,area->generation,0,tick.tick};
            auto itemEvents=hirelingSource?DomainResult<reactions::ItemEventPlan>{DomainStatus::Applied,reactions::ItemEventPlan{}}:
                ports_.itemTriggers.prepare(itemActor,{ItemSkillEvent::Kill},target->id,target->position);
            if(!itemEvents) {blocked=true;break;}
            if(poison && monsterSource && monsterSource->hireling) poison->actualSource=monsterSource->id;
            auto execution=std::make_shared<MonsterContactExecution>();execution->targetIndex=impact.next;execution->target=target->id;
            execution->channels=contactChannels;execution->pierce=pierce;execution->amount=amount;execution->absorbed=absorbed;execution->physical=physical;
            execution->cold=cold;execution->stun=impact.stunFrames;
            execution->statuses.source=impact.source;
            if(impact.monsterHit) {execution->statuses.burn=impact.monsterHit->burn;execution->statuses.burnFrames=impact.monsterHit->burnFrames;}
            MonsterHit classification;classification.channels=contactChannels;
            auto hitClassCursor=state_.hitClassCursor;
            execution->hitClass=monsterDamageHitClass(classification,impact.hitClass,hitClassCursor);
            state_.hitClassCursor=hitClassCursor;
            execution->poison=std::move(poison);execution->itemEvents=std::move(*itemEvents.value);
            execution->callbacks=ports_.effects.prepareDamageEvents(impact.source,target->id,physical,amount,
                !impact.melee && (!impact.weapon || impact.weapon->projectile),impact.returnFire,tick.tick,impact.reaction);
            impact.monsterContact=std::move(execution);
            continue;
        }
        if(blocked) return StepStatus::Blocked;
        if (impact.next == impact.targets.size()) { state_.spellTargets -= impact.targets.size(); it = state_.spells.erase(it); }
        else ++it;
    }
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}
