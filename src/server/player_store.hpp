#pragma once
#include "player_state.hpp"
#include <cstdint>
#include <map>

namespace d2x::server {
class AreaStore;
class GameInstance;
class MovementSystem;
struct RoomPoint;
namespace spatial { class System; }
namespace world { class System; }
namespace transactions { class System; }
namespace travel { class System; }
// Sole owner of admitted character saves. Domain services borrow const views;
// cross-domain writes belong to the explicit transaction boundary, not getters.
class PlayerStore {
    friend class GameInstance;
    friend class MovementSystem;
    friend class transactions::System;
    friend class travel::System;
    std::map<PlayerId, PlayerState> players_;
    void admit(PlayerId, CharacterDefinition, PersistentCharacter, AreaStore &, const PreparedRules &,uint64_t tick,uint64_t unitSeed,
        RoomPoint,const spatial::System &,world::System &);
  public:
    const PlayerState *find(PlayerId id) const {
        const auto found = players_.find(id);
        return found == players_.end() ? nullptr : &found->second;
    }
    const PlayerState *findActor(EntityId actor) const {
        for (const auto &entry : players_) {
            if (entry.second.actor == actor) return &entry.second;
        }
        return nullptr;
    }
    const std::map<PlayerId, PlayerState> &all() const { return players_; }
};
}
