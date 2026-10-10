#include "system.hpp"
#include "runtime.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/combat/geometry.hpp"
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
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server::skills {
DomainStatus System::activateCorpse(Release &pending,const ActorContext &actor,Vec center) {
    if(!ports_.monsters.usableCorpse(pending.unit,actor.area,actor.tick)) return DomainStatus::Unavailable;
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
    for(const auto &[id,m]:ports_.monsters.read().actors) if(m.enemyTarget() && m.life>0 && m.area==actor.area && area.activation.nearby(center,m.position)) {
        const int dx=int(m.position.x)-int(center.x),dy=int(m.position.y)-int(center.y),distance=dx*dx+dy*dy;
        const int radius=(program.radius+1)/2,physical=program.radius/2;
        if(distance<=radius*radius) fireTargets.push_back(id);
        if(distance<=physical*physical) physicalTargets.push_back(id);
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
    const auto *victim = ports_.monsters.find(pending.unit);
    const auto &area = ports_.areas.at(actor.area);
    if (!victim || !victim->enemyTarget() || victim->life <= 0 || victim->area != actor.area ||
        meleeDistance(player.position, 2, victim->position, victim->rule.size) > 1 ||
        !area.definition.collision.segment(player.position, victim->position)) return DomainStatus::Unavailable;
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
    for (const auto &[id, monster] : ports_.monsters.read().actors) {
        if (!monster.enemyTarget() || monster.life <= 0 || monster.area != actor.area) continue;
        const int deltaX = int(monster.position.x) - int(player.position.x);
        const int deltaY = int(monster.position.y) - int(player.position.y);
        const int radius = int(skill.staticRadius);
        if (deltaX * deltaX + deltaY * deltaY <= radius * radius) targets.push_back(id);
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
    const int deltaX = int(target.x) - int(player.position.x);
    const int deltaY = int(target.y) - int(player.position.y);
    if (deltaX * deltaX + deltaY * deltaY > skill.telekinesisRange * skill.telekinesisRange) return DomainStatus::InvalidRequest;
    switch (pending.unitType) {
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
