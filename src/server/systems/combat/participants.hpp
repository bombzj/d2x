#pragma once
#include "core/id.hpp"
#include "world/identity.hpp"

namespace d2x { struct Vec; }
namespace d2x::server {
struct PlayerState;
class PlayerStore;
namespace monsters { struct Actor; class System; }
namespace social { class System; }
namespace combat {
enum class Relation { Unknown, Self, Allied, Neutral, Hostile };
struct SourceBinding {
    EntityId actor, controller, credit;
    bool hostile{}, converted{};
    uint64_t allegianceRevision{};
    auto operator<=>(const SourceBinding &) const = default;
};
struct ParticipantView {
    const PlayerState *player{};
    const monsters::Actor *monster{};

    bool aliveIn(RegionId area) const;
    Vec position() const;
    int size() const;
    EntityId id() const;
    bool damageable() const;
    bool hostileMonster() const;
};

class Participants {
    const PlayerStore &players_;
    const monsters::System &monsters_;
  public:
    Participants(const PlayerStore &players, const monsters::System &monsters)
        : players_(players), monsters_(monsters) {}

    ParticipantView find(EntityId actor) const;
    SourceBinding bind(ParticipantView source) const;
    Relation relation(ParticipantView source, ParticipantView target, const social::System *social = nullptr) const;
    bool canHarm(ParticipantView source, ParticipantView target) const;
    bool canHarm(EntityId source, EntityId target) const;
    bool allied(ParticipantView source, ParticipantView target, const social::System &social) const;
};
}
}