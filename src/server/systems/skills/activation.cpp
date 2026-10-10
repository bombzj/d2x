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

namespace d2x::server::skills {
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
    return ports_.companions.amazon(actor, pending.skill, target).status;
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
DomainStatus System::activateMissile(Release &pending, const ActorContext &actor, Vec target) {
    missiles::Spawn spawn{actor,pending.skill,pending.collision,target};spawn.guidedTarget=pending.unit;
    return ports_.missiles.spawn(spawn).status;
}
DomainStatus System::activateUnsupported(Release &, const ActorContext &, Vec) {
    return DomainStatus::NotImplemented;
}
}
