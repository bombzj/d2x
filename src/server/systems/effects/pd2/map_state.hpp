#pragma once
#include "server/systems/attributes/calculation.hpp"
#include <cstdint>
#include <optional>
#include <vector>
namespace d2x::pd2 {struct MapInstance;struct MapCatalog;}
namespace d2x::server::effects {
struct MapPlayerStateChange {int state;bool active;std::vector<StatValue> values;};
std::optional<MapPlayerStateChange> preparePd2MapPlayerState(CombatEffectSet &,const pd2::MapCatalog &,const pd2::MapInstance *,
    const StatRuleCatalog &,EntityId recipient,uint64_t tick,const attributes::AuraSourceObserver &);
}
