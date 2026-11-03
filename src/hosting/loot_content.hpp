#pragma once
#include "core/entity_id.hpp"
#include <map>
#include <memory>
#include "server/player_identity.hpp"

namespace d2x {
class GameHost;
struct ClassicData;
class Archives;

class MonsterCatalog;
class WorldCatalog;
struct LootContent {
    std::shared_ptr<const MonsterCatalog> monsters;
    std::map<int, std::shared_ptr<const WorldCatalog>> worlds;
    std::map<GameHandle,EntityId> cursors;
};
void preparePendingLoot(GameHost &, GameHandle, Archives &, const ClassicData &, LootContent &);
}
