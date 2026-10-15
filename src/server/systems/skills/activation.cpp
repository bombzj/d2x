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
#include "server/systems/travel/system.hpp"
#include "server/systems/effects/system.hpp"
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
#include "gameplay/skills/targeting.hpp"
#include "server/movement.hpp"
#include <cmath>
#include "server/systems/loot/system.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server::skills {
DomainStatus System::activateWhirlwind(Release &pending,const ActorContext &actor,Vec destination) {
    const auto &owner=*ports_.players.find(actor.player);
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto moved=ports_.movement.leapStep(actor,destination,pending.chargeSpeed,MovementCollisionRule{0x804,2});
    if(!moved || *moved.value) {
        auto result=ports_.effects.removeState(actor,pending.skill.weapon->attackState.id);if(!result) return result.status;
        result=ports_.effects.removeState(actor,pending.skill.weapon->movingState.id);if(!result) return result.status;
        pending.whirlwind=false;state_.casts.at(actor.actor).until=actor.tick;
        ports_.movement.execute(actor,{MovementAction::Stop,{},false});return DomainStatus::Applied;
    }
    if(actor.tick>=pending.nextWhirlHit) {
        const size_t count=pending.offhand && pending.nextWhirlHit>state_.casts.at(actor.actor).started+4?2:1;
        while(pending.whirlHand<count) {
            const auto &hand=pending.offhand && pending.whirlHand%2?*pending.offhand:*pending.weapon;
            const auto last=owner.totals.equipment.weapons.begin()+owner.totals.equipment.weaponCount;
            const auto current=std::find_if(owner.totals.equipment.weapons.begin(),last,[&](const auto &w){return w.item==hand.item;});
            if(current==last) {pending.whirlHand=count;break;}
            std::vector<uint64_t> targets;
            for(const auto id:participants.enemies(actor.actor,actor.area)) {
                const auto target=participants.find(id);
                if(meleeDistance(owner.position,2,target.position(),target.size())<=current->rangeAdder+1 && ports_.areas.at(actor.area).definition.collision.segment(owner.position,target.position())) targets.push_back(id.value);
            }
            auto successor=missileChainSuccessor(pending.unit.value,targets);if(!successor && targets.size()==1) successor=targets.front();
            if(successor) {
                WeaponSkillDamage weapon;weapon.weapon=*current;weapon.level=owner.persistent.player.level;
                missiles::Spawn request{actor,pending.skill,{},participants.find(EntityId{successor}).position(),true};request.weapon=weapon;
                const auto hit=ports_.missiles.direct(request,{EntityId{successor}});if(hit.status==DomainStatus::Capacity) return hit.status;
                pending.unit=EntityId{successor};
            }
            ++pending.whirlHand;
        }
        const auto start=state_.casts.at(actor.actor).started;
        pending.nextWhirlHit+=uint64_t(pending.nextWhirlHit==start+4?4:pending.whirlPeriod);pending.whirlHand=0;
    }
    pending.tick=actor.tick+1;return DomainStatus::Applied;
}
DomainStatus System::activateLeap(Release &pending,const ActorContext &actor,Vec landing) {
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
    const auto moved=ports_.movement.leapStep(actor,landing,pending.chargeSpeed);if(!moved) return moved.status;
    if(!*moved.value) return DomainStatus::Applied;
    const auto cleared=ports_.effects.removeState(actor,pending.skill.leap->moving.id);if(!cleared) return cleared.status;
    if(pending.skill.leap->attack) {
        const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
        const auto reachable=[&](combat::ParticipantView m) {return m.aliveIn(actor.area) && participants.canHarm(actor.actor,m.id()) && meleeDistance(landing,2,m.position(),m.size())<=pending.weapon->rangeAdder+1 && ports_.areas.at(actor.area).definition.collision.segment(landing,m.position());};
        auto victim=participants.find(pending.unit);
        if(!reachable(victim)) {victim={};for(const auto id:participants.enemies(actor.actor,actor.area)) {const auto m=participants.find(id);if(reachable(m)) {victim=m;break;}}}
        if(!victim.id()) {pending.landed=true;state_.casts.at(actor.actor).until=actor.tick+4;return DomainStatus::Unavailable;}
        pending.unit=victim.id();pending.unitType=victim.player?0:1;pending.landed=true;
        const auto sequence=barbarianSequence(14,owner.totals.equipment.animationClass);
        int hit=16;for(size_t i=15;i<sequence.size();++i) if(sequence[i].hit) {hit=int(i);break;}
        pending.weaponHits={weaponSequenceTick(hit-15,pending.weaponSpeed)};
        pending.tick=actor.tick+uint64_t(pending.weaponHits.front());
        state_.casts.at(actor.actor).until=actor.tick+uint64_t(weaponSequenceTick(int(sequence.size())-15,pending.weaponSpeed));
        return DomainStatus::Applied;
    }
    const int radius=pending.skill.leap->knockbackRadius;
    for(const auto &[id,m]:ports_.monsters.read().actors) if(m.enemyTarget() && m.life>0 && m.area==actor.area && missileDistance(landing,m.position)<=radius)
        ports_.monsters.knockback(id,landing,actor.tick);
    pending.landed=true;state_.casts.at(actor.actor).until=actor.tick+4;
    return DomainStatus::Applied;
}
DomainStatus System::activateCorpseSearch(Release &pending,const ActorContext &actor,Vec center) {
    if(!ports_.monsters.usableCorpse(pending.unit,actor.area,actor.tick)) return DomainStatus::Unavailable;
    const auto &owner=*ports_.players.find(actor.player);const auto &corpse=*ports_.monsters.find(pending.unit);
    if(pending.skill.corpseSearch->potion && !corpse.rule.softCorpse) return DomainStatus::Unavailable;
    // Wait for the original death treasure request to leave this source slot.
    if(ports_.loot.read().pending.contains(pending.unit)) return DomainStatus::Capacity;
    const auto &program=*pending.skill.corpseSearch;auto random=corpse.combatRandom;
    const bool found=limitedRandom(random,100)<unsigned(program.chance);
    transactions::CharacterEdit payment{actor,owner.inventoryRevision,owner.characterRevision,owner.persistent.player};
    payment.player.mana-=pending.skill.manaCost;payment.charge=pending.skill.charge;
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
    payment.player.mana-=skill.manaCost;payment.charge=skill.charge;
    payment.publicFacts.emplace_back(StateFact{pending.unit,1,actor.area,skill.grimWard->noSelectState,true});
    payment.publicFacts.emplace_back(StateFact{pending.unit,1,actor.area,skill.grimWard->noDrawState,true});
    auto cost=ports_.transactions.prepare(std::move(payment));if(!cost) return cost.status;
    missiles::Spawn request{actor,std::move(skill),{},*location,true};request.cost=std::move(*cost.value);
    const auto result=ports_.missiles.spawn(request);if(result) ports_.monsters.consumeCorpse(pending.unit);
    return result.status;
}
DomainStatus System::activateCorpse(Release &pending,const ActorContext &actor,Vec center) {
    if(!ports_.monsters.consumableCorpse(pending.unit,actor.area,actor.tick)) return DomainStatus::Unavailable;
    const auto &owner=*ports_.players.find(actor.player);const auto &corpse=*ports_.monsters.find(pending.unit);
    if(pending.skill.effect==SkillBehavior::PoisonExplosion) {
        const auto result=ports_.missiles.spawn({actor,pending.skill,pending.collision,center,false,actor.actor,0,center});
        if(result) ports_.monsters.consumeCorpse(pending.unit);
        return result.status;
    }
    const auto &program=*pending.skill.bone;auto random=corpse.combatRandom;
    const int64_t life=corpse.rule.corpseBaseLife;
    if(life<=0) return DomainStatus::Unavailable;
    const int64_t minimum=life*program.minimumPercent/100,maximum=life*program.maximumPercent/100;
    int64_t damage=minimum+limitedRandom(random,uint32_t(std::max<int64_t>(0,maximum-minimum)));
    if(corpse.rule.level>owner.persistent.player.level) damage=damage*owner.persistent.player.level/corpse.rule.level;
    const auto fire=damage*program.elementalPercent/100;
    std::vector<EntityId> physicalTargets,fireTargets;
    const auto &area=ports_.areas.at(actor.area).definition;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    for(const auto id:participants.enemies(actor.actor,actor.area)) {
        const auto position=participants.find(id).position();
        if(!area.activation.nearby(center,position)) continue;
        const int radius=(program.radius+1)/2,physical=program.radius/2;
        if(withinSkillArea(center,position,float(radius),SkillAreaCoordinates::TruncatedCells)) fireTargets.push_back(id);
        if(withinSkillArea(center,position,float(physical),SkillAreaCoordinates::TruncatedCells)) physicalTargets.push_back(id);
    }
    combat::SpellImpact physical{pending.unit,actor.actor,actor.area,DamageType::Physical,damage-fire,std::move(physicalTargets)};
    combat::SpellImpact elemental{pending.unit,actor.actor,actor.area,DamageType::Fire,fire,std::move(fireTargets)};
    physical.occurrence=actor.sequence*2;elemental.occurrence=physical.occurrence+1;physical.unblockable=elemental.unblockable=true;
    auto hits=ports_.combat.prepareSpells({std::move(physical),std::move(elemental)});if(!hits) return hits.status;
    transactions::CharacterEdit edit{actor,owner.inventoryRevision,owner.characterRevision,owner.persistent.player};edit.player.mana-=pending.skill.manaCost;edit.charge=pending.skill.charge;
    edit.publicFacts.emplace_back(MissileFact{actor.actor,0,actor.area,program.corpseVisual,pending.skill.rank,0,center,center});
    auto debit=ports_.transactions.prepare(std::move(edit));if(!debit) return debit.status;
    const auto committed=ports_.transactions.commit(std::move(*debit.value));if(!committed) return committed.status;
    ports_.combat.commitSpells(std::move(*hits.value));ports_.monsters.consumeCorpse(pending.unit);ports_.monsters.commitCombatRandom(pending.unit,random);
    return DomainStatus::Applied;
}
DomainStatus System::activateItem(Release &pending, const ActorContext &actor, Vec) {
    return ports_.inventory.useSkill(actor, pending.skill.sourceId).status;
}
DomainStatus System::activateUnsummon(Release &pending, const ActorContext &actor, Vec) {
    return ports_.companions.dismiss(actor, pending.unit).status;
}
DomainStatus System::activateKick(Release &pending, const ActorContext &actor, Vec target) {
    if (pending.unitType == 2) return DomainStatus::Applied;
    const auto &player = *ports_.players.find(actor.player);
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto victim=participants.find(pending.unit);
    const auto &area = ports_.areas.at(actor.area);
    if (!victim.aliveIn(actor.area) || !participants.canHarm(actor.actor,pending.unit) ||
        meleeDistance(player.position, 2, victim.position(), victim.size()) > 1 ||
        !area.definition.collision.segment(player.position, victim.position())) return DomainStatus::Unavailable;
    return ports_.missiles.direct({actor, pending.skill, {}, target}, {pending.unit}).status;
}
DomainStatus System::activateAmazonSummon(Release &pending, const ActorContext &actor, Vec target) {
    return pending.skill.summon && pending.skill.summon->amazon?ports_.companions.amazon(actor,pending.skill,target).status:ports_.companions.necro(actor,pending.skill,target,pending.unit).status;
}
DomainStatus System::activateAmazonMagic(Release &pending, const ActorContext &actor, Vec) {
    return ports_.effects.amazonMagic(actor, pending.skill).status;
}
DomainStatus System::activateTeleport(Release &pending, const ActorContext &actor, Vec target) {
    return ports_.travel.teleport(actor, {actor.area, actor.areaGeneration, target}, pending.skill.manaCost, pending.skill.charge).status;
}
DomainStatus System::activateHydra(Release &pending, const ActorContext &actor, Vec target) {
    return ports_.companions.hydra(actor, pending.skill, target).status;
}
DomainStatus System::activateWarcry(Release &pending,const ActorContext &actor,Vec target) {
    if(!pending.manaPaid) {
        const auto result=ports_.effects.skill(actor,pending.skill);if(!result) return result.status;
        pending.manaPaid=true;
    }
    return ports_.missiles.spawn({actor,pending.skill,pending.collision,target,true}).status;
}
DomainStatus System::activateEffect(Release &pending, const ActorContext &actor, Vec) {
    const auto &skill = pending.skill;
    if (skill.effect == SkillBehavior::Enchant && pending.unit && pending.unitType == 1)
        return ports_.effects.skillUnit(actor, skill, pending.unit).status;
    std::optional<PlayerId> recipient;
    if (skill.effect == SkillBehavior::Enchant && pending.unit)
        for (const auto &[id, other] : ports_.players.all())
            if (other.actor == pending.unit) { recipient = id; break; }
    return ports_.effects.skill(actor, skill, recipient).status;
}
std::vector<EntityId> System::staticFieldTargets(const ActorContext &actor, const SkillCastSpec &skill) const {
    const auto &player = *ports_.players.find(actor.player);
    std::vector<EntityId> targets;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    for (const auto id:participants.enemies(actor.actor,actor.area)) {
        const auto target=participants.find(id);
        if (withinSkillArea(player.position,target.position(),float(int(skill.staticRadius)),SkillAreaCoordinates::TruncatedCells)) targets.push_back(id);
    }
    return targets;
}
DomainStatus System::activateStaticField(Release &pending, const ActorContext &actor, Vec) {
    const auto &player = *ports_.players.find(actor.player);
    return ports_.missiles.direct({actor, pending.skill, {}, player.position}, staticFieldTargets(actor, pending.skill)).status;
}
DomainStatus System::activateTelekinesis(Release &pending, const ActorContext &actor, Vec target) {
    const auto &player = *ports_.players.find(actor.player);
    const auto &skill = pending.skill;
    if (!withinSkillArea(player.position,target,float(skill.telekinesisRange),SkillAreaCoordinates::TruncatedCells)) return DomainStatus::InvalidRequest;
    switch (pending.unitType) {
    case 0:
    case 1: return ports_.missiles.direct({actor, skill, {}, target}, {pending.unit}).status;
    case 4: return ports_.inventory.telekinesis(actor, pending.unit, skill).status;
    case 2:
        if (!pending.manaPaid) {
            const auto debit = ports_.transactions.release(actor, player.characterRevision, skill.manaCost, {}, skill.charge);
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
    if(pending.skill.curse && pending.skill.curse->ai==CurseAi::Taunt) {
        const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
        const auto eligible=[&](const monsters::Actor *m) {return m && m->alive() && !m->owner &&
            skillAiControlAllowed(m->identity.rank,m->rule.switchAi,m->rule.boss) && participants.canHarm(actor.actor,m->id);};
        const auto *victim=ports_.monsters.find(pending.unit);
        if(!eligible(victim)) {
            victim=nullptr;int32_t best=INT32_MAX;
            const Vec origin=ports_.players.find(actor.player)->position+ports_.areas.at(actor.area).definition.origin;
            for(const auto id:ports_.effects.nativeAuraCandidateRecipients({actor.actor,actor.area,20,0x8783u,false})) {
                const auto *candidate=ports_.monsters.find(id);if(!eligible(candidate)) continue;
                const Vec point=candidate->position+ports_.areas.at(candidate->area).definition.origin;
                const auto dx=int32_t(point.x)-int32_t(origin.x),dy=int32_t(point.y)-int32_t(origin.y);
                const auto distance=dx*dx+dy*dy;
                if(distance<best) {best=distance;victim=candidate;}
            }
        }
        if(!victim) return DomainStatus::Unavailable;
        return ports_.effects.curse(actor,pending.skill,victim->position,victim->id).status;
    }
    return ports_.effects.curse(actor,pending.skill,target,pending.unit).status;
}
DomainStatus System::activateMissile(Release &pending, const ActorContext &actor, Vec target) {
    missiles::Spawn spawn{actor,pending.skill,pending.collision,target};spawn.guidedTarget=pending.unit;
    return ports_.missiles.spawn(spawn).status;
}
DomainStatus System::activateUnsupported(Release &, const ActorContext &, Vec) {
    return DomainStatus::NotImplemented;
}
}
