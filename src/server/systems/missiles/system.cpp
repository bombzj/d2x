#include "system.hpp"
#include "runtime.hpp"
#include "server/systems/combat/system.hpp"
#include <stdexcept>
#include <algorithm>
#include <cstddef>
#include "server/systems/monsters/system.hpp"
#include "server/systems/monsters/native_birth.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/companions/system.hpp"
#include "gameplay/skills/behavior.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "core/random.hpp"
namespace d2x::server::missiles {
System::System(Ports ports) : ports_(ports), runtime_(std::make_unique<Runtime>()) {}
System::~System() = default;
std::vector<EntityId> System::collisionCandidates(const Missile &missile,Vec next) const {
    (void)next;
    std::vector<EntityId> result;
    for(const auto id:nearbyUnits(missile,next)) if(canCollide(missile,id)) result.push_back(id);
    return result;
}
bool System::canCollide(const Missile &missile,EntityId id) const {
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto unit=participants.find(id);
    if(!unit.id() || !unit.damageable()) return false;
    if(!missile.nativeDamage) return participants.canHarm(missile.owner,id);
    const auto &rule=missile.nativeDamage->rule;
    if(rule.lastCollide && id==(missile.lastHit?missile.lastHit:missile.owner)) return false;
    if(missile.currentFrame()>missile.nativeActivateFrame || rule.collisionMode==0 || rule.collisionMode==4 || rule.collisionMode==6 || rule.collisionMode==7) return false;
    if(rule.collisionMode==1 && !unit.player && unit.alignment()!=UnitAlignment::Good) return false;
    if((rule.collisionMode==2 || rule.collisionMode==5) && !unit.monster) return false;
    if(participants.find(missile.owner).id() && !missile.environment && !rule.collideFriend && !participants.canHarm(missile.owner,id)) return false;
    return !rule.nextHit || ports_.effects.missileHitAllowed(id,missile.executionTick);
}
DomainStatus System::commitAdvance(const Missile &missile, Advance &plan, TickContext tick) {
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    // The interpreted program records publication operations in call order.
    // Finish each accepted contact before moving to the next child or target.
    while(plan.nextOperation<plan.operations.size()) {
        const auto operation=plan.operations[plan.nextOperation];
        if(operation.kind==Advance::OperationKind::LevelSpawn) {
            auto &entry=plan.levelSpawns.at(operation.index);
            if(!entry.publication) {
                auto prepared=ports_.monsters.prepareLevelSpawn(entry.area,entry.observer,entry.position,tick.tick,
                    ports_.spatial.nativeCollisionSample());
                if(!prepared) return prepared.status;
                entry.publication=std::move(*prepared.value);
            }
            if(!entry.publication->validate()) return DomainStatus::Stale;
            entry.publication->publish();plan.started=true;
        } else if(operation.kind==Advance::OperationKind::NativeCollision) {
            auto &flight=*plan.collisionFlight;
            if(flight.nativeExpired) {
                plan.next.position=flight.end;
                const auto native=nativeHit(plan,{});
                if(!native) {if(plan.next.enemy) throw std::logic_error("Unsupported native monster missile hit program");else projectileImpact(plan,{});}
                plan.finished=!native || !(*native&4);
            } else {
                if(!flight.wall) flight.contacts=collisionContacts(plan.next,flight.end);
                continueMissileFlight(plan);
            }
        } else if(operation.kind==Advance::OperationKind::ContactContinue) {
            continueMissileFlight(plan);
        } else if(operation.kind==Advance::OperationKind::ContactPrelude) {
            auto &contact=plan.impacts.at(operation.index);auto &flight=*plan.collisionFlight;
            const auto &rule=plan.next.nativeDamage->rule;
            if(!flight.piercePrepared) {
                flight.piercing=false;
                if(rule.pierce && plan.next.pierces>0) {
                    const auto values=ports_.effects.unitStatValues(missile.owner,tick.tick);
                    if(values && rule.damageStats &&
                        (values->get({rule.damageStats->names.at("skill_pierce"),0}) || values->get({rule.damageStats->names.at("item_pierce"),0}))) {
                        --plan.next.pierces;flight.piercing=true;
                    }
                }
                flight.piercePrepared=true;
            }
            plan.started=true;
            const auto result=ports_.combat.missilePrelude(contact,tick);
            if(isDeferred(result.status)) return result.status;
            const bool ignored=result && *result.value==combat::MissilePreludeResult::Ignore;
            const bool hit=result && *result.value==combat::MissilePreludeResult::Hit;
            if(ignored) {plan.operations.push_back({Advance::OperationKind::ContactContinue,0});++plan.nextOperation;continue;}
            auto damage=contact;plan.started=true;
            flight.continueFlight=hit && ((!rule.killOnHit && !flight.forceRemove) || flight.piercing);
            bool direct=true;
            if(hit || (missile.enemy?missile.enemy->rule.alwaysExplode:missile.rules->alwaysExplodingMissiles.contains(missile.definition))) {
                if(const auto native=nativeHit(plan,damage.targets.front())) {
                    direct=(*native&4)==0 && (*native&2)!=0;
                    flight.continueFlight=(*native&4) || (!(*native&1) && !flight.forceRemove) || flight.piercing;
                } else if(flight.weapon) weaponImpact(plan,plan.next.position);
                else if(missile.enemy) throw std::logic_error("Unsupported native monster missile hit program");
                else direct=projectileImpact(plan,damage.targets.front());
            }
            // Native SrvHit executes before FullDamageParams / SrvDmg.
            if(hit && direct) plan.addImpact(std::move(damage));
            plan.operations.push_back({Advance::OperationKind::ContactContinue,0});
        } else if(operation.kind==Advance::OperationKind::MissileField) {
            const auto &field=plan.missileFields.at(operation.index);
            ports_.areas.setMissileMask(field.area,field.position,field.size,true);plan.started=true;
        } else if(operation.kind==Advance::OperationKind::BoneSegment) {
            const auto &segment=plan.boneSegments.at(operation.index);
            const auto born=ports_.companions.growBoneWall(plan.next.owner,segment.root,plan.next.skill.sourceId,
                plan.next.skill.rank,segment.area,segment.position,tick.tick);
            if(isDeferred(born.status)) return born.status;
            if(born.status==DomainStatus::Stale || born.status==DomainStatus::Conflict) return born.status;
            if(born && *born.value) {plan.next.parameters[1]=nativeSubtract(plan.next.parameters[1],1);plan.started=true;}
        } else if(operation.kind==Advance::OperationKind::OwnerSeed) {
            const auto &loan=plan.formulaSeeds.at(operation.index);const auto source=participants.find(loan.owner);
            DomainResult<> consumed;
            if(source.monster) consumed=ports_.monsters.consumeRandom(loan.owner,loan.expected,loan.next);
            else if(source.player) {
                const auto &owner=*source.player;const auto *area=ports_.areas.find(owner.area);
                if(!area) return DomainStatus::Stale;
                consumed=ports_.transactions.consumeRandom({owner.player,owner.actor,owner.area,area->generation,0,tick.tick},loan.expected,loan.next);
            } else {++plan.nextOperation;continue;}
            if(isDeferred(consumed.status)) return consumed.status;
            if(!consumed) throw std::logic_error("Native missile formula owner seed changed");
            plan.started=true;
        } else if(operation.kind==Advance::OperationKind::SkillState) {
            auto &state=plan.skillStates.at(operation.index);const auto source=participants.find(state.source);
            if(!source.id() || !participants.find(state.target).id()) {++plan.nextOperation;continue;}
            if(!state.duration) {
                const auto catalog=source.player?source.player->rules.skills->formulas:source.monster->rule.formulas;
                if(!catalog || !catalog->skills.contains(state.skill)) {++plan.nextOperation;continue;}
                const auto expected=source.player?source.player->combatRandom:source.monster->combatRandom;auto random=expected;
                const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
                    if(low>=high) return low;
                    return nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
                };
                const auto sampled=source.monster?ports_.effects.unitStatValues(source.id(),tick.tick):std::optional<StatSnapshot>{};
                if(source.monster && !sampled) throw std::logic_error("Missing native missile skill state owner");
                auto input=source.player?skills::formulaInput(*source.player,draw):skills::formulaInput(*source.monster,*sampled,draw);
                SkillFormulaValues values(*catalog,input);const auto duration=values.calculation(FormulaDomain::Skill,state.skill,state.rank,
                    state.procedure==effects::NativeSkillStateProcedure::SpiderGoo?"calc4":"auralencalc");
                if(!duration) throw std::logic_error("Missing native missile skill state duration");
                state.duration=state.procedure==effects::NativeSkillStateProcedure::FingerMageSpider || state.procedure==effects::NativeSkillStateProcedure::SpiderGoo?
                    std::max(*duration,state.minimumDuration):*duration;
                state.random=std::pair{expected,random};
            }
            if(!state.seedCommitted) {
                DomainResult<> consumed;
                if(source.monster) consumed=ports_.monsters.consumeRandom(source.id(),state.random->first,state.random->second);
                else {
                    const auto &owner=*source.player;const auto &area=ports_.areas.at(owner.area);
                    consumed=ports_.transactions.consumeRandom({owner.player,owner.actor,owner.area,area.generation,0,tick.tick},state.random->first,state.random->second);
                }
                if(isDeferred(consumed.status)) return consumed.status;
                if(!consumed) throw std::logic_error("Native missile skill duration seed changed");
                state.seedCommitted=true;plan.started=true;
            }
            const auto applied=ports_.effects.skillAuraState(state.source,state.target,state.skill,state.rank,*state.duration,tick.tick,state.procedure);
            if(isDeferred(applied.status)) return applied.status;
            if(applied.status==DomainStatus::Conflict || applied.status==DomainStatus::Stale) return applied.status;
            if(state.posted) *state.posted=bool(applied);
        } else if(operation.kind==Advance::OperationKind::TowerOpen) {
            const auto result=ports_.objects.openTowerChest(EntityId{uint32_t(operation.index)},tick.tick);
            if(!result) return result.status;
            plan.started=true;
        } else if(operation.kind==Advance::OperationKind::TowerGold) {
            auto &entry=plan.towerGold.at(operation.index);
            if(!entry.resolved) {
                const auto global=entry.observer+ports_.areas.at(entry.area).definition.origin;
                const auto room=ports_.areas.roomAtPosition(entry.area,entry.observer,int(global.x),int(global.y));
                if(room) if(const auto position=ports_.areas.nativeFreeSpace(*room,int(entry.position.x),int(entry.position.y),1,ports_.spatial.nativeCollisionSample()))
                    entry.destination=GroundLocation{position->room.area,position->position};
                entry.resolved=true;
            }
            if(entry.destination) {
                const auto result=ports_.objects.towerGold(*entry.destination,tick.tick);
                if(!result) return result.status;
                plan.started=true;
            }
        } else if(operation.kind==Advance::OperationKind::TowerSound) {
            const auto object=ports_.objects.read().objects.find(EntityId{uint32_t(operation.index)});
            if(object!=ports_.objects.read().objects.end()) {
                const auto result=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},object->second.area},
                    {SoundFact{object->first,2,object->second.area,0x5c}}});
                if(!result) return result.status;
                plan.started=true;
            }
        } else if(operation.kind==Advance::OperationKind::OrbMist) {
            const auto result=ports_.objects.openOrbMist(EntityId{uint32_t(operation.index)},tick.tick);
            if(!result) return result.status;
            plan.started=true;
        } else if(operation.kind==Advance::OperationKind::Redemption) {
            auto &entry=plan.redemptions.at(operation.index);
            if(!entry.affected) {
                auto prepared=ports_.effects.prepareRedeemedCorpse(entry.corpse,tick.tick);
                if(isDeferred(prepared.status)) return prepared.status;
                if(!prepared) {entry.affected=false;++plan.nextOperation;continue;}
                const auto chance=entry.skipChance?0:rollRandom(plan.damageRolls->random)%100;
                const auto calculate=[&](std::string_view field) {
                    const auto value=missileSelfFormula(plan,field,entry.skill,1);
                    if(!value) throw std::logic_error("Missing missile Redemption formula");
                    return *value;
                };
                entry.affected=entry.skipChance || chance<uint32_t(calculate("calc1"));
                if(*entry.affected) {
                    // A real missile has zero MAXHP/MAXMANA. These writes stay
                    // on that missile; they must never heal its dead owner.
                    auto &resources=plan.next.nativeResources;
                    resources[0]=std::min(nativeAdd(resources[0],nativeShiftLeft(calculate("calc2"),8)),0);
                    resources[1]=std::min(nativeAdd(resources[1],nativeShiftLeft(calculate("calc3"),8)),0);
                    entry.publication=std::move(*prepared.value);
                }
            }
            if(entry.publication) {
                if(!ports_.effects.validateAuraUnits(*entry.publication)) return DomainStatus::Stale;
                const auto *corpse=ports_.monsters.find(entry.corpse);if(!corpse) return DomainStatus::Stale;
                auto facts=entry.publication->facts;
                const auto sent=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},corpse->area},std::move(facts)});
                if(!sent) return sent.status;
                ports_.effects.publishAuraUnits(*entry.publication);entry.publication.reset();plan.started=true;
            }
        } else if(operation.kind==Advance::OperationKind::CairnPortal) {
            const auto result=ports_.quests.cairnFx(plan.next.area,plan.next.position,
                plan.next.nativeDamage->rule.serverParameters[3],tick.tick);
            if(isDeferred(result.status)) return result.status;
            if(!result) throw std::logic_error("Invalid Cairn missile world publication");
            plan.started=true;
        } else if(operation.kind==Advance::OperationKind::ReleaseRoom) {
            ports_.world.retainControllerRoom(plan.next.area,plan.next.position,false);plan.started=true;
        } else if(operation.kind==Advance::OperationKind::BaalFx) {
            const auto result=ports_.quests.baalFx(plan.next.area,plan.next.position);
            if(isDeferred(result.status)) return result.status;
            if(!result) throw std::logic_error("Invalid Baal missile world publication");
            plan.started=true;
        } else if(operation.kind==Advance::OperationKind::BaalWave) {
            const auto &wave=plan.baalWaves.at(operation.index);
            const auto born=ports_.quests.spawnBaalWave(wave.area,wave.position,wave.mappedPreset,tick.tick);
            if(!born) return born.status;
            plan.started=true;
        } else if(operation.kind==Advance::OperationKind::Fear) {
            const auto result=ports_.effects.nativeFear(plan.fears.at(operation.index),tick.tick);
            if(isDeferred(result.status) || result.status==DomainStatus::Conflict || result.status==DomainStatus::Stale) return result.status;
        } else if(operation.kind==Advance::OperationKind::TargetBurst) {
            const auto burst=plan.targetBursts.at(operation.index);
            if(participants.find(plan.next.owner).id()) {
                const auto &area=ports_.areas.at(plan.next.area);
                const auto recipients=ports_.effects.nativeAuraCandidateRecipients({plan.next.owner,unitArea(plan.next.owner),burst.range,
                    burst.filter,burst.checkNoAura,plan.next.position+area.definition.origin,effects::NativeAuraTargetMode::TargetRange,tick.tick});
                const auto previous=plan.operations.size();int index{};
                for(const auto id:recipients) {
                    if(index++>=burst.count) break;
                    if(const auto point=unitPosition(plan.next,id)) planNativeChild(plan,burst.definition,plan.next.position,*point-plan.next.position);
                }
                // Synchronous enumeration inserts children before the caller's
                // already queued damage/continuation, preserving native order.
                std::rotate(plan.operations.begin()+std::ptrdiff_t(plan.nextOperation+1),plan.operations.begin()+std::ptrdiff_t(previous),plan.operations.end());
            }
        } else if(operation.kind==Advance::OperationKind::Resource) {
            const auto &write=plan.resources.at(operation.index);const auto target=participants.find(write.target);
            if(!target.id()) {++plan.nextOperation;continue;}
            const auto region=unitArea(write.target);std::vector<DomainFact> facts;
            if(write.overlay>0) facts.emplace_back(OverlayFact{write.target,uint8_t(target.player?0:1),region,write.overlay});
            DomainResult<> written;
            if(target.player) {
                const auto &player=*target.player;const auto &area=ports_.areas.at(region);
                const ActorContext actor{player.player,player.actor,region,area.generation,0,tick.tick};
                written=ports_.transactions.resources(actor,player.characterRevision,
                    write.kind==Advance::ResourceWrite::Kind::Life?Fixed8::fromRaw(write.value):player.persistent.player.hp,
                    write.kind==Advance::ResourceWrite::Kind::Mana?Fixed8::fromRaw(write.value):player.persistent.player.mana,
                    player.persistent.player.stamina,std::move(facts));
            } else {
                const auto sources=ports_.effects.unitStatSources(write.target,tick.tick);
                attributes::MonsterResourceTrace resources(*target.monster,tick.tick);resources.sampleSources(sources);
                if(write.kind==Advance::ResourceWrite::Kind::Life) resources.seedLife(Fixed8::fromRaw(write.value));
                else resources.writeBaseStat("mana",write.value,sources);
                auto change=resources.take();if(!ports_.monsters.canCommitLife(change)) return DomainStatus::Stale;
                if(!facts.empty() && !ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},region},std::move(facts)})) return DomainStatus::Capacity;
                if(!ports_.monsters.commitLife(std::move(change))) throw std::logic_error("Native missile resource reservation changed");
                written={DomainStatus::Applied,std::monostate{}};
            }
            if(isDeferred(written.status)) return written.status;
            plan.started=true;
        } else if(operation.kind==Advance::OperationKind::TargetManaDrain) {
            const auto &drain=plan.manaDrains.at(operation.index);const auto *target=ports_.players.findActor(drain.target);
            if(!target || !target->entered) {++plan.nextOperation;continue;}
            const auto current=target->persistent.player.mana.raw();
            if(current>0) {
                const auto loss=std::clamp(nativePercentage(current,drain.percentage,100),1,current);
                const auto &area=ports_.areas.at(target->area);
                const ActorContext actor{target->player,target->actor,target->area,area.generation,0,tick.tick};
                const auto result=ports_.transactions.resources(actor,target->characterRevision,target->persistent.player.hp,
                    Fixed8::fromRaw(nativeSubtract(current,loss)),target->persistent.player.stamina);
                if(isDeferred(result.status)) return result.status;
            }
            plan.started=true;
        } else if(operation.kind==Advance::OperationKind::SourceHeal) {
            auto &healing=plan.sourceHeals.at(operation.index);
            const auto source=participants.find(healing.owner);
            if(!source.id()) {++plan.nextOperation;continue;}
            if(!healing.amount) {
                if(!plan.damageRolls || !plan.next.nativeDamage) throw std::logic_error("Missing native source healing ranges");
                const auto target=participants.find(plan.next.lastHit);const auto *unit=target.monster;
                const auto rolled=rollMissileElementDamage(plan.damageRolls->data,plan.next.nativeDamage->rule.damageElement,
                    plan.damageRolls->mastery,{bool(target.id()),unit && unit->rule.demon,unit && unit->rule.undead,0},plan.damageRolls->random);
                healing.amount=std::max(0,rolled.value);
            }
            DomainResult<> result;
            if(source.monster) {
                attributes::MonsterResourceTrace resources(*source.monster,tick.tick);
                resources.sampleSources(ports_.effects.unitStatSources(source.id(),tick.tick));
                resources.seedLife(Fixed8::fromRaw(std::min(resources.read().maximum.raw(),nativeAdd(resources.read().life.raw(),*healing.amount))));
                auto change=resources.take();
                if(!ports_.monsters.canCommitLife(change)) return DomainStatus::Stale;
                if(!ports_.monsters.commitLife(std::move(change))) throw std::logic_error("Source healing lost its native resource reservation");
                result={DomainStatus::Applied,std::monostate{}};
            } else {
                const auto &owner=*source.player;const auto *area=ports_.areas.find(owner.area);
                if(!area) {++plan.nextOperation;continue;}
                transactions::CharacterEdit edit{{owner.player,owner.actor,owner.area,area->generation,0,tick.tick},
                    owner.inventoryRevision,owner.characterRevision,owner.persistent.player};
                edit.player.hp=Fixed8::fromRaw(std::min(owner.totals.character.maxLifeFixed.raw(),nativeAdd(edit.player.hp.raw(),*healing.amount)));
                auto transaction=ports_.transactions.prepare(std::move(edit));
                if(!transaction) {if(isDeferred(transaction.status)) return transaction.status;++plan.nextOperation;continue;}
                result=ports_.transactions.commit(std::move(*transaction.value));
            }
            if(isDeferred(result.status)) return result.status;
            plan.started=true;
        } else if(operation.kind==Advance::OperationKind::Curse || operation.kind==Advance::OperationKind::Buff) {
            auto skill=plan.next.skill;skill.manaCost=0;skill.charge.reset();
            const auto *caster=ports_.players.findActor(missile.owner);
            const auto &castArea=ports_.areas.at(caster?caster->area:missile.area);
            const ActorContext actor{missile.player,missile.owner,castArea.definition.id,castArea.generation,0,tick.tick};
            DomainResult<> result;
            if(operation.kind==Advance::OperationKind::Curse) {
                const auto target=plan.curseTargets.at(operation.index);const auto recipient=participants.find(target);
                result=recipient.id() && recipient.aliveIn(unitArea(target))?ports_.effects.curse(actor,skill,recipient.position(),target):DomainResult<>{DomainStatus::Unavailable,{}};
            } else {
                const auto target=plan.buffTargets.at(operation.index);const auto *recipient=ports_.players.findActor(target);
                result=recipient?ports_.effects.skill(actor,skill,recipient->player):ports_.effects.skillUnit(actor,skill,target);
            }
            if(isDeferred(result.status)) return result.status;
            if(result.status==DomainStatus::NotImplemented) throw std::logic_error("Unimplemented missile callback program");
            plan.started=true;
        } else if(operation.kind==Advance::OperationKind::Child) {
            if(!plan.childPublication) {
                if(ports_.ids.cursor()>=UINT32_MAX) return DomainStatus::Capacity;
                auto gameRandom=ports_.random;
                Advance::ChildBuildContext context{gameRandom,tick.tick};
                auto child=plan.children.at(operation.index);
                const auto source=participants.find(child.owner);
                if(!source.player && !source.monster) {++plan.nextOperation;continue;}
                if(const auto recipe=plan.childBuilders.find(operation.index);recipe!=plan.childBuilders.end()) {
                    auto built=recipe->second(child,context);
                    if(!built) {++plan.nextOperation;continue;}
                    child=std::move(*built);
                } else child.random=childRandom(gameRandom);
                if(child.creationRejected || !creationRoom(child) || !townAllowed(child)) {++plan.nextOperation;continue;}
                child.id=EntityId{ports_.ids.cursor()};child.created=child.executionTick=tick.tick;child.expires=tick.tick+uint64_t(child.lifetimeFrames);
                Advance::ChildPublication publication;
                publication.child=child.id;publication.area=child.area;publication.hitTarget=child.hitOnCreation;child.hitOnCreation.reset();
                publication.facts=visuals({child});publication.insertion.emplace(child.id,std::move(child));
                publication.timers=prepareTimers(publication.insertion);
                publication.gameRandom=gameRandom;publication.ownerSeed=std::move(context.ownerSeed);publication.originSeed=std::move(context.originSeed);
                plan.childPublication.emplace(std::move(publication));
                // SUNIT allocation is a retained internal phase. IDs and the
                // game seed are consumed once after all nodes are prepared;
                // output backpressure must not replay or rewind that phase.
            }
            auto &publication=*plan.childPublication;
            if(!publication.identityCommitted) {
                if(publication.ownerSeed) {
                    const auto &loan=*publication.ownerSeed;const auto source=participants.find(loan.owner);
                    DomainResult<> consumed;
                    if(source.monster) consumed=ports_.monsters.consumeRandom(loan.owner,loan.expected,loan.next);
                    else if(source.player) {
                        const auto &owner=*source.player;const auto *area=ports_.areas.find(owner.area);
                        if(!area) throw std::logic_error("Missing missile damage owner area");
                        consumed=ports_.transactions.consumeRandom({owner.player,owner.actor,owner.area,area->generation,0,tick.tick},loan.expected,loan.next);
                    } else throw std::logic_error("Missing missile damage owner during publication");
                    if(isDeferred(consumed.status)) return consumed.status;
                    if(!consumed) throw std::logic_error("Stale missile damage owner seed");
                }
                if(publication.originSeed) {
                    const auto &loan=*publication.originSeed;
                    if(loan.state->random!=loan.expected) throw std::logic_error("Stale native origin missile seed");
                    loan.state->random=loan.next;
                }
                ports_.ids.allocate();ports_.random=publication.gameRandom;plan.started=true;publication.identityCommitted=true;
            }
            if(!publication.published) {
                if(!publication.facts.empty() && !ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},publication.area},publication.facts})) return DomainStatus::Capacity;
                state_.missiles.merge(publication.insertion);publishTimers(publication.timers);publication.published=true;
            }
            if(publication.hitTarget) {
                const auto live=state_.missiles.find(publication.child);
                if(live!=state_.missiles.end()) {
                    if(!publication.immediate) publication.immediate=immediateHit(live->second,*publication.hitTarget,tick.tick);
                    const auto result=commitAdvance(live->second,*publication.immediate,tick);
                    if(result!=DomainStatus::Applied) return result;
                    const bool remove=publication.immediate->finished;
                    if(remove) {
                        ports_.frameEvents.cancelOwner({FrameEventUnit::Missile,publication.child});
                        ports_.areas.removeRoomUnit(publication.child);state_.missiles.erase(live);
                    } else live->second=std::move(publication.immediate->next);
                }
            }
            plan.childPublication.reset();
        } else {
            auto &impact=plan.impacts.at(operation.index);
            if(impact.areaDamage && !impact.areaDamage->rolled) impact.areaDamage->rolled=std::make_shared<combat::MissileAreaRoll>();
            if(impact.areaDamage && !impact.areaDamage->rolled->value && impact.missileDamage) {
                const auto recipient=participants.find(plan.next.lastHit);const auto *unit=recipient.monster;
                const MissileDamageTarget target{bool(recipient.id()),unit && unit->rule.demon,unit && unit->rule.undead,0};
                auto &rolls=*impact.missileDamage;
                impact.areaDamage->rolled->value=impact.areaDamage->full?rollMissileDamageData(rolls.data,rolls.mastery,target,rolls.random):
                    rollMissileElementDamage(rolls.data,impact.areaDamage->element,rolls.mastery,target,rolls.random).hit;
            }
            if(!plan.contactAccepted) {
                auto contact=impact;contact.synchronous=true;
                auto prepared=ports_.combat.prepareSpells({std::move(contact)});
                if(!prepared) {
                    if(!isDeferred(prepared.status)) throw std::logic_error("Invalid staged missile contact");
                    return prepared.status;
                }
                ports_.combat.commitSpells(std::move(*prepared.value));plan.contactAccepted=true;plan.started=true;
            }
            if(ports_.combat.advanceAuraPulse(impact.projectile,impact.occurrence,tick)==StepStatus::Blocked) return DomainStatus::Capacity;
            plan.contactAccepted=false;
        }
        ++plan.nextOperation;
    }
    if(plan.finished && plan.next.nativeDamage) ports_.areas.setMissileMask(plan.next.area,plan.next.position,plan.next.collision.size,false);
    if(plan.damageRolls) plan.next.random=plan.damageRolls->random;
    return DomainStatus::Applied;
}
StepStatus System::advanceFrame(EntityId id,TickContext tick) {
    const auto it=state_.missiles.find(id);if(it==state_.missiles.end()) return StepStatus::Complete;
    const auto &missile=it->second;
    const auto retained=runtime_->pending.find(id);
    const bool accepted=retained!=runtime_->pending.end() && retained->second.started;
    const auto *player=ports_.players.find(missile.player);const auto *area=ports_.areas.find(missile.area);
    const auto *monster=ports_.monsters.find(missile.owner);
    const bool validOwner=player && player->entered && (player->actor==missile.owner ||
        (monster && monster->owner==missile.player));
    // Native owner lookup is global by type/GUID. A warp, death mode or
    // changed alignment does not delete an already allocated missile. Missing
    // owners suppress contacts/child creation while the missile expires.
    if(!accepted && ((!missile.enemy && !missile.nativeDamage && !validOwner) ||
        !area || area->generation!=missile.generation || !townAllowed(missile))) {
        if(area && area->generation==missile.generation) ports_.areas.setMissileMask(missile.area,missile.position,missile.collision.size,false);
        runtime_->pending.erase(id);ports_.areas.removeRoomUnit(id);state_.missiles.erase(it);return StepStatus::Complete;
    }
    if(!accepted && tick.tick<=missile.created) return StepStatus::Complete;
    auto [entry,fresh]=runtime_->pending.try_emplace(id);if(fresh) entry->second=advance(missile,tick);
    auto &plan=entry->second;
    const auto committed=commitAdvance(missile,plan,tick);
    if(committed==DomainStatus::Capacity || committed==DomainStatus::Conflict || committed==DomainStatus::Stale) return StepStatus::Blocked;
    if(committed!=DomainStatus::Applied) throw std::logic_error("Synchronous missile advance lost its publication reservation");
    const bool finished=plan.finished || !moveToRoom(plan.next,plan.next.area==missile.area?missile.position:plan.next.position) || !townAllowed(plan.next);
    if(missile.nativeDamage && missile.nativeDamage->rule.damageFormulas->missiles.at(missile.definition).number("collision")) {
        ports_.areas.setMissileMask(missile.area,missile.position,missile.collision.size,false);
        if(!finished) ports_.areas.setMissileMask(plan.next.area,plan.next.position,plan.next.collision.size,true);
    } else if(finished && plan.next.nativeDamage) ports_.areas.setMissileMask(plan.next.area,plan.next.position,plan.next.collision.size,false);
    it->second=std::move(plan.next);runtime_->pending.erase(entry);
    if(finished) {ports_.areas.removeRoomUnit(id);state_.missiles.erase(it);}
    else ports_.areas.moveRoomUnit(id,it->second.area,it->second.position);
    return StepStatus::Complete;
}
DomainResult<> System::frameEvent(EntityId id,TickContext tick) {
    const auto status=advanceFrame(id,tick);
    if(!state_.missiles.contains(id)) ports_.frameEvents.cancelOwner({FrameEventUnit::Missile,id});
    return status==StepStatus::Blocked?DomainResult<>{DomainStatus::Capacity,{}}:DomainResult<>{DomainStatus::Applied,std::monostate{}};
}
}
