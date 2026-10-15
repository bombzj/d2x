#pragma once
#include "core/id.hpp"
#include "world/identity.hpp"
#include "gameplay/combat/alignment.hpp"
#include "gameplay/combat/source.hpp"
#include "gameplay/combat/relation.hpp"
#include <vector>

namespace d2x { struct Vec; }
namespace d2x::server {
struct PlayerState;
class PlayerStore;
namespace monsters { struct Actor; class System; }
namespace social { class System; }
namespace combat {
using Relation = CombatRelation;
using SourceBinding = CombatSourceBinding;
struct ParticipantView {
    const PlayerState *player{};
    const monsters::Actor *monster{};

    bool aliveIn(RegionId area) const;
    Vec position() const;
    int size() const;
    EntityId id() const;
    bool damageable() const;
    bool moving() const;
    bool hostileMonster() const;
    UnitAlignment alignment() const;
};

class Participants {
    const PlayerStore &players_;
    const monsters::System &monsters_;
    const social::System &social_;
  public:
    Participants(const PlayerStore &players, const monsters::System &monsters, const social::System &social)
        : players_(players), monsters_(monsters), social_(social) {}

    ParticipantView find(EntityId actor) const;
    std::vector<EntityId> enemies(EntityId source, RegionId area) const;
    SourceBinding bind(ParticipantView source) const;
    Relation relation(ParticipantView source, ParticipantView target) const;
    bool canHarm(ParticipantView source, ParticipantView target) const;
    bool canHarm(EntityId source, EntityId target) const;
    bool allied(ParticipantView source, ParticipantView target, const social::System &social) const;
    bool auraAligned(ParticipantView source,ParticipantView target) const;
    bool petOrPartyAlly(ParticipantView source, ParticipantView target, const social::System &social) const;
    bool skillFriendlyTarget(ParticipantView source,ParticipantView target,bool targetPet,bool targetAlly) const;
};
}
}
