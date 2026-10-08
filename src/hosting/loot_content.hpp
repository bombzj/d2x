#pragma once
#include "hosting/game_host.hpp"
#include "content/classic_data.hpp"
namespace d2x {
class MonsterCatalog;
class WorldCatalog;
struct LootContent {
    std::shared_ptr<const MonsterCatalog> monsters;
    std::map<int, std::shared_ptr<const WorldCatalog>> worlds;
};
void preparePendingLoot(GameHost &, GameHandle, Archives &, const ClassicData &, LootContent &);
}
