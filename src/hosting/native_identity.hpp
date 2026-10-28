#pragma once
#include "core/id.hpp"
#include "server/player_identity.hpp"

namespace d2x {class GameHost;}

namespace d2x::hosting {
class NativeIdentityView {
    const GameHost &host_;
    GameHandle game_;
  public:
    NativeIdentityView(const GameHost &host,GameHandle game):host_(host),game_(game) {}
    uint32_t guid(EntityId entity,uint8_t type) const;
    uint32_t guid(EntityId entity) const;
    EntityId entity(uint32_t guid,uint32_t type) const;
};
}