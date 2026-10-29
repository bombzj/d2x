#include "system.hpp"
#include "server/systems/combat/participants.hpp"
#include "runtime.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/missiles/native_birth.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/effects/corpse_explosion.hpp"
#include "server/systems/effects/native_area.hpp"
#include "evaluation.hpp"
#include "native_caster.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/pd2_corpse.hpp"
#include "server/systems/companions/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/combat/system.hpp"
#include "gameplay/skills/bone_spec.hpp"
#include "gameplay/skills/corpse_search_spec.hpp"
#include "gameplay/skills/leap_spec.hpp"
#include "gameplay/skills/grim_ward_spec.hpp"
#include "gameplay/skills/barbarian_sequence.hpp"
#include "gameplay/skills/amazon_sequence.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/skills/amazon_missile.hpp"
#include "gameplay/skills/targeting.hpp"
#include "gameplay/skills/pd2_program.hpp"
#include "server/movement.hpp"
#include <cmath>
#include "server/systems/loot/system.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::skills {
DomainStatus System::activateWhirlwind(Release &pending,const ActorContext &actor,Vec destination) {
    const auto &owner=*ports_.players.find(actor.player);
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    if(pending.motionFrame!=actor.tick) {
        const auto moved=ports_.movement.leapStep(actor,destination,pending.chargeSpeed,MovementCollisionRule{0x804,2});
        if(!moved && isDeferred(moved.status)) return moved.status;
        pending.motionFrame=actor.tick;pending.motionFinished=!moved || *moved.value;
    }
    if(pending.motionFinished) {
        auto result=ports_.effects.removeState(actor,pending.skill.weapon->attackState.id);if(!result) return result.status;
        result=ports_.effects.removeState(actor,pending.skill.weapon->movingState.id);if(!result) return result.status;
        pending.whirlwind=false;state_.casts.at(actor.actor).until=actor.tick+1;
        ports_.movement.execute(actor,{MovementAction::Stop,{},false});return DomainStatus::Applied;
    }
    if(actor.tick>=pending.nextWhirlHit) {
        const size_t count=pending.offhand && pending.nextWhirlHit>state_.casts.at(actor.actor).started+4?2:1;
        while(pending.whirlHand<count) {
            if(pending.nativeMeleeOccurrence) {
                if(ports_.combat.advanceAuraPulse(actor.actor,*pending.nativeMeleeOccurrence,{actor.tick})==StepStatus::Blocked) return DomainStatus::Capacity;
                pending.nativeMeleeOccurrence.reset();pending.nativeCallbackStarted=false;
                ++pending.whirlHand;++pending.whirlwindAttackHand;continue;
            }
            const auto &hand=pending.offhand && pending.whirlwindAttackHand%2?*pending.offhand:*pending.weapon;
            const auto last=owner.totals.equipment.weapons.begin()+owner.totals.equipment.weaponCount;
            const auto current=std::find_if(owner.totals.equipment.weapons.begin(),last,[&](const auto &w){return w.item==hand.item;});
            if(current==last) {pending.whirlHand=count;break;}
            const auto targets=ports_.effects.nativeAuraCandidateRecipients({actor.actor,actor.area,5,0xa783,false,{},effects::NativeAuraTargetMode::OwnerRange,actor.tick});
            const auto successor=nativeSkillSuccessor(targets,pending.unit?ports_.monsters.nativeIdentities().guid(pending.unit):UINT32_MAX,ports_.monsters.nativeIdentities());
            if(successor) {
                WeaponSkillDamage weapon;weapon.weapon=*current;weapon.level=owner.persistent.player.level;
                missiles::Spawn request{actor,pending.skill,{},participants.find(successor).position(),true};request.weapon=weapon;
                const auto hit=ports_.missiles.directContact(request,{successor});if(!hit) return hit.status;
                if(!hit.value->occurrence) return DomainStatus::Unavailable;
                pending.unit=successor;pending.nativeMeleeOccurrence=hit.value->occurrence;pending.nativeCallbackStarted=true;
                continue;
            }
            pending.unit={};++pending.whirlHand;++pending.whirlwindAttackHand;
        }
        const auto start=state_.casts.at(actor.actor).started;
        int period=10;
        const auto &hand=pending.offhand && pending.whirlwindAttackHand%2?*pending.offhand:*pending.weapon;
        if(hand.item) {
            const auto animation=owner.rules.skills->weaponAnimations.find("a1"+owner.totals.equipment.animationClass);
            int frames=45;
            if(animation!=owner.rules.skills->weaponAnimations.end()) {
                const auto &clock=animation->second;
                const int speed=clock.speed*(hand.itemAttackRate+hand.itemAttackSpeed+100)/100;
                if(speed>0) frames=(clock.frames<<8)/speed;
            }
            period=frames<12?4:frames<15?6:frames<18?8:frames<20?10:frames<23?12:frames<26?14:16;
        }
        pending.nextWhirlHit+=uint64_t(pending.nextWhirlHit==start+4?4:period);pending.whirlHand=0;
    }
    pending.tick=actor.tick+1;return DomainStatus::Applied;
}
DomainStatus System::activateLeap(Release &pending,const ActorContext &actor,Vec landing) {
    if(pending.skill.pd2) return activatePd2Leap(pending,actor,landing);
    const auto &owner=*ports_.players.find(actor.player);
    if(pending.landed) return weaponRelease(pending,actor,landing);
    if(!pending.leaping) {
        if(!ports_.areas.at(actor.area).definition.collision.walkable(landing,playerMovement)) return DomainStatus::Unavailable;
        pending.chargeSpeed=float(owner.definition.runVelocity)*25.f/16.f;
        if(pending.chargeSpeed<=0) return DomainStatus::Unavailable;
        auto skill=pending.skill;CombatEffectSpec moving;moving.state=skill.leap->moving;
        moving.duration=uint64_t(std::ceil((landing-owner.position).length()*25.f/pending.chargeSpeed))+5;
        skill.appliedEffect=moving;
        const auto result=ports_.effects.skill(actor,skill);if(!result) return result.status;
        pending.leaping=true;pending.manaPaid=true;return DomainStatus::Applied;
    }
    if(pending.motionFrame!=actor.tick) {
        const auto moved=ports_.movement.leapStep(actor,landing,pending.chargeSpeed);if(!moved) return moved.status;
        pending.motionFrame=actor.tick;pending.motionFinished=*moved.value;
    }
    if(!pending.motionFinished) return DomainStatus::Applied;
    const auto cleared=ports_.effects.removeState(actor,pending.skill.leap->moving.id);if(!cleared) return cleared.status;
    if(pending.skill.leap->attack) {
        const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
        const auto &grid=ports_.areas.at(actor.area).definition.collision;
        auto victim=participants.find(pending.unit);
        if(!victim.aliveIn(actor.area) || !playerMeleeReach(grid,owner.position,victim.position(),victim.size(),pending.weapon->rangeAdder)) {
            const auto targets=ports_.effects.nativeAuraCandidateRecipients({actor.actor,actor.area,pending.weapon->rangeAdder+4,0x20003,false,{},effects::NativeAuraTargetMode::OwnerRange,actor.tick});
            victim=participants.find(nativeSkillSuccessor(targets,UINT32_MAX,ports_.monsters.nativeIdentities()));
        }
        if(!victim.id()) {pending.landed=true;state_.casts.at(actor.actor).until=actor.tick+4;return DomainStatus::Unavailable;}
        pending.unit=victim.id();pending.unitType=victim.player?0:1;pending.landed=true;
        const auto sequence=barbarianSequence(14,owner.totals.equipment.animationClass);
        pending.animationEvents.clear();for(const auto &frame:sequence) pending.animationEvents.push_back(frame.hit?1:0);
        pending.weaponFrames=int(sequence.size());
        const auto timeline=rewoundPlayerActionTimeline(pending.weaponFrames,pending.weaponSpeed,16,pending.animationEvents);
        auto prepared=state_.prepareTimeline(actor.actor,actor.tick,timeline);
        pending.weaponHits.clear();for(const auto &event:timeline.events) if(event.event==1 || event.event==2) pending.weaponHits.push_back(event.tick);
        pending.tick=actor.tick+1;
        auto &cast=state_.casts.at(actor.actor);cast.timeline=prepared.generation;cast.until=actor.tick+uint64_t(timeline.end);
        state_.commitTimeline(std::move(prepared));
        return DomainStatus::Applied;
    }
    effects::NativeAreaSpec spec;spec.source=actor.actor;spec.area=actor.area;spec.center=landing;
    spec.radius=pending.skill.leap->knockbackRadius;spec.skill=pending.skill.sourceId;spec.rank=pending.skill.rank;
    // SrvDo077 -> sub_6FD10200: zero damage, SUCCESSFULHIT|KNOCKBACK,
    // and the native default finder, including hostile players/pets.
    spec.filter=0x8583;spec.resultFlags=9;
    const auto knocked=ports_.effects.nativeArea(spec,actor.tick,pending.directSkill);
    if(!knocked) return knocked.status;
    pending.landed=true;state_.casts.at(actor.actor).until=actor.tick+1;
    return DomainStatus::Applied;
}
DomainStatus System::activateCorpseSearch(Release &pending,const ActorContext &actor,Vec center) {
    if(!ports_.monsters.usableCorpse(pending.unit,actor.area,actor.tick)) return DomainStatus::Unavailable;
    const auto &owner=*ports_.players.find(actor.player);const auto &corpse=*ports_.monsters.find(pending.unit);
    if(pending.skill.pd2 && !pd2SearchableCorpseClass(corpse.rule.nativeClass)) return DomainStatus::Unavailable;
    if(pending.skill.corpseSearch->potion && !corpse.rule.softCorpse) return DomainStatus::Unavailable;
    // Wait for the original death treasure request to leave this source slot.
    if(ports_.loot.read().pending.contains(pending.unit)) return DomainStatus::Capacity;
    const auto &program=*pending.skill.corpseSearch;auto random=corpse.combatRandom;
    const bool found=limitedRandom(random,100)<unsigned(program.chance);
    transactions::CharacterEdit payment{actor,owner.inventoryRevision,owner.characterRevision,owner.persistent.player};
    payment.player.mana-=pending.skill.manaCost;payment.skillPayment=transactions::SkillManaPayment{pending.skill.manaCost,pending.skill.sourceId};payment.charge=pending.skill.charge;
    payment.publicFacts.emplace_back(StateFact{pending.unit,1,actor.area,program.noSelectState,true});
    DomainResult<> result;
    if(found && !program.potion) {
        LootRequest source;source.source=pending.unit;source.identity=corpse.identity;source.region=actor.area;source.difficulty=owner.persistent.difficulty;
        source.sourceSeed=true;source.rewardModifiers=monsterRewardModifiers(corpse.rule.enchantment);source.monsterPlayerCount=corpse.admittedPlayerCount;source.ignoreNoDrop=true;
        source.random=random;
        result=ports_.loot.queue({actor.sequence,source,actor.player,center},std::move(payment));
    } else if(found) {
        const int act=ports_.areas.at(actor.area).definition.act;
        const size_t index=size_t(act+5*owner.persistent.difficulty);
        const unsigned roll=limitedRandom(random,100);
        const int column=roll<unsigned(program.manaPotionChance)?1:roll<unsigned(program.manaPotionChance+program.rejuvenationChance)?2:0;
        LootRequest source;source.source=pending.unit;source.identity=corpse.identity;source.region=actor.area;source.difficulty=owner.persistent.difficulty;
        loot::Request request{actor.sequence,source,actor.player,center};request.object=loot::ObjectSource{};
        request.object->reportCompletion=false;
        request.source.random=random;
        request.object->dropCode=program.potions.at(index).at(size_t(column));request.object->dropCount=1;request.object->dropLevel=corpse.rule.level;
        result=ports_.loot.queue(request,std::move(payment));
    } else {
        auto plan=ports_.transactions.prepare(std::move(payment));if(!plan) return plan.status;
        result=ports_.transactions.commit(std::move(*plan.value));
    }
    if(result) {ports_.monsters.searchCorpse(pending.unit);ports_.monsters.commitCombatRandom(pending.unit,random);}
    return result.status;
}
DomainStatus System::activateGrimWard(Release &pending,const ActorContext &actor,Vec center) {
    if(!ports_.monsters.usableCorpse(pending.unit,actor.area,actor.tick)) return DomainStatus::Unavailable;
    const auto &corpse=*ports_.monsters.find(pending.unit);if(!corpse.rule.softCorpse) return DomainStatus::Unavailable;
    const auto location=ports_.areas.at(actor.area).definition.collision.nativeSpawn(center,1,{0x1000,2});
    if(!location) return DomainStatus::Unavailable;
    const auto &owner=*ports_.players.find(actor.player);
    auto skill=pending.skill;skill.missileCount=corpse.rule.corpseSize;
    transactions::CharacterEdit payment{actor,owner.inventoryRevision,owner.characterRevision,owner.persistent.player};
    payment.player.mana-=skill.manaCost;payment.skillPayment=transactions::SkillManaPayment{skill.manaCost,skill.sourceId};payment.charge=skill.charge;
    payment.publicFacts.emplace_back(StateFact{pending.unit,1,actor.area,skill.grimWard->noSelectState,true});
    payment.publicFacts.emplace_back(StateFact{pending.unit,1,actor.area,skill.grimWard->noDrawState,true});
    auto cost=ports_.transactions.prepare(std::move(payment));if(!cost) return cost.status;
    if(skill.pd2) {
        const auto size=corpse.rule.corpseSize;
        const auto missile=skill.pd2->missiles.at(size==0?1:size==2?2:0);
        if(missile<0) return DomainStatus::Unavailable;
        const std::array births{missiles::NativeSpawn{actor.actor,actor.area,actor.areaGeneration,actor.tick,*location,{},missile,skill.sourceId,skill.rank}};
        auto prepared=ports_.missiles.prepareNativeBirth(births);if(!prepared) return prepared.status;
        cost.value->missileBirth=std::move(*prepared.value);
        const auto result=ports_.transactions.commit(std::move(*cost.value));
        if(result) ports_.monsters.consumeCorpse(pending.unit);
        return result.status;
    }
    missiles::Spawn request{actor,std::move(skill),{},*location,true};request.cost=std::move(*cost.value);
    const auto result=ports_.missiles.spawn(request);if(result) ports_.monsters.consumeCorpse(pending.unit);
    return result.status;
}
DomainStatus System::activateCorpse(Release &pending,const ActorContext &actor,Vec) {
    if(pending.skill.effect==SkillBehavior::PoisonExplosion) {
        const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
        if(!caster || !caster->programs()) return DomainStatus::InvalidActor;
        const auto &catalog=*caster->programs()->formulas;
        const auto missile=catalog.missileIds.find(catalog.skills.at(pending.skill.sourceId).reference("srvmissilea"));
        if(missile==catalog.missileIds.end()) return DomainStatus::Applied;
        const auto *corpse=ports_.monsters.find(pending.unit);
        if(!corpse) return DomainStatus::Unavailable;
        if(!pending.manaPaid) {
            if(!ports_.monsters.nativeUsableCorpse(pending.unit,actor.tick)) return DomainStatus::Unavailable;
            if(corpse->revision==UINT64_MAX) return DomainStatus::Capacity;
            const int noSelect=catalog.corpseNoSelectState;
            if(noSelect<0) return DomainStatus::Unavailable;
            auto candidate=*corpse;candidate.corpse.states.insert(noSelect);++candidate.revision;
            auto publication=std::make_shared<effects::AuraUnitPublication>();publication->tick=actor.tick;
            ports_.effects.stageAuraActors(*publication,{{pending.unit,std::move(candidate)}},0);
            publication->facts.emplace_back(StateFact{pending.unit,1,corpse->area,noSelect,true});
            DomainResult<> consumed;
            if(caster->player) {
                const auto &p=*caster->player;
                transactions::CharacterEdit edit{actor,p.inventoryRevision,p.characterRevision,p.persistent.player};
                edit.player.mana-=pending.skill.manaCost;edit.skillPayment=transactions::SkillManaPayment{pending.skill.manaCost,pending.skill.sourceId};edit.charge=pending.skill.charge;
                auto plan=ports_.transactions.prepare(std::move(edit),nullptr,{},publication);
                if(!plan) return plan.status;
                consumed=ports_.transactions.commit(std::move(*plan.value));
            } else consumed=ports_.transactions.commitUnitAuras({},publication,actor);
            if(!consumed) return consumed.status;
            pending.manaPaid=true;pending.nativeCallbackStarted=true;
        }
        corpse=ports_.monsters.find(pending.unit);
        if(!corpse) return DomainStatus::Unavailable;
        std::vector<missiles::NativeSpawn> births;
        const int velocity=nativeShiftLeft(catalog.missiles.at(missile->second).number("param1"),7);
        for(const auto heading:poisonCloudDirections(2,0)) {
            missiles::NativeSpawn birth{actor.actor,corpse->area,ports_.areas.at(corpse->area).generation,actor.tick,
                corpse->position,corpse->position+heading.direction,missile->second,pending.skill.sourceId,pending.skill.rank};
            birth.nativeVelocity=velocity;birth.velocityFixed=true;
            births.push_back(std::move(birth));
        }
        auto prepared=ports_.missiles.prepareNativeBirth(births);
        if(!prepared) return prepared.status;
        const auto result=ports_.missiles.commitNativeBirth(std::move(*prepared.value));
        if(result) pending.nativeCallbackStarted=false;
        return result.status;
    }
    const auto result=ports_.effects.corpseExplosion(actor.actor,pending.unit,pending.skill.sourceId,pending.skill.rank,
        actor.tick,pending.corpseExplosion,&pending.skill);
    pending.nativeCallbackStarted=pending.corpseExplosion && pending.corpseExplosion->prepared && !pending.corpseExplosion->complete;
    return result.status;
}
DomainStatus System::activateItem(Release &pending, const ActorContext &actor, Vec) {
    return ports_.inventory.useSkill(actor, pending.skill.sourceId).status;
}
DomainStatus System::activateUnsummon(Release &pending, const ActorContext &actor, Vec) {
    return ports_.companions.dismiss(actor, pending.unit).status;
}
DomainStatus System::activateKick(Release &pending, const ActorContext &actor, Vec) {
    if (pending.unitType == 2) return DomainStatus::Applied;
    return pending.weaponMelee?drainWeaponMelee(pending,actor):DomainStatus::Unavailable;
}
DomainStatus System::activateAmazonSummon(Release &pending, const ActorContext &actor, Vec target) {
    return pending.skill.summon && pending.skill.summon->amazon?ports_.companions.amazon(actor,pending.skill,target).status:ports_.companions.necro(actor,pending.skill,target,pending.unit).status;
}
DomainStatus System::activateAmazonMagic(Release &pending, const ActorContext &actor, Vec) {
    return ports_.effects.amazonMagic(actor, pending.skill).status;
}
DomainStatus System::activateTeleport(Release &pending, const ActorContext &actor, Vec target) {
    if(pending.skill.pd2) return activatePd2Teleport(pending,actor,target);
    return ports_.travel.castTeleport(actor, {actor.area, actor.areaGeneration, target}, pending.skill.manaCost, pending.skill.charge,pending.skill.sourceId).status;
}
DomainStatus System::activateHydra(Release &pending, const ActorContext &actor, Vec target) {
    return ports_.companions.hydra(actor, pending.skill, target).status;
}
DomainStatus System::activateWarcry(Release &pending,const ActorContext &actor,Vec target) {
    if(pending.skill.pd2) return activatePd2(pending,actor,target);
    if(!pending.manaPaid) {
        const auto result=ports_.effects.skill(actor,pending.skill);if(!result) return result.status;
        pending.manaPaid=true;
    }
    return ports_.missiles.spawn({actor,pending.skill,pending.collision,target,true}).status;
}
DomainStatus System::activateEffect(Release &pending, const ActorContext &actor, Vec) {
    if(pending.skill.effect==SkillBehavior::AreaEnchant) return activatePd2Enchant(pending,actor);
    const auto &skill = pending.skill;
    if (skill.effect == SkillBehavior::Enchant && pending.unit && pending.unitType == 1)
        return ports_.effects.skillUnit(actor, skill, pending.unit).status;
    std::optional<PlayerId> recipient;
    if (skill.effect == SkillBehavior::Enchant && pending.unit)
        for (const auto &[id, other] : ports_.players.all())
            if (other.actor == pending.unit) { recipient = id; break; }
    return ports_.effects.skill(actor, skill, recipient).status;
}
DomainStatus System::activateStaticField(Release &pending, const ActorContext &actor, Vec) {
    const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs()) return DomainStatus::InvalidActor;
    const auto &rules=*caster->programs();
    if(!pending.directSkill) {
        if(ports_.areas.at(actor.area).definition.town) return DomainStatus::Unavailable;
        auto random=caster->random();
        const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
            return low>=high?low:nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
        };
        auto input=caster->formulaInput(ports_.effects,actor.tick,draw);SkillFormulaValues values(*rules.formulas,input);
        const auto formula=[&](std::string_view field) {const auto value=values.calculation(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,field);
            if(!value) throw std::logic_error("Missing native Static Field formula");
            return *value;
            };
        NativeStaticField program;program.minimumDamage=formula("calc2");program.percent=formula("calc1");
        program.minimumPercent=rules.staticMinimum.at(size_t(caster->player?caster->player->persistent.difficulty:caster->monster->rule.difficulty));
        const auto length=values.parameter(FormulaDomain::Skill,pending.skill.sourceId,pending.skill.rank,"edln");
        if(!length) return DomainStatus::Unavailable;
        program.length=*length;const auto &row=rules.formulas->skills.at(pending.skill.sourceId);
        program.element=MissileDamageElement(row.elementType);
        const auto radius=formula("aurarangecalc");
        auto pulse=std::make_shared<effects::NativeAreaPulse>();auto &spec=pulse->spec;
        spec.source=actor.actor;spec.area=actor.area;spec.center=caster->position();spec.skill=pending.skill.sourceId;spec.rank=pending.skill.rank;
        spec.directFunction=20;spec.staticField=program;
        spec.targets=ports_.effects.nativeAuraCandidateRecipients({actor.actor,actor.area,radius,uint32_t(row.number("aurafilter")),false,
            {},effects::NativeAuraTargetMode::OwnerRange,actor.tick});
        if(caster->monster) {
            const auto consumed=ports_.monsters.consumeRandom(actor.actor,caster->random(),random);if(!consumed) return consumed.status;
        } else {
            const auto &player=*caster->player;
            transactions::CharacterEdit payment{actor,player.inventoryRevision,player.characterRevision,player.persistent.player};
            if(!pending.manaPaid) {payment.player.mana-=pending.skill.manaCost;payment.skillPayment=transactions::SkillManaPayment{pending.skill.manaCost,pending.skill.sourceId};payment.charge=pending.skill.charge;}
            payment.random=transactions::UnitRandomEdit{player.combatRandom,random};
            auto plan=ports_.transactions.prepare(std::move(payment));if(!plan) return plan.status;
            const auto paid=ports_.transactions.commit(std::move(*plan.value));if(!paid) return paid.status;
        }
        pending.manaPaid=true;pending.directSkill=std::move(pulse);
    }
    pending.nativeCallbackStarted=true;
    auto result=ports_.effects.nativeArea(pending.directSkill->spec,actor.tick,pending.directSkill);
    if(result && caster->player && pending.skill.pd2 && pending.skill.curse) {
        // Current Do160 (RVA 0x3AF930) completes Static Field damage before
        // posting its six aura stat slots. The retained damage pulse prevents
        // repeating hits if the later state publication encounters backpressure.
        auto debuff=evaluate(*ports_.players.find(actor.player),pending.skill.sourceId,pending.skill.rank);
        debuff.manaCost=debuff.startMana=0;debuff.charge.reset();
        result=ports_.effects.curse(actor,debuff,ports_.players.find(actor.player)->position,{});
    }
    if(result) pending.nativeCallbackStarted=false;
    return result.status;
}
DomainStatus System::activateTelekinesis(Release &pending, const ActorContext &actor, Vec target) {
    const auto &player = *ports_.players.find(actor.player);
    const auto &skill = pending.skill;
    if (!pending.directSkill && !withinSkillArea(player.position,target,float(skill.telekinesisRange),SkillAreaCoordinates::TruncatedCells)) return DomainStatus::InvalidRequest;
    switch (pending.unitType) {
    case 0:
    case 1: {
        if(!pending.directSkill) {
            const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};const auto recipient=participants.find(pending.unit);
            const auto targetArea=recipient.player?recipient.player->area:recipient.monster?recipient.monster->area:RegionId{};
            if(!recipient.aliveIn(targetArea) || !participants.canHarm(actor.actor,pending.unit) ||
               ports_.areas.at(actor.area).definition.town || ports_.areas.at(targetArea).definition.town) return DomainStatus::Unavailable;
            auto pulse=std::make_shared<effects::NativeAreaPulse>();auto &spec=pulse->spec;
            spec.source=actor.actor;spec.area=actor.area;spec.center=player.position;spec.skill=skill.sourceId;spec.rank=skill.rank;
            spec.directFunction=21;spec.targets=std::vector<EntityId>{pending.unit};
            const auto paid=ports_.transactions.release(actor,player.characterRevision,pending.manaPaid?Fixed8{}:skill.manaCost,{},pending.manaPaid?std::optional<SkillCharge>{}:skill.charge,{},{},pending.manaPaid?-1:skill.sourceId);
            if(!paid) return paid.status;
            pending.manaPaid=true;pending.directSkill=std::move(pulse);
        }
        pending.nativeCallbackStarted=true;
        const auto result=ports_.effects.nativeArea(pending.directSkill->spec,actor.tick,pending.directSkill);
        if(result) pending.nativeCallbackStarted=false;
        return result.status;
    }
    case 4: return ports_.inventory.telekinesis(actor, pending.unit, skill).status;
    case 2:
        if (!pending.manaPaid) {
            const auto debit = ports_.transactions.release(actor, player.characterRevision, skill.manaCost, {}, skill.charge,{},{},skill.sourceId);
            if (!debit) return debit.status;
            pending.manaPaid = true;
        }
        if (ports_.travel.portalPosition(actor, pending.unit))
            return ports_.travel.useSpecial(actor, {travel::Kind::Portal, {pending.unit, 0}, {}}, skill.telekinesisRange).status;
        return ports_.objects.execute(actor, {{pending.unit, 0}}, skill.telekinesisRange).status;
    default: return DomainStatus::InvalidRequest;
    }
}
DomainStatus System::activateCurse(Release &pending,const ActorContext &actor,Vec target) {
    if(pending.skill.pd2 && pending.skill.pd2->doFunction==71) {
        const auto *owner=ports_.players.find(actor.player);if(!owner) return DomainStatus::InvalidActor;
        return ports_.effects.curse(actor,pending.skill,owner->position,{}).status;
    }
    if(pending.skill.curse && pending.skill.curse->ai==CurseAi::Taunt) {
        const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
        const auto eligible=[&](const monsters::Actor *m) {return m && m->alive() && !m->owner &&
            skillAiControlAllowed(m->identity.rank,m->rule.switchAi,m->rule.boss) && participants.canHarm(actor.actor,m->id);};
        const auto *victim=ports_.monsters.find(pending.unit);
        if(!eligible(victim)) {
            victim=nullptr;int32_t best=INT32_MAX;
            const auto caster=nativeCaster(ports_.players,ports_.monsters,actor.actor);
            if(!caster) return DomainStatus::InvalidActor;
            const Vec origin=caster->position()+ports_.areas.at(actor.area).definition.origin;
            for(const auto id:ports_.effects.nativeAuraCandidateRecipients({actor.actor,actor.area,20,0x8783u,false})) {
                const auto *candidate=ports_.monsters.find(id);if(!eligible(candidate)) continue;
                const Vec point=candidate->position+ports_.areas.at(candidate->area).definition.origin;
                const auto dx=int32_t(point.x)-int32_t(origin.x),dy=int32_t(point.y)-int32_t(origin.y);
                const auto distance=dx*dx+dy*dy;
                if(distance<best) {best=distance;victim=candidate;}
            }
        }
        if(!victim) return DomainStatus::Unavailable;
        return ports_.effects.curse(actor,pending.skill,
            victim->position+ports_.areas.at(victim->area).definition.origin-ports_.areas.at(actor.area).definition.origin,victim->id).status;
    }
    return ports_.effects.curse(actor,pending.skill,target,pending.unit).status;
}
DomainStatus System::activateMissile(Release &pending, const ActorContext &actor, Vec target) {
    if(pending.skill.pd2) return activatePd2(pending,actor,target);
    missiles::Spawn spawn{actor,pending.skill,pending.collision,target};spawn.guidedTarget=pending.unit;
    return ports_.missiles.spawn(spawn).status;
}
DomainStatus System::activateUnsupported(Release &, const ActorContext &, Vec) {
    return DomainStatus::NotImplemented;
}
}
