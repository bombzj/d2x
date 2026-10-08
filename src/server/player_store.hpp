#pragma once
#include "player_state.hpp"
#include <map>

namespace d2x::server {
class AreaStore;
class GameInstance;
class MovementSystem;
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
    void admit(PlayerId, CharacterDefinition, PersistentCharacter, const AreaStore &, const PreparedRules &);
  public:
    const PlayerState *find(PlayerId id) const {
        const auto found = players_.find(id);
        return found == players_.end() ? nullptr : &found->second;
    }
    const std::map<PlayerId, PlayerState> &all() const { return players_; }
};
}
