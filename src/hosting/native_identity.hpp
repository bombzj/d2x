#pragma once
#include "core/entity_id.hpp"
#include "server/player_identity.hpp"
#include <source_location>

namespace d2x {class GameHost;}

namespace d2x::hosting {
class NativeIdentityView {
    const GameHost &host_;
    GameHandle game_;
  public:
    NativeIdentityView(const GameHost &host,GameHandle game):host_(host),game_(game) {}
    uint32_t guid(EntityId entity,uint8_t type,std::source_location caller=std::source_location::current()) const;
    uint32_t guid(EntityId entity) const;
    EntityId entity(uint32_t guid,uint32_t type) const;
};
}