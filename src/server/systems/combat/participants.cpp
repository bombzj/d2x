#include "participants.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/social/system.hpp"

namespace d2x::server::combat {
bool ParticipantView::aliveIn(RegionId area) const {
    if (player) return player->entered && player->area == area && player->persistent.player.hp > 0;
    return monster && monster->area == area && monster->life > 0;
}
Vec ParticipantView::position() const {
    if (player) return player->position;
    return monster ? monster->position : Vec{};
}
int ParticipantView::size() const {
    if (player) return 2;
    return monster ? monster->rule.size : 0;
}
EntityId ParticipantView::id() const {
    return player ? player->actor : monster ? monster->id : EntityId{};
}
bool ParticipantView::damageable() const {
    return player || (monster && monster->damageable());
}
bool ParticipantView::hostileMonster() const {
    return monster && monster->enemyTarget();
}
ParticipantView Participants::find(EntityId actor) const {
    if (!actor) return {};
    if (const auto *player = players_.findActor(actor)) return {player, nullptr};
    return {nullptr, monsters_.find(actor)};
}
SourceBinding Participants::bind(ParticipantView source) const {
    SourceBinding result;result.actor=source.id();result.credit=result.actor;
    result.hostile=source.hostileMonster();
    if(source.player) result.controller=source.player->actor;
    else if(source.monster) {
        result.allegianceRevision=source.monster->allegianceRevision;
        result.converted=bool(source.monster->conversion);
        if(source.monster->owner) {
            const auto *owner=players_.find(*source.monster->owner);
            if(owner && owner->entered) result.controller=result.credit=owner->actor;
        }
    }
    return result;
}
Relation Participants::relation(ParticipantView source, ParticipantView target, const social::System *social) const {
    if (!source.id() || !target.id()) return Relation::Unknown;
    if (source.id() == target.id()) return Relation::Self;
    const auto controller = [&](ParticipantView unit) -> const PlayerState * {
        if (unit.player) return unit.player->entered ? unit.player : nullptr;
        if (unit.monster && unit.monster->owner) {
            const auto *owner = players_.find(*unit.monster->owner);
            return owner && owner->entered ? owner : nullptr;
        }
        return nullptr;
    };
    const auto *sourceOwner = controller(source), *targetOwner = controller(target);
    if (sourceOwner && targetOwner) {
        if (sourceOwner == targetOwner || (social && social->sameParty(sourceOwner->player, targetOwner->player)))
            return Relation::Allied;
        return Relation::Neutral;
    }
    if ((sourceOwner && target.hostileMonster()) || (source.hostileMonster() && targetOwner)) return Relation::Hostile;
    if (source.hostileMonster() && target.hostileMonster()) return Relation::Allied;
    return Relation::Unknown;
}
bool Participants::canHarm(ParticipantView source, ParticipantView target) const {
    return target.damageable() && relation(source, target) == Relation::Hostile;
}
bool Participants::canHarm(EntityId source, EntityId target) const {
    return canHarm(find(source), find(target));
}
bool Participants::allied(ParticipantView source, ParticipantView target, const social::System &social) const {
    const auto value = relation(source, target, &social);
    return value == Relation::Self || value == Relation::Allied;
}
}