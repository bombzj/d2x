#include "participants.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/social/system.hpp"
#include <algorithm>

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
bool ParticipantView::moving() const { return player ? player->moving : monster && monster->moving; }
std::vector<EntityId> Participants::enemies(EntityId actor,RegionId area) const {
    const auto source=find(actor);std::vector<EntityId> result;
    for(const auto &[id,player]:players_.all()) {
        (void)id;const ParticipantView target{&player,nullptr};
        if(target.aliveIn(area) && canHarm(source,target)) result.push_back(player.actor);
    }
    for(const auto &[id,monster]:monsters_.read().actors) {
        const ParticipantView target{nullptr,&monster};
        if(target.aliveIn(area) && canHarm(source,target)) result.push_back(id);
    }
    std::sort(result.begin(),result.end());return result;
}
bool ParticipantView::hostileMonster() const {
    return monster && monster->enemyTarget();
}
UnitAlignment ParticipantView::alignment() const {
    return player ? UnitAlignment::Good : monster ? monster->alignment() : UnitAlignment::Evil;
}
ParticipantView Participants::find(EntityId actor) const {
    if (!actor) return {};
    if (const auto *player = players_.findActor(actor)) return {player, nullptr};
    return {nullptr, monsters_.find(actor)};
}
SourceBinding Participants::bind(ParticipantView source) const {
    SourceBinding result;result.actor=source.id();result.credit=result.actor;
    result.hostile=source.hostileMonster();
    result.alignment=source.alignment();
    if(source.player) result.controller=source.player->actor;
    else if(source.monster) {
        result.allegianceRevision=source.monster->allegianceRevision;
        result.converted=bool(source.monster->conversion);
        if(source.monster->owner) {
            const auto *owner=players_.find(*source.monster->owner);
            if(owner && owner->entered) {
                result.credit=owner->actor;
                if(!source.monster->conversion) result.controller=owner->actor;
            }
        }
    }
    return result;
}
Relation Participants::relation(ParticipantView source, ParticipantView target) const {
    if (!source.id() || !target.id()) return Relation::Unknown;
    if (source.id() == target.id()) return Relation::Self;
    const auto controller = [&](ParticipantView unit) -> const PlayerState * {
        if (unit.player) return unit.player->entered ? unit.player : nullptr;
        if (unit.monster && unit.monster->owner && !unit.monster->conversion) {
            const auto *owner = players_.find(*unit.monster->owner);
            return owner && owner->entered ? owner : nullptr;
        }
        return nullptr;
    };
    const auto *sourceOwner = controller(source), *targetOwner = controller(target);
    if (sourceOwner && targetOwner) {
        if (sourceOwner == targetOwner) return Relation::Allied;
        // SUNIT's harmful relation checks the attacker's directed PlayerList flag.
        // Party membership cannot cancel hostility; beneficial filters are separate.
        if (social_.flags(sourceOwner->player, targetOwner->player) & 8) return Relation::Hostile;
        if (social_.sameParty(sourceOwner->player, targetOwner->player)) return Relation::Allied;
        return Relation::Neutral;
    }
    if (source.hostileMonster() && target.hostileMonster()) {
        if(source.monster->curseControl.ai==CurseAi::Confuse || source.monster->attractionTarget==target.id()) return Relation::Hostile;
    }
    // Owner/party relations above are distinct from STAT_ALIGNMENT. Neutral
    // monsters are not automatically allied with other neutral monsters.
    return sameAlignment(source.alignment(), target.alignment()) ? Relation::Allied : Relation::Hostile;
}
bool Participants::canHarm(ParticipantView source, ParticipantView target) const {
    return target.damageable() && relation(source, target) == Relation::Hostile;
}
bool Participants::canHarm(EntityId source, EntityId target) const {
    return canHarm(find(source), find(target));
}
bool Participants::allied(ParticipantView source, ParticipantView target, const social::System &social) const {
    if (!source.id() || !target.id()) return false;
    if (source.id() == target.id()) return true;
    const auto controller = [&](ParticipantView unit) -> const PlayerState * {
        return unit.player ? unit.player : unit.monster && unit.monster->owner && !unit.monster->conversion ? players_.find(*unit.monster->owner) : nullptr;
    };
    const auto *first = controller(source), *second = controller(target);
    if (first && second) return first == second || social.sameParty(first->player, second->player);
    // Beneficial alignment does not inherit Confuse/Attract's attack exception.
    return sameAlignment(source.alignment(), target.alignment());
}
bool Participants::petOrPartyAlly(ParticipantView source, ParticipantView target, const social::System &social) const {
    if (!source.player) return false;
    // Conversion changes alignment and AI allegiance, not AIGENERAL pet ownership.
    if (target.monster && target.monster->conversion) return false;
    if (target.monster && target.monster->owner == source.player->player) return true;
    const auto *owner = target.player ? target.player : target.monster && target.monster->owner ? players_.find(*target.monster->owner) : nullptr;
    return owner && social.sameParty(source.player->player, owner->player);
}
}
