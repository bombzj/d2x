#pragma once
#include "core/entity_id.hpp"
#include "server/player_identity.hpp"
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace d2x::server {
struct ChatFact { EntityId actor; std::string name, text; std::vector<PlayerId> recipients; uint8_t type{1}, language{}, unitType{2}, nameColor{},messageColor{}; };
// Private native trade UI and offer projection; mirror items never own inventory.
struct TradeFact {
    std::optional<uint8_t> action{}; // Gold acknowledgements do not reopen/reset the UI.
    EntityId partner{};
    std::string name{};
    std::optional<unsigned> ownGold{}, peerGold{};
};
struct ChatRelationFact { EntityId from,to; uint16_t flags{}, reverse{}, fromLevel{}, toLevel{}; };
struct PartyNoticeFact {PlayerId recipient;EntityId player;uint8_t action{};};
}
